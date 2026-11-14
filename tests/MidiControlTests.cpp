#include <catch2/catch_test_macros.hpp>

#include "boohcleur/core/Engine.h"
#include "boohcleur/core/MidiControl.h"

#include <map>
#include <vector>

using namespace boohcleur;

namespace
{
	constexpr double sampleRate = 48000.0;
	constexpr std::int64_t msec = 48;	// échantillons par milliseconde

	MidiEvent cc(int number, int value, std::int64_t at, int channel = 1)
	{
		return { static_cast<std::uint8_t>(0xB0 | (channel - 1)), static_cast<std::uint8_t>(number),
			static_cast<std::uint8_t>(value), at };
	}

	MidiEvent programChange(int number, std::int64_t at)
	{
		return { 0xC0, static_cast<std::uint8_t>(number), 0, at };
	}

	MidiSource ccSource(int number, int channel = 1)
	{
		return { MidiSourceKind::ControlChange, static_cast<std::uint8_t>(channel), static_cast<std::uint8_t>(number) };
	}

	Binding binding(MidiSource source, Gesture gesture, ControlAction action, PressMode mode = PressMode::Momentary)
	{
		Binding b;
		b.source = source;
		b.gesture = gesture;
		b.action = action;
		b.mode = mode;
		return b;
	}

	// Rejoue une suite de messages et de minuteries, et note les actions.
	struct Pedal
	{
		MidiControl control;
		std::vector<Trigger> fired;

		Pedal() { control.prepare(sampleRate); }

		void bind(const Binding& b)
		{
			ControlRequest r;
			r.type = ControlRequest::Type::AddBinding;
			r.binding = b;
			REQUIRE(control.request(r));
			control.processRequests();
		}

		void send(const MidiEvent& event)
		{
			TriggerList list;
			control.onMidi(event, list);
			collect(list);
		}

		void wait(std::int64_t until)
		{
			TriggerList list;
			control.update(until, list);
			collect(list);
		}

		void collect(const TriggerList& list)
		{
			for (int i = 0; i < list.count; ++i)
				fired.push_back(list.items[static_cast<std::size_t>(i)]);
		}
	};
}

TEST_CASE("MIDI : decodage des notes, CC et programmes")
{
	const auto on = MidiControl::decode({ 0x93, 60, 100, 0 }, PressMode::Momentary);
	CHECK(on.source == MidiSource { MidiSourceKind::Note, 4, 60 });
	CHECK(on.press);
	CHECK_FALSE(on.release);

	CHECK(MidiControl::decode({ 0x90, 60, 0, 0 }, PressMode::Momentary).release);	// vélocité 0
	CHECK(MidiControl::decode({ 0x80, 60, 0, 0 }, PressMode::Momentary).release);

	CHECK(MidiControl::decode(cc(20, 127, 0), PressMode::Momentary).press);
	CHECK(MidiControl::decode(cc(20, 64, 0), PressMode::Momentary).press);
	CHECK(MidiControl::decode(cc(20, 63, 0), PressMode::Momentary).release);

	const auto toggle = MidiControl::decode(cc(20, 0, 0), PressMode::EveryMessage);
	CHECK(toggle.press);
	CHECK(toggle.release);

	const auto pc = MidiControl::decode(programChange(5, 0), PressMode::Momentary);
	CHECK(pc.source == MidiSource { MidiSourceKind::ProgramChange, 1, 5 });
	CHECK(pc.press);

	CHECK(MidiControl::decode({ 0xF8, 0, 0, 0 }, PressMode::Momentary).source.kind == MidiSourceKind::None);
}

TEST_CASE("MIDI : appui court seul, declenche des l'appui")
{
	Pedal pedal;
	pedal.bind(binding(ccSource(20), Gesture::Short, ControlAction::TrackMain));

	pedal.send(cc(20, 127, 1000));
	REQUIRE(pedal.fired.size() == 1);
	CHECK(pedal.fired[0].action == ControlAction::TrackMain);
	CHECK(pedal.fired[0].pressDevice == 1000);

	pedal.send(cc(20, 0, 1000 + 100 * msec));
	pedal.wait(1000 + 2000 * msec);
	CHECK(pedal.fired.size() == 1);
}

