#include "MobileComponent.h"

#include "BinaryData.h"

using boohcleur::CommandType;
using boohcleur::TrackState;
namespace colours = boohcleur::ui::colours;
using boohcleur::ui::fr;

namespace
{
	// Latence réglable jusqu'à 300 ms : l'aller-retour micro / haut-parleur d'un
	// téléphone dépasse souvent largement celui d'une carte son.
	constexpr double maxLatencyMs = 300.0;

	void styleButton(juce::TextButton& button)
	{
		button.setColour(juce::TextButton::buttonColourId, colours::panel);
		button.setColour(juce::TextButton::buttonOnColourId, colours::accent);
		button.setColour(juce::TextButton::textColourOffId, colours::text);
		button.setColour(juce::TextButton::textColourOnId, colours::background);
		button.setWantsKeyboardFocus(false);
	}

	void styleCaption(juce::Label& label)
	{
		label.setColour(juce::Label::textColourId, colours::textDim);
		label.setFont(juce::Font(juce::FontOptions(14.0f)));
	}

	void styleSlider(juce::Slider& slider, double min, double max, double step, const juce::String& suffix)
	{
		slider.setSliderStyle(juce::Slider::LinearHorizontal);
		slider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 72, 24);
		slider.setRange(min, max, step);
		slider.setTextValueSuffix(suffix);
		slider.setColour(juce::Slider::thumbColourId, colours::accent);
		slider.setColour(juce::Slider::trackColourId, colours::accent.withAlpha(0.5f));
		slider.setColour(juce::Slider::backgroundColourId, colours::panelEdge);
		slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
		slider.setWantsKeyboardFocus(false);
	}
}

MobileComponent::MobileComponent(BoohcleurProcessor& processor, juce::AudioDeviceManager& deviceManager)
	: processor_(processor)
	, deviceManager_(deviceManager)
{
	setLookAndFeel(&lookAndFeel_);
	setOpaque(true);

	logo_ = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

	addAndMakeVisible(ring_);

	styleButton(mainButton_);
	mainButton_.onClick = [this] { post(CommandType::TrackMain); };
	addAndMakeVisible(mainButton_);

	for (auto* button : { &stopButton_, &undoButton_, &redoButton_, &clearButton_, &transportButton_ })
	{
		styleButton(*button);
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
	positionLabel_.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
	positionLabel_.setJustificationType(juce::Justification::centredRight);
	positionLabel_.setMinimumHorizontalScale(0.6f);
	addAndMakeVisible(positionLabel_);

	statusLabel_.setText(fr("Ouverture du micro…"), juce::dontSendNotification);
	styleCaption(statusLabel_);
	statusLabel_.setJustificationType(juce::Justification::centred);
	statusLabel_.setMinimumHorizontalScale(0.6f);
	addAndMakeVisible(statusLabel_);

	auto& engine = processor_.engine();

	latencyCaption_.setText("Latence", juce::dontSendNotification);
	tempoCaption_.setText("Tempo", juce::dontSendNotification);

	for (auto* caption : { &latencyCaption_, &tempoCaption_ })
	{
		styleCaption(*caption);
		addAndMakeVisible(*caption);
	}

	styleSlider(latencySlider_, 0.0, maxLatencyMs, 1.0, " ms");
	latencySlider_.setValue(processor_.latencyMilliseconds(), juce::dontSendNotification);
	latencySlider_.onValueChange = [this] { processor_.setLatencyMilliseconds(latencySlider_.getValue()); };
	addAndMakeVisible(latencySlider_);

	styleSlider(tempoSlider_, 40.0, 240.0, 1.0, " BPM");
	tempoSlider_.setValue(engine.transportSnapshot().bpm, juce::dontSendNotification);
	tempoSlider_.onValueChange = [this]
	{
		if (!tempoSlider_.isMouseButtonDown())
			post(CommandType::SetTempo, tempoSlider_.getValue());
	};
	tempoSlider_.onDragEnd = [this] { post(CommandType::SetTempo, tempoSlider_.getValue()); };
	addAndMakeVisible(tempoSlider_);

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

	// Sur un téléphone, l'écoute de l'entrée par le haut-parleur part en larsen :
	// elle reste coupée par défaut et n'a de sens qu'au casque.
	metronomeToggle_.setButtonText(fr("Métronome"));
	monitorToggle_.setButtonText(fr("Écoute (casque)"));
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

	setSize(800, 380);
	refresh();
	startTimerHz(30);
}

MobileComponent::~MobileComponent()
{
	stopTimer();
	setLookAndFeel(nullptr);
}

void MobileComponent::audioOpened(bool micGranted, const juce::String& error)
{
	auto* device = deviceManager_.getCurrentAudioDevice();

	if (device == nullptr)
	{
		statusLabel_.setText(fr("Audio indisponible : ") + (error.isNotEmpty() ? error : fr("aucun périphérique")),
			juce::dontSendNotification);
		return;
	}

	const double rate = device->getCurrentSampleRate();
	const int buffer = device->getCurrentBufferSizeSamples();
	const int latencySamples = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();

	// Point de départ : la latence annoncée par le système (souvent sous-estimée).
	// L'oreille tranche ensuite avec le curseur ; la calibration arrive en J7.
	if (rate > 0.0 && latencySamples > 0)
	{
		const double ms = juce::jlimit(0.0, maxLatencyMs, 1000.0 * latencySamples / rate);
		latencySlider_.setValue(std::round(ms), juce::sendNotificationSync);
	}

	juce::String text = device->getTypeName() + "  ·  " + juce::String(juce::roundToInt(rate / 1000.0)) + " kHz  ·  buffer "
		+ juce::String(buffer) + fr("  ·  latence annoncée ") + juce::String(juce::roundToInt(1000.0 * latencySamples / juce::jmax(1.0, rate))) + " ms";

	if (!micGranted)
		text = fr("Micro refusé : autorise-le dans les réglages Android, puis relance l'app.  ·  ") + text;
	else if (device->getActiveInputChannels().isZero())
		text = fr("Pas d'entrée micro ouverte.  ·  ") + text;

	statusLabel_.setText(text, juce::dontSendNotification);
}

void MobileComponent::post(CommandType type, double value)
{
	processor_.engine().post({ type, 0, value });
}

void MobileComponent::confirmClear()
{
	juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Effacer la boucle ?",
		fr("Toutes les couches de la boucle seront supprimées."), "Effacer", "Annuler", this,
		juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer(this)](int result)
		{
			if (safe != nullptr && result == 1)
				safe->post(CommandType::TrackClear);
		}));
}

