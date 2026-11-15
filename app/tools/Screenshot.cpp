// Outil de développement : rend l'interface dans un fichier PNG, sans carte son
// ni fenêtre. Le moteur est piloté hors ligne pour montrer un état réaliste.
//
// Usage : boohcleur-screenshot <fichier.png> [scénario]
// Scénarios : loops (défaut), overdub, armed, recording, empty, midi,
// session (les boucles de « loops » enregistrées puis rouvertes dans un
// second processeur, par le même chemin que l'application)

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
			// Guitare simulée : une note pincée par croche, d'intensité variable,
			// qui décroît (pour que la forme d'onde ait du relief).
			for (int i = 0; i < blockSize; ++i)
			{
				const std::int64_t n = sample + i;
				const std::int64_t eighth = 12000;
				const std::int64_t note = n / eighth;
				const double t = static_cast<double>(n % eighth) / sampleRate;
				const double accent = 0.25 + 0.6 * std::abs(std::sin(static_cast<double>(note) * 1.7));
				const double pitch = 110.0 * (1.0 + static_cast<double>(note % 5) * 0.25);
				inL[i] = inR[i] = static_cast<float>(accent * std::exp(-t * 9.0) * std::sin(2.0 * 3.14159265 * pitch * t));
			}

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
		std::fprintf(stderr, "usage : %s <fichier.png> [loops|overdub|armed|recording|empty|midi|session]\n", argv[0]);
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

	if (scenario == "loops" || scenario == "session")
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

	// Aller-retour par le disque : enregistrer, puis rouvrir ailleurs.
	std::unique_ptr<BoohcleurProcessor> reopened;

	if (scenario == "session")
	{
		const auto wait = [](SessionManager& sessions, auto start)
		{
			bool finished = false;
			SessionManager::Outcome outcome;
			start([&](const SessionManager::Outcome& o) { outcome = o; finished = true; });

			while (!finished)
				juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

			std::printf("%s : %s\n", outcome.ok ? "ok" : "ÉCHEC", outcome.message.toRawUTF8());
			juce::ignoreUnused(sessions);
			return outcome.ok;
		};

		const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Capture BOOHcleur");

		if (!wait(processor.sessions(), [&](auto done) { processor.sessions().save(folder, done); }))
			return 1;

		reopened = std::make_unique<BoohcleurProcessor>();
		reopened->setRateAndBufferSizeDetails(sampleRate, blockSize);
		reopened->prepareToPlay(sampleRate, blockSize);
		auto& sessions = reopened->sessions();

		if (!wait(sessions, [&](auto done) { sessions.open(folder.withFileExtension("boohcleur"), done); }))
			return 1;

		// Transport relancé : les boucles repartent ensemble.
		std::int64_t other = 0;
		reopened->engine().post({ CommandType::TransportStart, 0, 0.0 });
		run(reopened->engine(), other, bar * 2 + 30000);
		folder.withFileExtension("boohcleur").deleteRecursively();
	}

	BoohcleurEditor editor(reopened != nullptr ? *reopened : processor);
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
