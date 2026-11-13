#include "PluginEditor.h"

#include "BinaryData.h"

using boohcleur::CommandType;
using boohcleur::TrackState;
namespace colours = boohcleur::ui::colours;
using boohcleur::ui::fr;

namespace
{
	void styleButton(juce::TextButton& button, juce::Colour colour)
	{
		button.setColour(juce::TextButton::buttonColourId, colours::panel);
		button.setColour(juce::TextButton::buttonOnColourId, colour);
		button.setColour(juce::TextButton::textColourOffId, colours::text);
		button.setColour(juce::TextButton::textColourOnId, colours::background);
		button.setWantsKeyboardFocus(false);
	}

	void styleCaption(juce::Label& label)
	{
		label.setColour(juce::Label::textColourId, colours::textDim);
		label.setFont(juce::Font(juce::FontOptions(13.0f)));
	}

	void styleSlider(juce::Slider& slider, double min, double max, double step, const juce::String& suffix)
	{
		slider.setSliderStyle(juce::Slider::LinearHorizontal);
		slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
		slider.setRange(min, max, step);
		slider.setTextValueSuffix(suffix);
		slider.setColour(juce::Slider::thumbColourId, colours::accent);
		slider.setColour(juce::Slider::trackColourId, colours::accent.withAlpha(0.5f));
		slider.setColour(juce::Slider::backgroundColourId, colours::panelEdge);
		slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
		slider.setWantsKeyboardFocus(false);
	}
}

BoohcleurEditor::BoohcleurEditor(BoohcleurProcessor& processor)
	: AudioProcessorEditor(processor)
	, processor_(processor)
{
	setLookAndFeel(&lookAndFeel_);

	tempoCaption_.setText("Tempo", juce::dontSendNotification);
	quantizeCaption_.setText("Quantification", juce::dontSendNotification);
	latencyCaption_.setText("Latence (ms)", juce::dontSendNotification);
	toleranceCaption_.setText(fr("Tolérance (ms)"), juce::dontSendNotification);
	metronomeToggle_.setButtonText(fr("Métronome"));
	monitorToggle_.setButtonText(fr("Écoute de l'entrée"));

	logo_ = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

	addAndMakeVisible(ring_);

	styleButton(mainButton_, colours::accent);
	mainButton_.onClick = [this] { post(CommandType::TrackMain); };
	addAndMakeVisible(mainButton_);

	for (auto* button : { &stopButton_, &undoButton_, &redoButton_, &clearButton_, &transportButton_ })
	{
		styleButton(*button, colours::accent);
		addAndMakeVisible(*button);
	}

	stopButton_.onClick = [this] { post(CommandType::TrackStop); };
	undoButton_.onClick = [this] { post(CommandType::TrackUndo); };
	redoButton_.onClick = [this] { post(CommandType::TrackRedo); };
	clearButton_.onClick = [this] { confirmClear(); };
	transportButton_.onClick = [this]
	{
		post(processor_.engine().transportSnapshot().playing ? CommandType::TransportStop : CommandType::TransportStart);
	};

	positionLabel_.setColour(juce::Label::textColourId, colours::text);
	positionLabel_.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
	positionLabel_.setJustificationType(juce::Justification::centredRight);
	addAndMakeVisible(positionLabel_);

	hintLabel_.setText(fr("Clavier : espace = bouton principal  ·  S = stop  ·  Z / Y = undo / redo  ·  C = effacer"),
		juce::dontSendNotification);
	styleCaption(hintLabel_);
	hintLabel_.setJustificationType(juce::Justification::centred);
	addAndMakeVisible(hintLabel_);

	for (auto* caption : { &tempoCaption_, &quantizeCaption_, &latencyCaption_, &toleranceCaption_ })
	{
		styleCaption(*caption);
		addAndMakeVisible(*caption);
	}

	auto& engine = processor_.engine();

	styleSlider(tempoSlider_, 40.0, 240.0, 0.1, " BPM");
	tempoSlider_.setValue(engine.transportSnapshot().bpm, juce::dontSendNotification);
	tempoSlider_.onDragEnd = [this] { post(CommandType::SetTempo, tempoSlider_.getValue()); };
	tempoSlider_.onValueChange = [this]
	{
		if (!tempoSlider_.isMouseButtonDown())
			post(CommandType::SetTempo, tempoSlider_.getValue());
	};
	addAndMakeVisible(tempoSlider_);

	styleSlider(latencySlider_, 0.0, 50.0, 0.1, "");
	latencySlider_.setValue(processor_.latencyMilliseconds(), juce::dontSendNotification);
	latencySlider_.onValueChange = [this] { processor_.setLatencyMilliseconds(latencySlider_.getValue()); };
	addAndMakeVisible(latencySlider_);

	styleSlider(toleranceSlider_, 0.0, 200.0, 1.0, "");
	toleranceSlider_.setValue(engine.lateToleranceMilliseconds(), juce::dontSendNotification);
	toleranceSlider_.onValueChange = [this]
	{
		processor_.engine().setLateToleranceMilliseconds(static_cast<float>(toleranceSlider_.getValue()));
	};
	addAndMakeVisible(toleranceSlider_);

	quantizeBox_.addItem("Mesure", 1);
	quantizeBox_.addItem("Temps", 2);
	quantizeBox_.addItem(fr("Immédiat"), 3);
	quantizeBox_.setSelectedId(engine.quantizeDivision() == boohcleur::GridDivision::Beat ? 2
		: engine.quantizeDivision() == boohcleur::GridDivision::Immediate ? 3 : 1, juce::dontSendNotification);
	quantizeBox_.onChange = [this]
	{
		const int id = quantizeBox_.getSelectedId();
		processor_.engine().setQuantizeDivision(id == 2 ? boohcleur::GridDivision::Beat
			: id == 3 ? boohcleur::GridDivision::Immediate : boohcleur::GridDivision::Bar);
	};
	quantizeBox_.setWantsKeyboardFocus(false);
	addAndMakeVisible(quantizeBox_);

	metronomeToggle_.setToggleState(engine.metronomeEnabled(), juce::dontSendNotification);
	metronomeToggle_.onClick = [this] { processor_.engine().setMetronomeEnabled(metronomeToggle_.getToggleState()); };
	monitorToggle_.setToggleState(engine.monitorEnabled(), juce::dontSendNotification);
	monitorToggle_.onClick = [this] { processor_.engine().setMonitorEnabled(monitorToggle_.getToggleState()); };

	for (auto* toggle : { &metronomeToggle_, &monitorToggle_ })
	{
		toggle->setColour(juce::ToggleButton::textColourId, colours::text);
		toggle->setWantsKeyboardFocus(false);
		addAndMakeVisible(*toggle);
	}

	setWantsKeyboardFocus(true);
	setSize(780, 540);
	refresh();
	startTimerHz(30);
}

