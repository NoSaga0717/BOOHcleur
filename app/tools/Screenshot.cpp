// Outil de développement : rend l'interface dans un fichier PNG, sans carte son
// ni fenêtre. Le moteur est piloté hors ligne pour montrer un état réaliste.
//
// Usage : boohcleur-screenshot <fichier.png> [scénario]
// Scénarios : overdub (défaut), armed, recording, empty

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
		std::fprintf(stderr, "usage : %s <fichier.png> [overdub|armed|recording|empty]\n", argv[0]);
		return 1;
	}

	juce::ScopedJuceInitialiser_GUI gui;

	const juce::String scenario = argc > 2 ? juce::String(argv[2]) : juce::String("overdub");

	BoohcleurProcessor processor;
	processor.setRateAndBufferSizeDetails(sampleRate, blockSize);
	processor.prepareToPlay(sampleRate, blockSize);
	processor.setLatencyMilliseconds(6.5);

	auto& engine = processor.engine();
	const std::int64_t bar = 96000;	// 120 BPM, 4/4
	std::int64_t sample = 0;
	using boohcleur::CommandType;

	if (scenario == "armed")
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
	else
	{
		run(engine, sample, bar + 20000);
	}

	BoohcleurEditor editor(processor);
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
