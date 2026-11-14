#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "boohcleur/core/Engine.h"

#include <vector>

// Enveloppe JUCE du moteur. Ne contient aucune logique de looper : elle
// convertit les buffers JUCE pour boohcleur::Engine (ADR 0001, D2).
class BoohcleurProcessor final : public juce::AudioProcessor
{
public:
	BoohcleurProcessor();
	~BoohcleurProcessor() override = default;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override {}
	bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
	void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
	using AudioProcessor::processBlock;

	juce::AudioProcessorEditor* createEditor() override;
	bool hasEditor() const override { return true; }

	const juce::String getName() const override { return "BOOHcleur"; }
	bool acceptsMidi() const override { return true; }
	bool producesMidi() const override { return false; }
	bool isMidiEffect() const override { return false; }
	double getTailLengthSeconds() const override { return 0.0; }

	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override {}
	const juce::String getProgramName(int) override { return {}; }
	void changeProgramName(int, const juce::String&) override {}

	void getStateInformation(juce::MemoryBlock&) override {}
	void setStateInformation(const void*, int) override {}

	boohcleur::Engine& engine() noexcept { return engine_; }
	bool isPrepared() const noexcept { return prepared_.load(); }

	// Décalage de latence en millisecondes (réglé à la main en J2, calibré en J7).
	void setLatencyMilliseconds(double ms);
	double latencyMilliseconds() const noexcept { return latencyMs_.load(); }

private:
	boohcleur::Engine engine_;
	std::vector<std::vector<float>> inputCopy_;
	std::atomic<bool> prepared_ { false };
	std::atomic<double> latencyMs_ { 0.0 };
	std::atomic<double> sampleRate_ { 48000.0 };
	int maxBlock_ = 0;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoohcleurProcessor)
};