BoohcleurEditor::~BoohcleurEditor()
{
	stopTimer();
	setLookAndFeel(nullptr);
}

void BoohcleurEditor::post(CommandType type, double value)
{
	processor_.engine().post({ type, 0, value });
}

void BoohcleurEditor::confirmClear()
{
	juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Effacer la boucle ?",
		fr("Toutes les couches de la boucle seront supprimées."), "Effacer", "Annuler", this,
		juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer(this)](int result)
		{
			if (safe != nullptr && result == 1)
				safe->post(CommandType::TrackClear);
		}));
}

bool BoohcleurEditor::keyPressed(const juce::KeyPress& key)
{
	const auto c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

	if (key == juce::KeyPress::spaceKey)	{ post(CommandType::TrackMain); return true; }
	if (c == 's')							{ post(CommandType::TrackStop); return true; }
	if (c == 'z')							{ post(CommandType::TrackUndo); return true; }
	if (c == 'y')							{ post(CommandType::TrackRedo); return true; }
	if (c == 'c')							{ confirmClear(); return true; }

	return false;
}

void BoohcleurEditor::refresh()
{
	auto& engine = processor_.engine();
	const auto track = engine.trackSnapshot(0);
	const auto transport = engine.transportSnapshot();

	++frame_;

	boohcleur::ui::LoopRing::Model model;
	model.track = track;
	model.transport = transport;
	model.sampleRate = processor_.getSampleRate() > 0.0 ? processor_.getSampleRate() : 48000.0;
	model.blink = (frame_ / 8) % 2 == 0;
	ring_.setModel(model);

	juce::String main;

	switch (track.state)
	{
	case TrackState::Empty:			main = "REC"; break;
	case TrackState::Armed:			main = fr("ARMÉ…"); break;
	case TrackState::Recording:		main = "PLAY"; break;
	case TrackState::Playing:		main = "OVERDUB"; break;
	case TrackState::Overdubbing:	main = "PLAY"; break;
	case TrackState::Stopped:		main = "PLAY"; break;
	}

	mainButton_.setButtonText(main);
	mainButton_.setColour(juce::TextButton::buttonColourId, colours::forState(
		track.state == TrackState::Empty ? TrackState::Recording
		: track.state == TrackState::Playing ? TrackState::Overdubbing
		: track.state == TrackState::Recording || track.state == TrackState::Overdubbing || track.state == TrackState::Stopped
			? TrackState::Playing : track.state).withAlpha(0.85f));
	mainButton_.setColour(juce::TextButton::textColourOffId, colours::background);

	undoButton_.setEnabled(track.numActiveLayers > 0);
	redoButton_.setEnabled(track.numActiveLayers < track.numLayers);
	clearButton_.setEnabled(track.state != TrackState::Empty);

	transportButton_.setButtonText(transport.playing ? fr("Arrêter") : fr("Démarrer"));

	const auto bbt = boohcleur::toBarBeatTick(transport.tick, transport.signature);
	positionLabel_.setText("Mesure " + juce::String(bbt.bar + 1) + fr("  ·  Temps ") + juce::String(bbt.beat + 1)
		+ "     " + juce::String(transport.bpm, 1) + " BPM", juce::dontSendNotification);

	// Tempo modifiable seulement transport arrêté et boucle vide (ADR 0001, arbitrage 5).
	tempoSlider_.setEnabled(!transport.playing && track.state == TrackState::Empty && track.numLayers == 0);
}

