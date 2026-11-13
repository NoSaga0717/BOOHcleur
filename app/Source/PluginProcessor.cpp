#include "PluginProcessor.h"
#include "PluginEditor.h"

BoohcleurProcessor::BoohcleurProcessor()
	: AudioProcessor(BusesProperties()
		.withInput(juce::String::fromUTF8("Entrée"), juce::AudioChannelSet::stereo(), true)
		.withOutput("Sortie", juce::AudioChannelSet::stereo(), true))
{
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
	midi.clear();	// MIDI au jalon J3

	if (!prepared_.load())
	{
		buffer.clear();
		return;
	}

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

		engine_.process(in, numIn, out, numOut, count);
	}

	for (int c = numOut; c < buffer.getNumChannels(); ++c)
		buffer.clear(c, 0, total);
}

juce::AudioProcessorEditor* BoohcleurProcessor::createEditor()
{
	return new BoohcleurEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new BoohcleurProcessor();
}