void MobileComponent::refresh()
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

	// Couleur de l'action à venir (même code que le Standalone).
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
	positionLabel_.setText(juce::String(bbt.bar + 1) + "." + juce::String(bbt.beat + 1)
		+ "   " + juce::String(juce::roundToInt(transport.bpm)) + " BPM", juce::dontSendNotification);

	// Tempo modifiable seulement transport arrêté et boucle vide (ADR 0001, arbitrage 5).
	tempoSlider_.setEnabled(!transport.playing && track.state == TrackState::Empty && track.numLayers == 0);
}

juce::Rectangle<int> MobileComponent::safeArea() const
{
	auto area = getLocalBounds();

	// Encoches et barres système : l'activité JUCE dessine bord à bord.
	if (isShowing())
		if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()))
			area = display->safeAreaInsets.subtractedFrom(area);

	return area.reduced(10, 6);
}

void MobileComponent::paint(juce::Graphics& g)
{
	g.fillAll(colours::background);

	g.setColour(colours::panel);
	g.fillRect(headerArea_.withX(0).withWidth(getWidth()).withY(0).withBottom(headerArea_.getBottom() + 4));

	if (logo_.isValid())
		g.drawImageWithin(logo_, headerArea_.getX(), headerArea_.getY(), 96, headerArea_.getHeight(),
			juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);

	g.setColour(colours::panel);
	g.fillRoundedRectangle(settingsArea_.toFloat(), 10.0f);
}

void MobileComponent::resized()
{
	auto area = safeArea();
	const bool portrait = area.getHeight() > area.getWidth();

	headerArea_ = area.removeFromTop(44);
	{
		auto header = headerArea_;
		header.removeFromLeft(104);
		transportButton_.setBounds(header.removeFromRight(110).reduced(0, 4));
		header.removeFromRight(10);
		positionLabel_.setBounds(header);
	}

	area.removeFromTop(6);
	statusLabel_.setBounds(area.removeFromTop(20));
	area.removeFromTop(6);

	// Réglages : quantification et interrupteurs, puis latence et tempo.
	const int rowHeight = 40;
	settingsArea_ = area.removeFromBottom(portrait ? rowHeight * 4 + 16 : rowHeight * 2 + 12);
	{
		auto settings = settingsArea_.reduced(10, 6);

		auto layoutSlider = [](juce::Rectangle<int> r, juce::Label& caption, juce::Slider& slider)
		{
			caption.setBounds(r.removeFromLeft(70));
			slider.setBounds(r);
		};

		if (portrait)
		{
			auto row = settings.removeFromTop(rowHeight);
			quantizeBox_.setBounds(row.reduced(0, 4));
			row = settings.removeFromTop(rowHeight);
			metronomeToggle_.setBounds(row.removeFromLeft(row.getWidth() / 2));
			monitorToggle_.setBounds(row);
			layoutSlider(settings.removeFromTop(rowHeight), latencyCaption_, latencySlider_);
			layoutSlider(settings.removeFromTop(rowHeight), tempoCaption_, tempoSlider_);
		}
		else
		{
			auto row = settings.removeFromTop(rowHeight);
			quantizeBox_.setBounds(row.removeFromLeft(160).reduced(0, 4));
			row.removeFromLeft(16);
			metronomeToggle_.setBounds(row.removeFromLeft(row.getWidth() / 2));
			monitorToggle_.setBounds(row);

			row = settings.removeFromTop(rowHeight);
			const int half = row.getWidth() / 2;
			layoutSlider(row.removeFromLeft(half - 8), latencyCaption_, latencySlider_);
			row.removeFromLeft(16);
			layoutSlider(row, tempoCaption_, tempoSlider_);
		}
	}

	area.removeFromBottom(8);

	// Boucle et boutons de jeu : côte à côte en paysage, empilés en portrait.
	juce::Rectangle<int> controls;

	if (portrait)
	{
		const int buttons = 120 + 10 + 64;
		const int ringSize = juce::jmax(80, juce::jmin(area.getWidth(), area.getHeight() - buttons - 10));
		ring_.setBounds(area.removeFromTop(ringSize).withSizeKeepingCentre(ringSize, ringSize));
		area.removeFromTop(10);
		controls = area;
	}
	else
	{
		const int ringSize = juce::jmin(area.getHeight(), area.getWidth() / 3);
		ring_.setBounds(area.removeFromLeft(ringSize).withSizeKeepingCentre(ringSize, ringSize));
		area.removeFromLeft(16);
		controls = area;
	}

	auto row = controls.removeFromBottom(juce::jmin(64, controls.getHeight() / 3));
	controls.removeFromBottom(10);
	mainButton_.setBounds(controls);

	const int gap = 8;
	const int width = (row.getWidth() - 3 * gap) / 4;

	for (auto* button : { &stopButton_, &undoButton_, &redoButton_, &clearButton_ })
	{
		button->setBounds(row.removeFromLeft(width));
		row.removeFromLeft(gap);
	}
}
