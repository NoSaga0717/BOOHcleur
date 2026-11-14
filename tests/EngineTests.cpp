#include <catch2/catch_test_macros.hpp>

#include "boohcleur/core/Engine.h"

#include <cmath>
#include <functional>
#include <map>
#include <vector>

using namespace boohcleur;

namespace
{
	constexpr double sampleRate = 48000.0;
	constexpr int blockSize = 128;
	constexpr SamplePos bar = 96000;	// une mesure 4/4 à 120 BPM, 48 kHz

	// Banc hors ligne : fait tourner le moteur bloc par bloc, avec une entrée
	// générée et des commandes postées au début de blocs choisis.
	struct Bench
	{
		Engine engine;
		std::function<float(std::int64_t)> input = [](std::int64_t) { return 0.0f; };
		std::multimap<std::int64_t, Command> commands;	// bloc -> commande
		std::vector<float> left, right;
		std::int64_t block = 0;

		explicit Bench(EngineConfig config = {})
		{
			config.sampleRate = sampleRate;
			config.maxBlockSize = blockSize;
			REQUIRE(engine.prepare(config));
			engine.setMetronomeEnabled(false);
		}

		void at(std::int64_t blockIndex, CommandType type, double value = 0.0)
		{
			commands.insert({ blockIndex, Command { type, 0, value } });
		}

		// Bloc qui contient l'échantillon donné (pour poster une commande).
		static std::int64_t blockOf(SamplePos sample) { return sample / blockSize; }

		void run(SamplePos samples)
		{
			std::vector<float> inL(blockSize), inR(blockSize), outL(blockSize), outR(blockSize);

			for (SamplePos done = 0; done < samples; done += blockSize, ++block)
			{
				auto range = commands.equal_range(block);

				for (auto it = range.first; it != range.second; ++it)
					REQUIRE(engine.post(it->second));

				for (int i = 0; i < blockSize; ++i)
					inL[i] = inR[i] = input(block * blockSize + i);

				const float* in[2] = { inL.data(), inR.data() };
				float* out[2] = { outL.data(), outR.data() };
				engine.process(in, 2, out, 2, blockSize);

				left.insert(left.end(), outL.begin(), outL.end());
				right.insert(right.end(), outR.begin(), outR.end());
			}
		}

		float maxJump(SamplePos from, SamplePos to) const
		{
			float jump = 0.0f;

			for (SamplePos i = from; i + 1 < to; ++i)
				jump = std::max(jump, std::abs(left[static_cast<std::size_t>(i + 1)] - left[static_cast<std::size_t>(i)]));

			return jump;
		}

		float peak(SamplePos from, SamplePos to) const
		{
			float value = 0.0f;

			for (SamplePos i = from; i < to; ++i)
				value = std::max(value, std::abs(left[static_cast<std::size_t>(i)]));

			return value;
		}
	};

	std::function<float(std::int64_t)> impulses(std::vector<std::int64_t> positions, std::int64_t until)
	{
		return [positions, until](std::int64_t i)
		{
			if (i >= until)
				return 0.0f;

			for (auto p : positions)
				if (p == i)
					return 1.0f;

			return 0.0f;
		};
	}

	float sine(std::int64_t i) { return 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 437.3 * static_cast<double>(i) / sampleRate)); }
}

TEST_CASE("Moteur : premiere prise d'une mesure, rejouee en boucle")
{
	Bench bench;
	bench.input = impulses({ 1000, 50000 }, bar);
	bench.at(0, CommandType::TrackMain);							// sur la frontière : début à 0
	bench.at(Bench::blockOf(bar + 128), CommandType::TrackMain);	// 128 échantillons en retard : recadré sur la mesure 1
	bench.run(4 * bar);

	const auto snapshot = bench.engine.trackSnapshot(0);
	CHECK(snapshot.state == TrackState::Playing);
	CHECK(snapshot.loopLength == bar);
	CHECK(snapshot.numLayers == 1);

	for (int cycle = 1; cycle <= 2; ++cycle)
	{
		CHECK(bench.left[static_cast<std::size_t>(1000 + cycle * bar)] == 1.0f);
		CHECK(bench.left[static_cast<std::size_t>(50000 + cycle * bar)] == 1.0f);
		CHECK(bench.right[static_cast<std::size_t>(50000 + cycle * bar)] == 1.0f);
		CHECK(bench.peak(cycle * bar + 1001, cycle * bar + 50000) == 0.0f);
	}
}

TEST_CASE("Moteur : compensation de la latence aller-retour")
{
	Bench bench;
	bench.engine.setLatencySamples(300);
	// Le guitariste joue sur la position musicale 2000 : la carte son le rend 300 échantillons plus tard.
	bench.input = impulses({ 2000 + 300 }, bar + 300);
	bench.at(0, CommandType::TrackMain);
	bench.at(Bench::blockOf(bar), CommandType::TrackMain);
	bench.run(3 * bar);

	CHECK(bench.left[static_cast<std::size_t>(2000 + bar)] == 1.0f);
	CHECK(bench.left[static_cast<std::size_t>(2000 + 2 * bar)] == 1.0f);
}