void BoohcleurEditor::paint(juce::Graphics& g)
{
	g.fillAll(colours::background);

	auto header = getLocalBounds().removeFromTop(80);
	g.setColour(colours::panel);
	g.fillRect(header);
	g.setColour(colours::panelEdge);
	g.drawHorizontalLine(header.getBottom(), 0.0f, static_cast<float>(getWidth()));

	if (logo_.isValid())
		g.drawImageWithin(logo_, 16, 8, 140, 64, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);

	auto settings = getLocalBounds().removeFromBottom(150).reduced(16, 12);
	g.setColour(colours::panel);
	g.fillRoundedRectangle(settings.toFloat(), 10.0f);
}

void BoohcleurEditor::resized()
{
	auto area = getLocalBounds();

	auto header = area.removeFromTop(80).reduced(16, 18);
	transportButton_.setBounds(header.removeFromRight(130));
	header.removeFromRight(16);
	positionLabel_.setBounds(header.removeFromRight(360));

	auto settings = area.removeFromBottom(150).reduced(16, 12).reduced(16, 12);
	hintLabel_.setBounds(area.removeFromBottom(24));

	auto main = area.reduced(16, 12);
	ring_.setBounds(main.removeFromLeft(main.getHeight()).reduced(8));
	main.removeFromLeft(24);

	auto controls = main.withSizeKeepingCentre(main.getWidth(), 200);
	mainButton_.setBounds(controls.removeFromTop(110));
	controls.removeFromTop(14);

	auto row = controls.removeFromTop(48);
	const int width = (row.getWidth() - 3 * 10) / 4;

	for (auto* button : { &stopButton_, &undoButton_, &redoButton_, &clearButton_ })
	{
		button->setBounds(row.removeFromLeft(width));
		row.removeFromLeft(10);
	}

	auto column = [&settings](int width) { return settings.removeFromLeft(width).reduced(6, 0); };

	auto left = column(settings.getWidth() / 3);
	tempoCaption_.setBounds(left.removeFromTop(20));
	tempoSlider_.setBounds(left.removeFromTop(28));
	left.removeFromTop(8);
	quantizeCaption_.setBounds(left.removeFromTop(20));
	quantizeBox_.setBounds(left.removeFromTop(28).withWidth(150));

	auto middle = column(settings.getWidth() / 2);
	latencyCaption_.setBounds(middle.removeFromTop(20));
	latencySlider_.setBounds(middle.removeFromTop(28));
	middle.removeFromTop(8);
	toleranceCaption_.setBounds(middle.removeFromTop(20));
	toleranceSlider_.setBounds(middle.removeFromTop(28));

	auto right = column(settings.getWidth());
	right.removeFromTop(20);
	metronomeToggle_.setBounds(right.removeFromTop(28));
	right.removeFromTop(28);
	monitorToggle_.setBounds(right.removeFromTop(28));
}
