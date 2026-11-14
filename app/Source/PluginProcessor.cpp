#include "PluginProcessor.h"
#include "PluginEditor.h"

BoohcleurProcessor::BoohcleurProcessor()
	: AudioProcessor(BusesProperties()
		.withInput(juce::String::fromUTF8("Entrée"), juce::AudioChannelSet::stereo(), true)
		.withOutput("Sortie", juce::AudioChannelSet::stereo(), true))
{
	midiEvents_.reserve(1024);	// aucun agrandissement sur le thread audio
}

bool BoohcleurProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
	const auto in = layouts.getMainInputChannelSet();
	const auto out = layouts.getMainOutputChannelSet();

	return out == juce::AudioChannelSet::stereo()
		&& (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void BoohcleurProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	prepared_.store(false);

	boohcleur::EngineConfig config;
	config.sampleRate = sampleRate;
	config.maxBlockSize = juce::jmax(samplesPerBlock, 32);
	config.numChannels = 2;
	config.bpm = savedBpm_.load();

	maxBlock_ = config.maxBlockSize;
	inputCopy_.assign(2, std::vector<float>(static_cast<size_t>(maxBlock_), 0.0f));
	sampleRate_.store(sampleRate);

	if (engine_.prepare(config))
	{
		setLatencyMilliseconds(latencyMs_.load());
		prepared_.store(true);
	}
}

void BoohcleurProcessor::setLatencyMilliseconds(double ms)
{
	latencyMs_.store(ms);
	engine_.setLatencySamples(static_cast<int>(std::lround(ms * 0.001 * sampleRate_.load())));
}

void BoohcleurProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	juce::ScopedNoDenormals noDenormals;
	if (!prepared_.load())
	{
		buffer.clear();
		midi.clear();
		return;
	}

	// Messages MIDI courts du bloc (le pédalier), dans l'ordre et horodatés.
	midiEvents_.clear();

	for (const auto metadata : midi)
	{
		if (metadata.numBytes < 2 || metadata.numBytes > 3 || midiEvents_.size() == midiEvents_.capacity())
			continue;

		const auto* bytes = metadata.data;
		boohcleur::MidiEvent event;
		event.status = bytes[0];
		event.data1 = bytes[1];
		event.data2 = metadata.numBytes == 3 ? bytes[2] : 0;
		event.device = metadata.samplePosition;
		midiEvents_.push_back(event);
	}

	midi.clear();
	std::size_t nextEvent = 0;

	const int numIn = juce::jmin(getTotalNumInputChannels(), 2);
	const int numOut = juce::jmin(buffer.getNumChannels(), 2);
	const int total = buffer.getNumSamples();

	// Le buffer JUCE sert à la fois d'entrée et de sortie : on copie l'entrée
	// avant que le moteur n'écrive la sortie. Un hôte peut envoyer un bloc plus
	// grand qu'annoncé : on le découpe.
	for (int start = 0; start < total; start += maxBlock_)
	{
		const int count = juce::jmin(maxBlock_, total - start);
		const float* in[2] = { nullptr, nullptr };
		float* out[2] = { nullptr, nullptr };

		for (int c = 0; c < numIn; ++c)
		{
			std::copy_n(buffer.getReadPointer(c, start), count, inputCopy_[static_cast<size_t>(c)].data());
			in[c] = inputCopy_[static_cast<size_t>(c)].data();
		}

		for (int c = 0; c < numOut; ++c)
			out[c] = buffer.getWritePointer(c, start);

		// Messages de ce morceau de bloc, décalés au début du morceau.
		const std::size_t firstEvent = nextEvent;

		while (nextEvent < midiEvents_.size() && midiEvents_[nextEvent].device < start + count)
			midiEvents_[nextEvent++].device -= start;

		engine_.process(in, numIn, out, numOut, count,
			midiEvents_.data() + firstEvent, static_cast<int>(nextEvent - firstEvent));
	}

	for (int c = numOut; c < buffer.getNumChannels(); ++c)
		buffer.clear(c, 0, total);
}

namespace
{
	const char* actionId(boohcleur::ControlAction action)
	{
		using boohcleur::ControlAction;

		switch (action)
		{
		case ControlAction::TrackMain: return "main";
		case ControlAction::TrackStop: return "stop";
		case ControlAction::TrackPlay: return "play";
		case ControlAction::TrackUndo: return "undo";
		case ControlAction::TrackRedo: return "redo";
		case ControlAction::TrackClear: return "clear";
		case ControlAction::TransportToggle: return "transport";
		case ControlAction::MetronomeToggle: return "metronome";
		case ControlAction::None:
		case ControlAction::Count: break;
		}

		return "";
	}

	boohcleur::ControlAction actionFromId(const juce::String& id)
	{
		for (int a = 1; a < static_cast<int>(boohcleur::ControlAction::Count); ++a)
			if (id == actionId(static_cast<boohcleur::ControlAction>(a)))
				return static_cast<boohcleur::ControlAction>(a);

		return boohcleur::ControlAction::None;
	}
}

