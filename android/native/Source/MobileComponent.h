#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "LoopRing.h"
#include "LoopTile.h"
#include "PluginProcessor.h"

#include <array>
#include <memory>

// Interface tactile de test (téléphone) : les six boucles en grille (toucher
// un anneau = son bouton principal), les commandes de la boucle sélectionnée
// et un panneau de réglages repliable. S'adapte au portrait et au paysage.
// Le canevas libre reste prévu en J5b (ADR 0001, D9) : ceci n'est qu'un écran de test.
class MobileComponent final : public juce::Component, private juce::Timer
{
public:
	MobileComponent(BoohcleurProcessor& processor, juce::AudioDeviceManager& deviceManager);
	~MobileComponent() override;

	// Appelé quand le périphérique audio est ouvert (ou a échoué).
	void audioOpened(bool micGranted, const juce::String& error);

	void paint(juce::Graphics& g) override;
	void resized() override;
	bool keyPressed(const juce::KeyPress& key) override;

	// Relit l'état du moteur (appelé par le timer ; public pour les captures).
	void refresh();
	void setSettingsVisible(bool visible);

private:
	static constexpr int numTiles = 6;

	void timerCallback() override;
	void post(boohcleur::CommandType type, double value = 0.0);	// boucle sélectionnée
	void postTo(int track, boohcleur::CommandType type, double value = 0.0);
	void select(int track);
	void confirmClear();
	void updateStatus();
	void logState();
	juce::Rectangle<int> safeArea() const;

	BoohcleurProcessor& processor_;
	juce::AudioDeviceManager& deviceManager_;
	boohcleur::ui::LookAndFeel lookAndFeel_;
	juce::Image logo_;

	std::array<std::unique_ptr<boohcleur::ui::LoopTile>, numTiles> tiles_;
	int shownTiles_ = numTiles;

	juce::TextButton mainButton_, stopButton_ { "STOP" }, undoButton_ { "UNDO" },
		redoButton_ { "REDO" }, clearButton_ { "CLEAR" }, transportButton_, settingsButton_;

	juce::Label positionLabel_, statusLabel_, selectedLabel_;

	// Panneau de réglages (par-dessus les boucles).
	struct Panel final : juce::Component
	{
		void paint(juce::Graphics& g) override;
	};

	Panel settingsPanel_;
	juce::Label latencyCaption_, tempoCaption_, tracksCaption_, masterCaption_;
	juce::Slider latencySlider_, tempoSlider_, masterSlider_;
	juce::ComboBox quantizeBox_, tracksBox_;
	juce::ToggleButton metronomeToggle_, monitorToggle_;

	juce::Rectangle<int> headerArea_, bodyArea_;
	juce::String deviceText_;
	bool micGranted_ = true;
	bool audioOpened_ = false;
	std::uint64_t lastBlocks_ = 0;
	int frame_ = 0;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MobileComponent)
};
