#include <catch2/catch_test_macros.hpp>

#include "boohcleur/core/Engine.h"
#include "boohcleur/core/SamplePool.h"

#include <cmath>
#include <functional>
#include <map>
#include <vector>

using namespace boohcleur;

namespace
{
	constexpr double sampleRate = 48000.0;
	constexpr int blockSize = 128;
	constexpr SamplePos bar = 96000;

	struct Rig
	{
		Engine engine;
		std::function<float(std::int64_t)> input = [](std::int64_t) { return 0.0f; };
		std::multimap<std::int64_t, Command> commands;	// échantillon -> commande (au bloc qui le contient)
		std::multimap<std::int64_t, MidiEvent> midi;	// échantillon -> message
		std::vector<float> left, right;
		std::int64_t sample = 0;

		explicit Rig(EngineConfig config = {})
		{
			config.sampleRate = sampleRate;
			config.maxBlockSize = blockSize;
			REQUIRE(engine.prepare(config));
			engine.setMetronomeEnabled(false);
		}

		void at(SamplePos when, CommandType type, int track)
		{
			commands.insert({ when, Command { type, track, 0.0 } });
		}

		void run(SamplePos until)
		{
			std::vector<float> in(blockSize), outL(blockSize), outR(blockSize);
			std::vector<MidiEvent> events;

			for (; sample < until; sample += blockSize)
			{
				for (auto it = commands.lower_bound(sample); it != commands.end() && it->first < sample + blockSize; ++it)
					REQUIRE(engine.post(it->second));

				events.clear();

				for (auto it = midi.lower_bound(sample); it != midi.end() && it->first < sample + blockSize; ++it)
				{
					MidiEvent e = it->second;
					e.device = it->first - sample;
					events.push_back(e);
				}

				for (int i = 0; i < blockSize; ++i)
					in[static_cast<std::size_t>(i)] = input(sample + i);

				const float* ins[1] = { in.data() };
				float* outs[2] = { outL.data(), outR.data() };
				engine.process(ins, 1, outs, 2, blockSize, events.data(), static_cast<int>(events.size()));
				left.insert(left.end(), outL.begin(), outL.end());
				right.insert(right.end(), outR.begin(), outR.end());
			}
		}

		float out(SamplePos i) const { return left[static_cast<std::size_t>(i)]; }

		float maxJump(SamplePos from, SamplePos to) const
		{
			float jump = 0.0f;

			for (SamplePos i = from; i + 1 < to; ++i)
				jump = std::max(jump, std::abs(out(i + 1) - out(i)));

			return jump;
		}
	};

	float sine(std::int64_t i) { return 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 437.3 * static_cast<double>(i) / sampleRate)); }

	MidiEvent cc(int number, int value) { return { 0xB0, static_cast<std::uint8_t>(number), static_cast<std::uint8_t>(value), 0 }; }
}

TEST_CASE("Reserve : blocs pris et rendus")
{
	SamplePool pool;
	REQUIRE(pool.prepare(2, 3 * 2 * 1024 * sizeof(float) + 100, 1024));
	CHECK(pool.numChunks() == 3);

	const int a = pool.acquire();
	const int b = pool.acquire();
	const int c = pool.acquire();
	CHECK(pool.acquire() == -1);
	CHECK(pool.usedChunks() == 3);
	CHECK(a != b);
	CHECK(b != c);

	pool.channel(b, 1)[1023] = 0.25f;
	CHECK(pool.channel(b, 1)[1023] == 0.25f);

	pool.release(b);
	CHECK(pool.acquire() == b);
	CHECK_FALSE(pool.prepare(2, 100, 1024));
}