TEST_CASE("MIDI : appui court et appui long sur la meme pedale")
{
	Pedal pedal;
	const auto source = ccSource(20);
	pedal.bind(binding(source, Gesture::Short, ControlAction::TrackMain));
	pedal.bind(binding(source, Gesture::Long, ControlAction::TrackUndo));

	// Appui bref : l'action courte part au relâchement, datée de l'appui.
	pedal.send(cc(20, 127, 0));
	CHECK(pedal.fired.empty());
	pedal.send(cc(20, 0, 150 * msec));
	REQUIRE(pedal.fired.size() == 1);
	CHECK(pedal.fired[0].action == ControlAction::TrackMain);
	CHECK(pedal.fired[0].pressDevice == 0);

	// Appui tenu : l'action longue part à 500 ms, sans action courte au relâchement.
	pedal.fired.clear();
	const std::int64_t t = 1000 * msec;
	pedal.send(cc(20, 127, t));
	pedal.wait(t + 400 * msec);
	CHECK(pedal.fired.empty());
	pedal.wait(t + 510 * msec);
	REQUIRE(pedal.fired.size() == 1);
	CHECK(pedal.fired[0].action == ControlAction::TrackUndo);
	CHECK(pedal.fired[0].pressDevice == t);
	pedal.send(cc(20, 0, t + 900 * msec));
	pedal.wait(t + 2000 * msec);
	CHECK(pedal.fired.size() == 1);
}

TEST_CASE("MIDI : double appui")
{
	Pedal pedal;
	const auto source = ccSource(21);
	pedal.bind(binding(source, Gesture::Short, ControlAction::TrackMain));
	pedal.bind(binding(source, Gesture::Double, ControlAction::TrackStop));

	pedal.send(cc(21, 127, 0));
	pedal.send(cc(21, 0, 80 * msec));
	pedal.send(cc(21, 127, 200 * msec));
	REQUIRE(pedal.fired.size() == 1);
	CHECK(pedal.fired[0].action == ControlAction::TrackStop);
	CHECK(pedal.fired[0].pressDevice == 0);	// daté du premier appui
	pedal.send(cc(21, 0, 280 * msec));
	pedal.wait(2000 * msec);
	CHECK(pedal.fired.size() == 1);

	// Un seul appui : l'action courte part après le délai, datée de l'appui.
	pedal.fired.clear();
	const std::int64_t t = 3000 * msec;
	pedal.send(cc(21, 127, t));
	pedal.send(cc(21, 0, t + 80 * msec));
	pedal.wait(t + 300 * msec);
	CHECK(pedal.fired.empty());
	pedal.wait(t + 400 * msec);
	REQUIRE(pedal.fired.size() == 1);
	CHECK(pedal.fired[0].action == ControlAction::TrackMain);
	CHECK(pedal.fired[0].pressDevice == t);
}

TEST_CASE("MIDI : anti-rebond")
{
	Pedal pedal;
	const auto source = ccSource(22);
	pedal.bind(binding(source, Gesture::Short, ControlAction::TrackMain));
	pedal.bind(binding(source, Gesture::Long, ControlAction::TrackUndo));

	// Contact qui rebondit à l'appui puis au relâchement : un seul appui court.
	pedal.send(cc(22, 127, 0));
	pedal.send(cc(22, 0, 3 * msec));
	pedal.send(cc(22, 127, 6 * msec));
	pedal.send(cc(22, 0, 200 * msec));
	pedal.send(cc(22, 127, 205 * msec));
	pedal.send(cc(22, 0, 210 * msec));
	pedal.wait(1000 * msec);
	REQUIRE(pedal.fired.size() == 1);
	CHECK(pedal.fired[0].action == ControlAction::TrackMain);
	CHECK(pedal.fired[0].pressDevice == 0);

	// Appui très bref mais réel (10 ms) : accepté une fois le délai passé.
	pedal.fired.clear();
	const std::int64_t t = 2000 * msec;
	pedal.send(cc(22, 127, t));
	pedal.send(cc(22, 0, t + 10 * msec));
	pedal.wait(t + 40 * msec);
	REQUIRE(pedal.fired.size() == 1);
	CHECK(pedal.fired[0].action == ControlAction::TrackMain);
}

