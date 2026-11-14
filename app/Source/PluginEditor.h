#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LoopRing.h"
#include "LoopTile.h"
#include "MidiPanel.h"
#include "PluginProcessor.h"

// Interface J4 : grille de boucles avec mixeur, commandes de la boucle
// sélectionnée, réglages utiles pour jouer et pédalier MIDI.
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
	void setMidiPanelVisible(bool visible) { showMidiPanel(visible); }

private:
	void timerCallback() override { refresh(); }
	void post(boohcleur::CommandType type, double value = 0.0);	// boucle sélectionnée
	void postTo(int track, boohcleur::CommandType type, double value = 0.0);
	void select(int track);
	void layoutTiles(juce::Rectangle<int> area);
	void confirmClear();
	void showMidiPanel(bool show);
	void enableNewMidiInputs();

	BoohcleurProcessor& processor_;
	boohcleur::ui::LookAndFeel lookAndFeel_;
	juce::Image logo_;

	static constexpr int numTiles = 6;	// EngineConfig::numTracks par défaut
	std::array<std::unique_ptr<boohcleur::ui::LoopTile>, numTiles> tiles_;
	juce::Rectangle<int> tilesArea_;
	int shownTiles_ = numTiles;

	juce::TextButton mainButton_, stopButton_ { "STOP" }, undoButton_ { "UNDO" },
		redoButton_ { "REDO" }, clearButton_ { "CLEAR" }, transportButton_, midiButton_ { "MIDI" };

	boohcleur::ui::MidiPanel midiPanel_;
	juce::StringArray knownMidiInputs_;

	juce::Label positionLabel_, hintLabel_, selectedLabel_, memoryLabel_;
	juce::Label tempoCaption_, quantizeCaption_, latencyCaption_, toleranceCaption_, inputCaption_,
		tracksCaption_, masterCaption_;
	juce::Slider tempoSlider_, latencySlider_, toleranceSlider_, masterSlider_;
	juce::ComboBox quantizeBox_, inputBox_, tracksBox_;
	juce::ToggleButton metronomeToggle_, monitorToggle_;

	int frame_ = 0;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoohcleurEditor)
};