TEST_CASE("Moteur : pression tardive, le debut est recupere dans le tampon")
{
	Bench bench;
	bench.input = impulses({ 100 }, bar);
	bench.at(2, CommandType::TrackMain);	// 256 échantillons après la frontière (5 ms) : début recadré à 0
	bench.at(Bench::blockOf(bar), CommandType::TrackMain);
	bench.run(3 * bar);

	CHECK(bench.engine.trackSnapshot(0).loopLength == bar);
	CHECK(bench.left[static_cast<std::size_t>(100 + bar)] == 1.0f);
}

TEST_CASE("Moteur : pression hors tolerance, attente de la mesure suivante")
{
	Bench bench;
	bench.engine.setLateToleranceMilliseconds(50.0f);
	bench.input = impulses({ bar + 500 }, 2 * bar);
	bench.at(Bench::blockOf(9600), CommandType::TrackMain);	// 200 ms après la mesure 0 : début à la mesure 1
	bench.run(bar / 2);

	CHECK(bench.engine.trackSnapshot(0).state == TrackState::Armed);
	CHECK(bench.engine.trackSnapshot(0).pending == PendingAction::StartRecording);

	bench.at(Bench::blockOf(2 * bar), CommandType::TrackMain);
	bench.run(3 * bar);

	CHECK(bench.engine.trackSnapshot(0).loopLength == bar);
	CHECK(bench.left[static_cast<std::size_t>(bar + 500 + bar)] == 1.0f);
}

TEST_CASE("Moteur : overdub, undo et redo")
{
	Bench bench;
	bench.input = [](std::int64_t i)
	{
		if (i == 1000) return 1.0f;					// première prise
		if (i == bar + 30000) return 0.5f;			// overdub pendant le premier tour de lecture
		return 0.0f;
	};

	bench.at(0, CommandType::TrackMain);
	bench.at(Bench::blockOf(bar), CommandType::TrackMain);					// fin de prise, lecture
	bench.at(Bench::blockOf(bar + 10000), CommandType::TrackMain);			// début d'overdub
	bench.at(Bench::blockOf(bar + 60000), CommandType::TrackMain);			// fin d'overdub
	bench.run(3 * bar);

	auto snapshot = bench.engine.trackSnapshot(0);
	CHECK(snapshot.numLayers == 2);
	CHECK(snapshot.numActiveLayers == 2);
	CHECK(bench.left[static_cast<std::size_t>(2 * bar + 1000)] == 1.0f);
	CHECK(bench.left[static_cast<std::size_t>(2 * bar + 30000)] == 0.5f);

	bench.at(bench.block, CommandType::TrackUndo);
	bench.run(bar);
	CHECK(bench.engine.trackSnapshot(0).numActiveLayers == 1);
	CHECK(bench.left[static_cast<std::size_t>(3 * bar + 1000)] == 1.0f);
	CHECK(bench.left[static_cast<std::size_t>(3 * bar + 30000)] == 0.0f);

	bench.at(bench.block, CommandType::TrackRedo);
	bench.run(bar);
	CHECK(bench.engine.trackSnapshot(0).numActiveLayers == 2);
	CHECK(bench.left[static_cast<std::size_t>(4 * bar + 30000)] == 0.5f);

	// Nouvel overdub après un undo : la couche annulée est remplacée.
	bench.at(bench.block, CommandType::TrackUndo);
	bench.at(bench.block + 2, CommandType::TrackMain);
	bench.at(bench.block + 100, CommandType::TrackMain);
	bench.run(bar);
	snapshot = bench.engine.trackSnapshot(0);
	CHECK(snapshot.numLayers == 2);
	CHECK(snapshot.numActiveLayers == 2);
}

TEST_CASE("Moteur : un overdub de plus d'un tour cree une couche par tour")
{
	Bench bench;
	bench.input = [](std::int64_t i) { return i < bar ? 0.0f : 0.001f; };
	bench.at(0, CommandType::TrackMain);
	bench.at(Bench::blockOf(bar), CommandType::TrackMain);
	bench.at(Bench::blockOf(bar + 1000), CommandType::TrackMain);
	bench.at(Bench::blockOf(bar + 1000 + 2 * bar + 500), CommandType::TrackMain);	// un peu plus de 2 tours
	bench.run(5 * bar);

	CHECK(bench.engine.trackSnapshot(0).numLayers == 4);	// prise + 3 tours d'overdub
}

TEST_CASE("Moteur : raccord de boucle sans discontinuite")
{
	Bench bench;
	bench.input = [](std::int64_t i) { return i < bar + 2000 ? sine(i) : 0.0f; };
	bench.at(0, CommandType::TrackMain);
	bench.at(Bench::blockOf(bar), CommandType::TrackMain);
	bench.run(4 * bar);

	// Sinusoïde de 437,3 Hz : la mesure ne contient pas un nombre entier de périodes,
	// donc sans fondu le raccord saute. Pente maximale : 0,029 par échantillon.
	CHECK(bench.maxJump(bar + 4000, 4 * bar) < 0.04f);
}