TEST_CASE("MIDI : pedale a bascule, chaque message est un appui")
{
	Pedal pedal;
	pedal.bind(binding(ccSource(23), Gesture::Short, ControlAction::TrackMain, PressMode::EveryMessage));

	pedal.send(cc(23, 127, 0));
	pedal.send(cc(23, 0, 500 * msec));
	pedal.send(cc(23, 127, 1000 * msec));
	CHECK(pedal.fired.size() == 3);
}

TEST_CASE("MIDI : apprentissage d'une pedale")
{
	Pedal pedal;

	ControlRequest learn;
	learn.type = ControlRequest::Type::StartLearn;
	learn.binding.action = ControlAction::TrackMain;
	learn.binding.gesture = Gesture::Short;
	REQUIRE(pedal.control.request(learn));
	pedal.control.processRequests();
	pedal.control.publish();
	CHECK(pedal.control.snapshot().learning);

	// Le message appris ne déclenche rien, son relâchement non plus.
	pedal.send(cc(30, 127, 0, 2));
	pedal.send(cc(30, 0, 100 * msec, 2));
	pedal.wait(1000 * msec);
	CHECK(pedal.fired.empty());

	pedal.control.publish();
	auto state = pedal.control.snapshot();
	CHECK_FALSE(state.learning);
	REQUIRE(state.numBindings == 1);
	CHECK(state.bindings[0].source == ccSource(30, 2));
	CHECK(state.bindings[0].action == ControlAction::TrackMain);
	CHECK(state.bindings[0].mode == PressMode::Momentary);
	CHECK(state.lastSource == ccSource(30, 2));
	CHECK(state.messageCount == 2);

	pedal.send(cc(30, 127, 2000 * msec, 2));
	REQUIRE(pedal.fired.size() == 1);
	CHECK(pedal.fired[0].action == ControlAction::TrackMain);

	// Un Program Change appris devient un appui par message.
	learn.binding.action = ControlAction::TrackStop;
	REQUIRE(pedal.control.request(learn));
	pedal.control.processRequests();
	pedal.send(programChange(4, 3000 * msec));
	pedal.control.publish();
	state = pedal.control.snapshot();
	REQUIRE(state.numBindings == 2);
	CHECK(state.bindings[1].mode == PressMode::EveryMessage);

	pedal.fired.clear();
	pedal.send(programChange(4, 4000 * msec));
	REQUIRE(pedal.fired.size() == 1);
	CHECK(pedal.fired[0].action == ControlAction::TrackStop);
}

TEST_CASE("MIDI : effacer demande deux appuis rapproches")
{
	Pedal pedal;
	pedal.bind(binding(ccSource(24), Gesture::Short, ControlAction::TrackClear));

	pedal.send(cc(24, 127, 0));
	pedal.send(cc(24, 0, 100 * msec));
	CHECK(pedal.fired.empty());
	pedal.control.publish();
	CHECK(pedal.control.snapshot().clearArmed);

	pedal.send(cc(24, 127, 1000 * msec));
	REQUIRE(pedal.fired.size() == 1);
	CHECK(pedal.fired[0].action == ControlAction::TrackClear);

	// Second appui trop tard : il ne fait que réarmer.
	pedal.fired.clear();
	pedal.send(cc(24, 0, 1100 * msec));
	pedal.send(cc(24, 127, 5000 * msec));
	pedal.send(cc(24, 0, 5100 * msec));
	pedal.send(cc(24, 127, 7500 * msec));
	CHECK(pedal.fired.empty());
	pedal.wait(10000 * msec);
	pedal.control.publish();
	CHECK_FALSE(pedal.control.snapshot().clearArmed);
}

