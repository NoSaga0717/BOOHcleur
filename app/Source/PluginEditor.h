#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LoopRing.h"
#include "PluginProcessor.h"

// Interface J2 : une boucle, ses commandes et les réglages utiles pour jouer.
// Mise en page fixe pour l'instant ; le canevas libre arrive en J5b (ADR 0001, D9).
class BoohcleurEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
	explicit BoohcleurEditor(BoohcleurProcessor& processor);
	~BoohcleurEditor() override;

	void paint(juce::Graphics& g) override;
	void resized() override;
	bool keyPressed(const juce::KeyPress& key) override;

	// Relit l'état du moteur (appelé par le timer ; public pour les captures d'écran).
	void refresh();

private:
	void timerCallback() override { refresh(); }
	void post(boohcleur::CommandType type, double value = 0.0);
	void confirmClear();

	BoohcleurProcessor& processor_;
	boohcleur::ui::LookAndFeel lookAndFeel_;
	juce::Image logo_;

	boohcleur::ui::LoopRing ring_;

	juce::TextButton mainButton_, stopButton_ { "STOP" }, undoButton_ { "UNDO" },
		redoButton_ { "REDO" }, clearButton_ { "CLEAR" }, transportButton_;

	juce::Label positionLabel_, hintLabel_;
	juce::Label tempoCaption_, quantizeCaption_, latencyCaption_, toleranceCaption_;
	juce::Slider tempoSlider_, latencySlider_, toleranceSlider_;
	juce::ComboBox quantizeBox_;
	juce::ToggleButton metronomeToggle_, monitorToggle_;

	int frame_ = 0;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoohcleurEditor)
};
