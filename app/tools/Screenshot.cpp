// Outil de développement : rend l'interface dans un fichier PNG, sans carte son
// ni fenêtre. Le moteur est piloté hors ligne pour montrer un état réaliste.
//
// Usage : boohcleur-screenshot <fichier.png> [scénario]
// Scénarios : loops (défaut), overdub, armed, recording, empty, midi

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cmath>
#include <vector>

namespace
{
	constexpr double sampleRate = 48000.0;
	constexpr int blockSize = 128;

	void run(boohcleur::Engine& engine, std::int64_t& sample, std::int64_t until)
	{
		std::vector<float> inL(blockSize), inR(blockSize), outL(blockSize), outR(blockSize);

		for (; sample < until; sample += blockSize)
		{
			for (int i = 0; i < blockSize; ++i)
				inL[i] = inR[i] = 0.2f * static_cast<float>(std::sin(0.05 * static_cast<double>(sample + i)));

			const float* in[2] = { inL.data(), inR.data() };
			float* out[2] = { outL.data(), outR.data() };
			engine.process(in, 2, out, 2, blockSize);
		}
	}
}

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::fprintf(stderr, "usage : %s <fichier.png> [loops|overdub|armed|recording|empty|midi]\n", argv[0]);
		return 1;
	}

	juce::ScopedJuceInitialiser_GUI gui;

	const juce::String scenario = argc > 2 ? juce::String(argv[2]) : juce::String("loops");

	BoohcleurProcessor processor;
	processor.setRateAndBufferSizeDetails(sampleRate, blockSize);
	processor.prepareToPlay(sampleRate, blockSize);
	processor.setLatencyMilliseconds(6.5);

	auto& engine = processor.engine();
	const std::int64_t bar = 96000;	// 120 BPM, 4/4
	std::int64_t sample = 0;
	using boohcleur::CommandType;

	if (scenario == "loops")
	{
		// Une session en cours : 4 boucles de longueurs différentes, une en prise.
		auto post = [&engine](CommandType type, int track) { engine.post({ type, track, 0.0 }); };
		post(CommandType::TrackMain, 0);
		run(engine, sample, bar * 4);
		post(CommandType::TrackMain, 0);					// boucle 1 : 4 mesures
		post(CommandType::TrackMain, 1);
		run(engine, sample, bar * 5 + 1000);
		post(CommandType::TrackMain, 0);					// overdub sur la boucle 1
		run(engine, sample, bar * 6);
		post(CommandType::TrackMain, 1);					// boucle 2 : 2 mesures
		post(CommandType::TrackMain, 0);
		post(CommandType::TrackMain, 2);
		run(engine, sample, bar * 8);
		post(CommandType::TrackMain, 2);					// boucle 3 : 1 mesure
		run(engine, sample, bar * 9);
		post(CommandType::TrackStop, 2);
		post(CommandType::TrackMain, 3);					// boucle 4 : prise en cours
		run(engine, sample, bar * 11 + 40000);
		post(CommandType::TrackMain, 1);					// overdub en cours sur la boucle 2
		run(engine, sample, bar * 11 + 70000);
		engine.setTrackVolume(1, 0.7f);
		engine.setTrackMuted(2, true);
		engine.setSelectedTrack(1);
	}
	else if (scenario == "armed")
	{
		run(engine, sample, bar * 2 + 30000);
		engine.post({ CommandType::TrackMain, 0, 0.0 });
		run(engine, sample, bar * 2 + 40000);
	}
	else if (scenario == "recording")
	{
		engine.post({ CommandType::TrackMain, 0, 0.0 });
		run(engine, sample, bar * 2 + 60000);
	}
	else if (scenario == "overdub")
	{
		engine.post({ CommandType::TrackMain, 0, 0.0 });
		run(engine, sample, bar * 4);
		engine.post({ CommandType::TrackMain, 0, 0.0 });	// fin de prise : 4 mesures
		run(engine, sample, bar * 5);
		engine.post({ CommandType::TrackMain, 0, 0.0 });	// overdub 1
		run(engine, sample, bar * 6);
		engine.post({ CommandType::TrackMain, 0, 0.0 });
		run(engine, sample, bar * 7);
		engine.post({ CommandType::TrackMain, 0, 0.0 });	// overdub 2 en cours
		run(engine, sample, bar * 10 + 50000);
	}
	else if (scenario == "midi")
	{
		// Pédalier type : 4 pédales en CC, une à bascule, apprentissage en cours.
		using namespace boohcleur;
		auto add = [&engine](int cc, Gesture gesture, ControlAction action, PressMode mode = PressMode::Momentary)
		{
			ControlRequest r;
			r.type = ControlRequest::Type::AddBinding;
			r.binding.source = { MidiSourceKind::ControlChange, 1, static_cast<std::uint8_t>(cc) };
			r.binding.gesture = gesture;
			r.binding.action = action;
			r.binding.mode = mode;
			engine.midiControl().request(r);
		};

		add(20, Gesture::Short, ControlAction::TrackMain);
		add(20, Gesture::Long, ControlAction::TrackUndo);
		add(21, Gesture::Short, ControlAction::TrackStop);
		add(21, Gesture::Double, ControlAction::TrackClear);
		add(22, Gesture::Short, ControlAction::TrackRedo);
		add(23, Gesture::Short, ControlAction::MetronomeToggle, PressMode::EveryMessage);

		std::vector<float> zero(blockSize), outL(blockSize), outR(blockSize);
		const float* in[1] = { zero.data() };
		float* out[2] = { outL.data(), outR.data() };
		const MidiEvent events[] = { { 0xB0, 22, 127, 0 }, { 0xB0, 22, 0, 60 } };
		engine.process(in, 1, out, 2, blockSize);
		sample += blockSize;
		run(engine, sample, bar);
		engine.process(in, 1, out, 2, blockSize, events, 2);
		sample += blockSize;

		ControlRequest learn;
		learn.type = ControlRequest::Type::StartLearn;
		learn.binding.action = ControlAction::TransportToggle;
		engine.midiControl().request(learn);
		run(engine, sample, bar + 20000);
	}
	else
	{
		run(engine, sample, bar + 20000);
	}

	BoohcleurEditor editor(processor);
	editor.setMidiPanelVisible(scenario == "midi");
	editor.refresh();

	const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 2.0f);
	juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
	file.deleteFile();
	juce::FileOutputStream stream(file);
	juce::PNGImageFormat png;

	if (!stream.openedOk() || !png.writeImageToStream(image, stream))
	{
		std::fprintf(stderr, "écriture impossible : %s\n", argv[1]);
		return 1;
	}

	std::printf("%s (%s)\n", file.getFullPathName().toRawUTF8(), scenario.toRawUTF8());
	return 0;
}