TEST_CASE("MIDI : retrait d'une affectation et changement de mode")
{
	Pedal pedal;
	pedal.bind(binding(ccSource(25), Gesture::Short, ControlAction::TrackMain));
	pedal.bind(binding(ccSource(25), Gesture::Long, ControlAction::TrackUndo));
	pedal.bind(binding(ccSource(25), Gesture::Short, ControlAction::TrackStop));	// remplace la première

	pedal.control.publish();
	auto state = pedal.control.snapshot();
	REQUIRE(state.numBindings == 2);
	CHECK(state.bindings[0].action == ControlAction::TrackStop);

	ControlRequest r;
	r.type = ControlRequest::Type::SetMode;
	r.index = 1;
	r.mode = PressMode::EveryMessage;
	REQUIRE(pedal.control.request(r));
	r.type = ControlRequest::Type::RemoveBinding;
	r.index = 0;
	REQUIRE(pedal.control.request(r));
	pedal.control.processRequests();
	pedal.control.publish();
	state = pedal.control.snapshot();
	REQUIRE(state.numBindings == 1);
	CHECK(state.bindings[0].action == ControlAction::TrackUndo);
	CHECK(state.bindings[0].mode == PressMode::EveryMessage);
}

// Rejeu d'une session au pédalier dans le moteur, message par message.
TEST_CASE("Moteur : le pedalier quantifie sur l'instant de l'appui")
{
	constexpr int blockSize = 128;
	constexpr SamplePos bar = 96000;

	Engine engine;
	EngineConfig config;
	config.sampleRate = sampleRate;
	config.maxBlockSize = blockSize;
	REQUIRE(engine.prepare(config));
	engine.setMetronomeEnabled(false);

	// Appui court + double appui sur la même pédale : l'appui court n'est connu
	// que 300 ms après le relâchement, bien au-delà des 80 ms de tolérance.
	for (const auto& b : { binding(ccSource(20), Gesture::Short, ControlAction::TrackMain),
			 binding(ccSource(20), Gesture::Double, ControlAction::TrackUndo) })
	{
		ControlRequest r;
		r.type = ControlRequest::Type::AddBinding;
		r.binding = b;
		REQUIRE(engine.midiControl().request(r));
	}

	// Appuis 20 ms après la mesure 0 puis après la mesure 1.
	std::multimap<std::int64_t, MidiEvent> midi;
	for (const SamplePos t : { SamplePos { 20 * msec }, bar + 20 * msec })
	{
		midi.insert({ t, cc(20, 127, t) });
		midi.insert({ t + 60 * msec, cc(20, 0, t + 60 * msec) });
	}

	std::vector<float> inL(blockSize), outL(blockSize), outR(blockSize), left;
	std::vector<MidiEvent> events;

	for (std::int64_t block = 0; block * blockSize < 3 * bar; ++block)
	{
		const std::int64_t start = block * blockSize;
		events.clear();

		for (auto it = midi.lower_bound(start); it != midi.end() && it->first < start + blockSize; ++it)
		{
			MidiEvent e = it->second;
			e.device = it->first - start;	// décalage dans le bloc
			events.push_back(e);
		}

		for (int i = 0; i < blockSize; ++i)
			inL[static_cast<std::size_t>(i)] = start + i == 1000 ? 1.0f : 0.0f;

		const float* in[1] = { inL.data() };
		float* out[2] = { outL.data(), outR.data() };
		engine.process(in, 1, out, 2, blockSize, events.data(), static_cast<int>(events.size()));
		left.insert(left.end(), outL.begin(), outL.end());

		if (start == bar / 2)
		{
			// Prise commencée rétroactivement à la mesure 0 (le son de 1000 est dedans).
			CHECK(engine.trackSnapshot(0).state == TrackState::Recording);
		}
	}

	const auto snapshot = engine.trackSnapshot(0);
	CHECK(snapshot.state == TrackState::Playing);
	CHECK(snapshot.loopLength == bar);
	CHECK(left[static_cast<std::size_t>(2 * bar + 1000)] == 1.0f);
}