TEST_CASE("Boucles : six par defaut, longueurs differentes")
{
	Rig rig;
	CHECK(rig.engine.numTracks() == 6);

	rig.input = [](std::int64_t i)
	{
		if (i == 1000) return 1.0f;					// boucle 1 : une mesure
		if (i == 2 * bar + 5000) return 0.5f;		// boucle 2 : deux mesures, enregistrée ensuite
		return 0.0f;
	};

	rig.at(0, CommandType::TrackMain, 0);
	rig.at(bar, CommandType::TrackMain, 0);
	rig.at(2 * bar, CommandType::TrackMain, 1);
	rig.at(4 * bar, CommandType::TrackMain, 1);
	rig.run(9 * bar);

	CHECK(rig.engine.trackSnapshot(0).loopLength == bar);
	CHECK(rig.engine.trackSnapshot(1).loopLength == 2 * bar);
	CHECK(rig.engine.trackSnapshot(1).state == TrackState::Playing);

	for (SamplePos b = 4; b < 9; ++b)
		CHECK(rig.out(b * bar + 1000) == 1.0f);

	CHECK(rig.out(4 * bar + 5000) == 0.5f);
	CHECK(rig.out(5 * bar + 5000) == 0.0f);
	CHECK(rig.out(6 * bar + 5000) == 0.5f);
	CHECK(rig.out(8 * bar + 5000) == 0.5f);
}

TEST_CASE("Mixeur : volume, mute et volume general sans clic")
{
	Rig rig;
	rig.input = [](std::int64_t i) { return i < bar + 2000 ? sine(i) : 0.0f; };
	rig.at(0, CommandType::TrackMain, 0);
	rig.at(bar, CommandType::TrackMain, 0);
	rig.run(2 * bar);

	const auto peak = [&rig](SamplePos from, SamplePos to)
	{
		float value = 0.0f;
		for (SamplePos i = from; i < to; ++i)
			value = std::max(value, std::abs(rig.out(i)));
		return value;
	};

	CHECK(peak(2 * bar - 20000, 2 * bar) > 0.49f);

	rig.engine.setTrackVolume(0, 0.5f);
	rig.run(2 * bar + 30000);
	CHECK(peak(2 * bar + 10000, 2 * bar + 30000) < 0.26f);
	CHECK(peak(2 * bar + 10000, 2 * bar + 30000) > 0.24f);

	rig.engine.setTrackMuted(0, true);
	rig.run(2 * bar + 60000);
	CHECK(peak(2 * bar + 40000, 2 * bar + 60000) == 0.0f);

	rig.engine.setTrackMuted(0, false);
	rig.engine.setTrackVolume(0, 1.0f);
	rig.engine.setMasterVolume(0.25f);
	rig.run(3 * bar);
	CHECK(peak(2 * bar + 80000, 3 * bar) < 0.13f);

	CHECK(rig.maxJump(bar + 4000, 3 * bar) < 0.04f);	// aucun saut aux changements de gain
	CHECK(rig.engine.trackSnapshot(0).state == TrackState::Playing);	// muette mais toujours calée
}

TEST_CASE("Memoire : rendue a l'effacement et a l'abandon des couches annulees")
{
	Rig rig;
	rig.input = [](std::int64_t i) { return i >= bar ? 0.01f : 0.0f; };
	rig.at(0, CommandType::TrackMain, 0);
	rig.at(bar, CommandType::TrackMain, 0);
	rig.run(2 * bar);

	const auto oneLayer = rig.engine.memorySnapshot().usedBytes;
	CHECK(oneLayer > 0);
	CHECK(oneLayer < 2 * static_cast<std::size_t>(bar) * 2 * sizeof(float));	// une mesure stéréo, arrondie au bloc

	rig.at(2 * bar, CommandType::TrackMain, 0);					// overdub d'un tour
	rig.at(3 * bar, CommandType::TrackMain, 0);
	rig.run(4 * bar);
	const auto twoLayers = rig.engine.memorySnapshot().usedBytes;
	CHECK(twoLayers > oneLayer);

	rig.at(4 * bar, CommandType::TrackUndo, 0);
	rig.at(4 * bar + 1000, CommandType::TrackMain, 0);			// nouvel overdub : la couche annulée est abandonnée
	rig.at(5 * bar + 1000, CommandType::TrackMain, 0);
	rig.run(6 * bar);
	CHECK(rig.engine.memorySnapshot().usedBytes <= twoLayers + 16384 * 2 * sizeof(float));

	rig.at(6 * bar, CommandType::TrackClear, 0);
	rig.run(7 * bar);
	CHECK(rig.engine.memorySnapshot().usedBytes == 0);
	CHECK(rig.engine.trackSnapshot(0).state == TrackState::Empty);
}