void BoohcleurProcessor::getStateInformation(juce::MemoryBlock& data)
{
	juce::XmlElement root("BOOHcleur");
	root.setAttribute("version", 1);

	auto& engine = engine_;
	auto* settings = root.createNewChildElement("Settings");
	settings->setAttribute("bpm", prepared_.load() ? engine.transportSnapshot().bpm : savedBpm_.load());
	settings->setAttribute("latencyMs", latencyMs_.load());
	settings->setAttribute("toleranceMs", static_cast<double>(engine.lateToleranceMilliseconds()));
	settings->setAttribute("quantize", static_cast<int>(engine.quantizeDivision()));
	settings->setAttribute("metronome", engine.metronomeEnabled());
	settings->setAttribute("monitor", engine.monitorEnabled());
	settings->setAttribute("input", static_cast<int>(engine.inputMode()));

	const auto control = engine.midiControl().snapshot();
	auto* midi = root.createNewChildElement("Midi");
	midi->setAttribute("longPressMs", control.settings.longPressMs);
	midi->setAttribute("doubleTapMs", control.settings.doubleTapMs);
	midi->setAttribute("debounceMs", control.settings.debounceMs);

	for (int i = 0; i < control.numBindings; ++i)
	{
		const auto& b = control.bindings[static_cast<std::size_t>(i)];
		auto* e = midi->createNewChildElement("Binding");
		e->setAttribute("kind", static_cast<int>(b.source.kind));
		e->setAttribute("channel", b.source.channel);
		e->setAttribute("number", b.source.number);
		e->setAttribute("mode", static_cast<int>(b.mode));
		e->setAttribute("gesture", static_cast<int>(b.gesture));
		e->setAttribute("action", actionId(b.action));
		e->setAttribute("track", b.track);
	}

	copyXmlToBinary(root, data);
}

void BoohcleurProcessor::setStateInformation(const void* data, int size)
{
	const auto root = getXmlFromBinary(data, size);

	if (root == nullptr || !root->hasTagName("BOOHcleur"))
		return;

	if (const auto* s = root->getChildByName("Settings"))
	{
		savedBpm_.store(juce::jlimit(40.0, 240.0, s->getDoubleAttribute("bpm", 120.0)));
		setLatencyMilliseconds(juce::jlimit(0.0, 50.0, s->getDoubleAttribute("latencyMs", 0.0)));
		engine_.setLateToleranceMilliseconds(static_cast<float>(juce::jlimit(0.0, 200.0, s->getDoubleAttribute("toleranceMs", 80.0))));
		engine_.setQuantizeDivision(static_cast<boohcleur::GridDivision>(
			juce::jlimit(0, 2, s->getIntAttribute("quantize", static_cast<int>(boohcleur::GridDivision::Bar)))));
		engine_.setMetronomeEnabled(s->getBoolAttribute("metronome", true));
		engine_.setMonitorEnabled(s->getBoolAttribute("monitor", false));
		engine_.setInputMode(static_cast<boohcleur::InputMode>(juce::jlimit(0, 2, s->getIntAttribute("input", 0))));

		if (prepared_.load())
			engine_.post({ boohcleur::CommandType::SetTempo, 0, savedBpm_.load() });	// pris si transport arrêté
	}

	if (const auto* m = root->getChildByName("Midi"))
	{
		auto& control = engine_.midiControl();
		boohcleur::ControlRequest request;
		request.type = boohcleur::ControlRequest::Type::ClearAll;
		control.request(request);

		request.type = boohcleur::ControlRequest::Type::SetSettings;
		request.settings.longPressMs = juce::jlimit(150.0, 3000.0, m->getDoubleAttribute("longPressMs", 500.0));
		request.settings.doubleTapMs = juce::jlimit(100.0, 1000.0, m->getDoubleAttribute("doubleTapMs", 300.0));
		request.settings.debounceMs = juce::jlimit(0.0, 100.0, m->getDoubleAttribute("debounceMs", 30.0));
		control.request(request);

		for (const auto* e : m->getChildWithTagNameIterator("Binding"))
		{
			boohcleur::Binding b;
			b.source.kind = static_cast<boohcleur::MidiSourceKind>(juce::jlimit(0, 3, e->getIntAttribute("kind")));
			b.source.channel = static_cast<std::uint8_t>(juce::jlimit(1, 16, e->getIntAttribute("channel", 1)));
			b.source.number = static_cast<std::uint8_t>(juce::jlimit(0, 127, e->getIntAttribute("number")));
			b.mode = static_cast<boohcleur::PressMode>(juce::jlimit(0, 1, e->getIntAttribute("mode")));
			b.gesture = static_cast<boohcleur::Gesture>(juce::jlimit(0, 2, e->getIntAttribute("gesture")));
			b.action = actionFromId(e->getStringAttribute("action"));
			b.track = static_cast<std::uint8_t>(juce::jlimit(0, 15, e->getIntAttribute("track")));

			request.type = boohcleur::ControlRequest::Type::AddBinding;
			request.binding = b;
			control.request(request);
		}
	}
}

juce::AudioProcessorEditor* BoohcleurProcessor::createEditor()
{
	return new BoohcleurEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new BoohcleurProcessor();
}