TEST_CASE("Moteur : stop, play et clear avec fondus")
{
	Bench bench;
	bench.input = [](std::int64_t i) { return i < bar + 2000 ? sine(i) : 0.0f; };
	bench.at(0, CommandType::TrackMain);
	bench.at(Bench::blockOf(bar), CommandType::TrackMain);
	bench.at(Bench::blockOf(bar + 30000), CommandType::TrackStop);
	bench.run(2 * bar);

	CHECK(bench.engine.trackSnapshot(0).state == TrackState::Stopped);
	CHECK(bench.peak(bar + 31000, 2 * bar) == 0.0f);

	bench.at(bench.block, CommandType::TrackPlay);	// quantifié : repart à la mesure suivante
	bench.run(2 * bar);
	CHECK(bench.engine.trackSnapshot(0).state == TrackState::Playing);
	CHECK(bench.peak(3 * bar + 100, 3 * bar + 1000) > 0.4f);

	bench.at(bench.block, CommandType::TrackClear);
	bench.run(bar);
	CHECK(bench.engine.trackSnapshot(0).state == TrackState::Empty);
	CHECK(bench.engine.trackSnapshot(0).numLayers == 0);

	CHECK(bench.maxJump(bar + 4000, static_cast<SamplePos>(bench.left.size())) < 0.04f);
}

TEST_CASE("Moteur : fermeture automatique a la duree maximale")
{
	EngineConfig config;
	config.maxLoopSeconds = 5.0;	// 2,5 mesures : fermeture à 2 mesures
	Bench bench(config);
	bench.at(0, CommandType::TrackMain);
	bench.run(4 * bar);

	const auto snapshot = bench.engine.trackSnapshot(0);
	CHECK(snapshot.state == TrackState::Playing);
	CHECK(snapshot.loopLength == 2 * bar);
}

TEST_CASE("Moteur : tempo verrouille tant que le transport tourne ou qu'une piste est pleine")
{
	Bench bench;
	bench.at(0, CommandType::SetTempo, 90.0);
	bench.run(blockSize);
	CHECK(bench.engine.transportSnapshot().bpm == 120.0);

	bench.at(bench.block, CommandType::TransportStop);
	bench.at(bench.block + 10, CommandType::SetTempo, 90.0);
	bench.at(bench.block + 11, CommandType::TransportStart);
	bench.run(20 * blockSize);
	CHECK(bench.engine.transportSnapshot().bpm == 90.0);
	CHECK(bench.engine.transportSnapshot().playing);
}

TEST_CASE("Moteur : l'arret du transport abandonne la premiere prise")
{
	Bench bench;
	bench.at(0, CommandType::TrackMain);
	bench.at(100, CommandType::TransportStop);
	bench.run(200 * blockSize);

	CHECK(bench.engine.trackSnapshot(0).state == TrackState::Empty);
	CHECK_FALSE(bench.engine.transportSnapshot().playing);
}

TEST_CASE("Moteur : guitare sur une seule entree, rejouee des deux cotes")
{
	auto runWith = [](InputMode mode, bool guitarOnLeft)
	{
		Engine engine;
		EngineConfig config;
		config.sampleRate = sampleRate;
		config.maxBlockSize = blockSize;
		REQUIRE(engine.prepare(config));
		engine.setMetronomeEnabled(false);
		engine.setInputMode(mode);

		std::vector<float> inL(blockSize), inR(blockSize), outL(blockSize), outR(blockSize);
		std::vector<float> left, right;

		for (std::int64_t block = 0; block * blockSize < 3 * bar; ++block)
		{
			if (block == 0 || block == Bench::blockOf(bar))
				REQUIRE(engine.post({ CommandType::TrackMain, 0, 0.0 }));

			for (int i = 0; i < blockSize; ++i)
			{
				const float value = block * blockSize + i == 1000 ? 1.0f : 0.0f;
				inL[i] = guitarOnLeft ? value : 0.0f;
				inR[i] = guitarOnLeft ? 0.0f : value;
			}

			const float* in[2] = { inL.data(), inR.data() };
			float* out[2] = { outL.data(), outR.data() };
			engine.process(in, 2, out, 2, blockSize);
			left.insert(left.end(), outL.begin(), outL.end());
			right.insert(right.end(), outR.begin(), outR.end());
		}

		const auto at = static_cast<std::size_t>(bar + 1000);
		return std::pair { left[at], right[at] };
	};

	CHECK(runWith(InputMode::MonoInput1, true) == std::pair { 1.0f, 1.0f });	// défaut : des deux côtés
	CHECK(runWith(InputMode::MonoInput2, false) == std::pair { 1.0f, 1.0f });
	CHECK(runWith(InputMode::MonoInput2, true) == std::pair { 0.0f, 0.0f });
	CHECK(runWith(InputMode::Stereo, true) == std::pair { 1.0f, 0.0f });
}