TEST_CASE("Memoire pleine : la prise continue, la fin reste muette")
{
	EngineConfig config;
	config.memoryBudgetBytes = 2 * 16384 * 2 * sizeof(float);	// deux blocs : 0,68 s
	Rig rig(config);
	rig.input = [](std::int64_t i) { return i == 1000 || i == 50000 ? 1.0f : 0.0f; };
	rig.at(0, CommandType::TrackMain, 0);
	rig.at(bar, CommandType::TrackMain, 0);
	rig.run(3 * bar);

	CHECK(rig.engine.trackSnapshot(0).state == TrackState::Playing);
	CHECK(rig.engine.trackSnapshot(0).loopLength == bar);
	CHECK(rig.engine.memorySnapshot().secondsLeft == 0.0);
	CHECK(rig.out(2 * bar + 1000) == 1.0f);		// dans les blocs disponibles
	CHECK(rig.out(2 * bar + 50000) == 0.0f);	// au-delà : muet
}

TEST_CASE("Boucles actives : les suivantes sont ignorees")
{
	Rig rig;
	rig.engine.setActiveTracks(2);
	rig.engine.setSelectedTrack(4);
	CHECK(rig.engine.selectedTrack() == 0);	// refusé : hors des boucles actives

	rig.at(0, CommandType::TrackMain, 3);
	rig.at(0, CommandType::TrackMain, 1);
	rig.run(blockSize * 4);

	CHECK(rig.engine.trackSnapshot(3).state == TrackState::Empty);
	CHECK(rig.engine.trackSnapshot(1).state == TrackState::Recording);
}

TEST_CASE("Pedalier : boucle selectionnee, suivante et boucle dediee")
{
	Rig rig;
	rig.engine.setActiveTracks(3);

	auto bind = [&rig](int number, ControlAction action, std::uint8_t track)
	{
		ControlRequest r;
		r.type = ControlRequest::Type::AddBinding;
		r.binding.source = { MidiSourceKind::ControlChange, 1, static_cast<std::uint8_t>(number) };
		r.binding.action = action;
		r.binding.track = track;
		REQUIRE(rig.engine.midiControl().request(r));
	};

	bind(20, ControlAction::TrackMain, selectedTrack);
	bind(21, ControlAction::SelectNext, selectedTrack);
	bind(22, ControlAction::SelectPrevious, selectedTrack);
	bind(23, ControlAction::TrackMain, 2);	// pédale dédiée à la boucle 3

	auto press = [&rig](SamplePos at, int number)
	{
		rig.midi.insert({ at, cc(number, 127) });
		rig.midi.insert({ at + 2400, cc(number, 0) });
	};

	press(1000, 21);	// boucle 2 sélectionnée
	press(10000, 20);	// rec sur la boucle 2
	rig.run(20000);
	CHECK(rig.engine.selectedTrack() == 1);
	CHECK(rig.engine.trackSnapshot(1).state != TrackState::Empty);
	CHECK(rig.engine.trackSnapshot(0).state == TrackState::Empty);

	press(30000, 22);
	press(40000, 22);	// 0 -> 2 en reculant (tour complet)
	rig.run(50000);
	CHECK(rig.engine.selectedTrack() == 2);

	press(60000, 21);	// 2 -> 0
	rig.run(70000);
	CHECK(rig.engine.selectedTrack() == 0);

	press(80000, 23);	// la pédale dédiée arme la boucle 3 et la sélectionne
	rig.run(90000);
	CHECK(rig.engine.trackSnapshot(2).state != TrackState::Empty);
	CHECK(rig.engine.selectedTrack() == 2);
}

TEST_CASE("Boucle libre : quantification immediate, longueur au temps pres")
{
	Rig rig;
	rig.engine.setQuantizeDivision(GridDivision::Immediate);
	rig.input = [](std::int64_t i) { return i == 5000 ? 1.0f : 0.0f; };
	rig.at(1000, CommandType::TrackMain, 0);	// traité au bloc 896, au tick d'avant : 875
	rig.at(70000, CommandType::TrackMain, 0);	// bloc 69888 : 69875
	rig.run(4 * bar);

	const auto length = rig.engine.trackSnapshot(0).loopLength;
	CHECK(length == 69875 - 875);	// ni mesure ni temps : la durée jouée, au tick près (0,5 ms)
	CHECK(rig.out(5000 + length) == 1.0f);
	CHECK(rig.out(5000 + 3 * length) == 1.0f);
}
