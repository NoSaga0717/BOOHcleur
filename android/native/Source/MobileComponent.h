#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "LoopRing.h"
#include "PluginProcessor.h"

// Interface tactile de test (téléphone) : une boucle, le gros bouton
// Rec / Play / Overdub, stop, undo / redo, effacer et quelques réglages.
// S'adapte au portrait et au paysage. Le canevas libre reste prévu en J5b
// (ADR 0001, D9) : ceci n'est qu'un écran de test.
class MobileComponent final : public juce::Component, private juce::Timer
{
public:
	MobileComponent(BoohcleurProcessor& processor, juce::AudioDeviceManager& deviceManager);
	~MobileComponent() override;

	// Appelé quand le périphérique audio est ouvert (ou a échoué).
	void audioOpened(bool micGranted, const juce::String& error);

	void paint(juce::Graphics& g) override;
	void resized() override;

	// Relit l'état du moteur (appelé par le timer ; public pour les captures).
	void refresh();

private:
	void timerCallback() override { refresh(); }
	void post(boohcleur::CommandType type, double value = 0.0);
	void confirmClear();
	juce::Rectangle<int> safeArea() const;

	BoohcleurProcessor& processor_;
	juce::AudioDeviceManager& deviceManager_;
	boohcleur::ui::LookAndFeel lookAndFeel_;
	juce::Image logo_;

	boohcleur::ui::LoopRing ring_;

	juce::TextButton mainButton_, stopButton_ { "STOP" }, undoButton_ { "UNDO" },
		redoButton_ { "REDO" }, clearButton_ { "CLEAR" }, transportButton_;

	juce::Label positionLabel_, statusLabel_, latencyCaption_, tempoCaption_;
	juce::Slider latencySlider_, tempoSlider_;
	juce::ComboBox quantizeBox_;
	juce::ToggleButton metronomeToggle_, monitorToggle_;

	juce::Rectangle<int> headerArea_, settingsArea_;
	int frame_ = 0;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MobileComponent)
};
