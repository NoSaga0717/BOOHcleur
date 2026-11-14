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

void MobileComponent::Panel::paint(juce::Graphics& g)
{
	g.setColour(colours::background.withAlpha(0.97f));
	g.fillRect(getLocalBounds());
	g.setColour(colours::panel);
	g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 10.0f);
}

MobileComponent::MobileComponent(BoohcleurProcessor& processor, juce::AudioDeviceManager& deviceManager)
	: processor_(processor)
	, deviceManager_(deviceManager)
{
	setLookAndFeel(&lookAndFeel_);
	setOpaque(true);
	setWantsKeyboardFocus(true);	// clavier Bluetooth, et le test dans l'émulateur

	logo_ = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

	auto& engine = processor_.engine();

	for (int t = 0; t < numTiles; ++t)
	{
		auto tile = std::make_unique<boohcleur::ui::LoopTile>(t);
		tile->onTap = [this, t] { select(t); postTo(t, CommandType::TrackMain); };
		tile->onSelect = [this, t] { select(t); };
		tile->onVolume = [this, t](float gain) { processor_.engine().setTrackVolume(t, gain); };
		tile->onMute = [this, t](bool muted) { processor_.engine().setTrackMuted(t, muted); };
		addAndMakeVisible(*tile);
		tiles_[static_cast<std::size_t>(t)] = std::move(tile);
	}

	styleButton(mainButton_);
	mainButton_.onClick = [this] { post(CommandType::TrackMain); };
	addAndMakeVisible(mainButton_);

	for (auto* button : { &stopButton_, &undoButton_, &redoButton_, &clearButton_, &transportButton_, &settingsButton_ })
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
		postTo(0, processor_.engine().transportSnapshot().playing ? CommandType::TransportStop : CommandType::TransportStart);
	};
	settingsButton_.setButtonText(fr("Réglages"));
	settingsButton_.onClick = [this] { setSettingsVisible(!settingsPanel_.isVisible()); };

	positionLabel_.setColour(juce::Label::textColourId, colours::text);
	positionLabel_.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
	positionLabel_.setJustificationType(juce::Justification::centredRight);
	positionLabel_.setMinimumHorizontalScale(0.6f);
	addAndMakeVisible(positionLabel_);

	selectedLabel_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
	selectedLabel_.setColour(juce::Label::textColourId, colours::accent);
	addAndMakeVisible(selectedLabel_);

	statusLabel_.setText(fr("Ouverture du micro…"), juce::dontSendNotification);
	styleCaption(statusLabel_);
	statusLabel_.setJustificationType(juce::Justification::centred);
	statusLabel_.setMinimumHorizontalScale(0.5f);
	addAndMakeVisible(statusLabel_);

	// --- Réglages ---

	latencyCaption_.setText("Latence", juce::dontSendNotification);
	tempoCaption_.setText("Tempo", juce::dontSendNotification);
	tracksCaption_.setText("Boucles", juce::dontSendNotification);
	masterCaption_.setText("Volume", juce::dontSendNotification);

	for (auto* caption : { &latencyCaption_, &tempoCaption_, &tracksCaption_, &masterCaption_ })
	{
		styleCaption(*caption);
		settingsPanel_.addAndMakeVisible(*caption);
	}

	styleSlider(latencySlider_, 0.0, maxLatencyMs, 1.0, " ms");
	latencySlider_.setValue(processor_.latencyMilliseconds(), juce::dontSendNotification);
	latencySlider_.onValueChange = [this] { processor_.setLatencyMilliseconds(latencySlider_.getValue()); };
	settingsPanel_.addAndMakeVisible(latencySlider_);

	styleSlider(tempoSlider_, 40.0, 240.0, 1.0, " BPM");
	tempoSlider_.setValue(engine.transportSnapshot().bpm, juce::dontSendNotification);
	tempoSlider_.onValueChange = [this]
	{
		if (!tempoSlider_.isMouseButtonDown())
			postTo(0, CommandType::SetTempo, tempoSlider_.getValue());
	};
	tempoSlider_.onDragEnd = [this] { postTo(0, CommandType::SetTempo, tempoSlider_.getValue()); };
	settingsPanel_.addAndMakeVisible(tempoSlider_);

	styleSlider(masterSlider_, 0.0, 1.5, 0.01, "");
	masterSlider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
	masterSlider_.setSkewFactorFromMidPoint(0.5);
	masterSlider_.setValue(engine.masterVolume(), juce::dontSendNotification);
	masterSlider_.onValueChange = [this] { processor_.engine().setMasterVolume(static_cast<float>(masterSlider_.getValue())); };
	settingsPanel_.addAndMakeVisible(masterSlider_);

	quantizeBox_.addItem("Quantification : mesure", 1);
	quantizeBox_.addItem("Quantification : temps", 2);
	quantizeBox_.addItem(fr("Libre (immédiat)"), 3);
	quantizeBox_.setSelectedId(engine.quantizeDivision() == boohcleur::GridDivision::Beat ? 2
		: engine.quantizeDivision() == boohcleur::GridDivision::Immediate ? 3 : 1, juce::dontSendNotification);
	quantizeBox_.onChange = [this]
	{
		const int id = quantizeBox_.getSelectedId();
		processor_.engine().setQuantizeDivision(id == 2 ? boohcleur::GridDivision::Beat
			: id == 3 ? boohcleur::GridDivision::Immediate : boohcleur::GridDivision::Bar);
	};
	quantizeBox_.setWantsKeyboardFocus(false);
	settingsPanel_.addAndMakeVisible(quantizeBox_);

	for (int n = 1; n <= numTiles; ++n)
		tracksBox_.addItem(juce::String(n), n);

	tracksBox_.setSelectedId(engine.activeTracks(), juce::dontSendNotification);
	tracksBox_.onChange = [this]
	{
		processor_.engine().setActiveTracks(tracksBox_.getSelectedId());
		resized();
	};
	tracksBox_.setWantsKeyboardFocus(false);
	settingsPanel_.addAndMakeVisible(tracksBox_);

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
		settingsPanel_.addAndMakeVisible(*toggle);
	}

	addChildComponent(settingsPanel_);

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
	micGranted_ = micGranted;
	audioOpened_ = true;
	auto* device = deviceManager_.getCurrentAudioDevice();

	if (device == nullptr)
	{
		deviceText_ = fr("Audio indisponible : ") + (error.isNotEmpty() ? error : fr("aucun périphérique"));
		juce::Logger::writeToLog("BOOHcleur: " + deviceText_);
		updateStatus();
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

	deviceText_ = device->getTypeName() + "  ·  " + juce::String(juce::roundToInt(rate / 1000.0)) + " kHz  ·  buffer "
		+ juce::String(buffer) + fr("  ·  entrées ") + juce::String(device->getActiveInputChannels().countNumberOfSetBits());

	juce::Logger::writeToLog("BOOHcleur: audio ouvert : " + deviceText_ + (micGranted ? "" : " (micro refusé)"));
	updateStatus();
}

// Ligne d'état : ce qui empêche de jouer d'abord (micro, moteur, audio
// arrêté), sinon le périphérique. C'est elle qu'il faut lire quand REC ne
// fait rien.
void MobileComponent::updateStatus()
{
	using Error = BoohcleurProcessor::PrepareError;
	const auto blocks = processor_.blocksProcessed();
	const bool running = blocks != lastBlocks_;
	lastBlocks_ = blocks;

	juce::String text;
	auto colour = colours::textDim;

	if (!audioOpened_)
		text = fr("Ouverture du micro…");
	else if (processor_.prepareError() == Error::OutOfMemory)
		text = fr("Moteur arrêté : pas assez de mémoire pour les boucles."), colour = colours::forState(TrackState::Recording);
	else if (processor_.prepareError() == Error::InvalidConfig)
		text = fr("Moteur arrêté : réglages audio refusés.  ·  ") + deviceText_, colour = colours::forState(TrackState::Recording);
	else if (!processor_.isPrepared())
		text = fr("Moteur pas encore prêt (audio jamais démarré).  ·  ") + deviceText_, colour = colours::forState(TrackState::Armed);
	else if (!running)
		text = fr("Audio à l'arrêt : le moteur ne reçoit plus rien.  ·  ") + deviceText_, colour = colours::forState(TrackState::Armed);
	else if (!micGranted_)
		text = fr("Micro refusé : autorise-le dans les réglages Android, puis relance l'app."), colour = colours::forState(TrackState::Armed);
	else
		text = deviceText_;

	statusLabel_.setText(text, juce::dontSendNotification);
	statusLabel_.setColour(juce::Label::textColourId, colour);
}

// Journal Android (logcat) toutes les 2 s : sert au test dans l'émulateur et
// au diagnostic à distance.
void MobileComponent::logState()
{
	auto& engine = processor_.engine();
	juce::String line = "BOOHcleur: etat prepared=" + juce::String(processor_.isPrepared() ? 1 : 0)
		+ " blocs=" + juce::String(static_cast<juce::int64>(processor_.blocksProcessed()))
		+ " transport=" + juce::String(engine.transportSnapshot().playing ? 1 : 0);

	for (int t = 0; t < engine.numTracks(); ++t)
		line << " b" << (t + 1) << "=" << boohcleur::ui::stateLabel(engine.trackSnapshot(t).state);

	// Où toucher : le centre du bouton principal, en pixels de l'écran.
	const auto scale = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
		? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale : 1.0;
	const auto centre = mainButton_.getScreenBounds().getCentre().toDouble() * scale;
	line << " principal=" << juce::roundToInt(centre.x) << "," << juce::roundToInt(centre.y);

	juce::Logger::writeToLog(line);
}

void MobileComponent::timerCallback()
{
	refresh();

	if (frame_ % 60 == 0)
	{
		updateStatus();
		logState();
	}
}

void MobileComponent::post(CommandType type, double value)
{
	postTo(processor_.engine().selectedTrack(), type, value);
}

void MobileComponent::postTo(int track, CommandType type, double value)
{
	processor_.engine().post({ type, track, value });
}

void MobileComponent::select(int track)
{
	processor_.engine().setSelectedTrack(track);
	refresh();
}

bool MobileComponent::keyPressed(const juce::KeyPress& key)
{
	const auto c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

	if (key == juce::KeyPress::spaceKey)	{ post(CommandType::TrackMain); return true; }
	if (c == 's')							{ post(CommandType::TrackStop); return true; }
	if (c >= '1' && c <= '0' + numTiles)	{ select(static_cast<int>(c - '1')); return true; }

	return false;
}

void MobileComponent::confirmClear()
{
	const int track = processor_.engine().selectedTrack();

	juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
		"Effacer la boucle " + juce::String(track + 1) + " ?",
		fr("Toutes les couches de la boucle seront supprimées."), "Effacer", "Annuler", this,
		juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer(this), track](int result)
		{
			if (safe != nullptr && result == 1)
				safe->postTo(track, CommandType::TrackClear);
		}));
}

void MobileComponent::setSettingsVisible(bool visible)
{
	settingsPanel_.setVisible(visible);
	settingsButton_.setToggleState(visible, juce::dontSendNotification);

	if (visible)
		settingsPanel_.toFront(false);
}

void MobileComponent::refresh()
{
	auto& engine = processor_.engine();
	const int selected = engine.selectedTrack();
	const auto track = engine.trackSnapshot(selected);
	const auto transport = engine.transportSnapshot();

	++frame_;

	if (engine.activeTracks() != shownTiles_)
	{
		tracksBox_.setSelectedId(engine.activeTracks(), juce::dontSendNotification);
		resized();
	}

	boohcleur::ui::LoopRing::Model model;
	model.transport = transport;
	model.sampleRate = processor_.getSampleRate() > 0.0 ? processor_.getSampleRate() : 48000.0;
	model.blink = (frame_ / 8) % 2 == 0;

	for (int t = 0; t < numTiles; ++t)
	{
		model.read(engine, t);
		tiles_[static_cast<std::size_t>(t)]->setModel(model, t == selected, engine.trackVolume(t), engine.trackMuted(t));
	}

	selectedLabel_.setText("Boucle " + juce::String(selected + 1) + fr("  ·  ") + boohcleur::ui::stateLabel(track.state),
		juce::dontSendNotification);

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

	metronomeToggle_.setToggleState(engine.metronomeEnabled(), juce::dontSendNotification);

	// Tempo modifiable seulement transport arrêté et boucles vides (ADR 0001, arbitrage 5).
	bool allEmpty = true;

	for (int t = 0; t < engine.numTracks(); ++t)
		allEmpty = allEmpty && engine.trackSnapshot(t).state == TrackState::Empty && engine.trackSnapshot(t).numLayers == 0;

	tempoSlider_.setEnabled(!transport.playing && allEmpty);
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
}

void MobileComponent::resized()
{
	auto area = safeArea();
	const bool portrait = area.getHeight() > area.getWidth();

	headerArea_ = area.removeFromTop(44);
	{
		auto header = headerArea_;
		header.removeFromLeft(104);
		transportButton_.setBounds(header.removeFromRight(100).reduced(0, 4));
		header.removeFromRight(8);
		settingsButton_.setBounds(header.removeFromRight(100).reduced(0, 4));
		header.removeFromRight(8);
		positionLabel_.setBounds(header);
	}

	area.removeFromTop(4);
	statusLabel_.setBounds(area.removeFromTop(20));
	area.removeFromTop(4);
	bodyArea_ = area;

	// Commandes de la boucle sélectionnée : à droite en paysage, en bas en portrait.
	juce::Rectangle<int> controls;

	if (portrait)
		controls = area.removeFromBottom(juce::jmin(260, area.getHeight() / 2));
	else
		controls = area.removeFromRight(juce::jmin(320, area.getWidth() * 2 / 5));

	if (portrait)
		area.removeFromBottom(8);
	else
		area.removeFromRight(8);

	selectedLabel_.setBounds(controls.removeFromTop(24));
	auto row = controls.removeFromBottom(juce::jmin(56, controls.getHeight() / 3));
	controls.removeFromBottom(8);
	mainButton_.setBounds(controls);

	const int gap = 6;
	const int width = (row.getWidth() - 3 * gap) / 4;

	for (auto* button : { &stopButton_, &undoButton_, &redoButton_, &clearButton_ })
	{
		button->setBounds(row.removeFromLeft(width));
		row.removeFromLeft(gap);
	}

	// Grille des boucles.
	shownTiles_ = processor_.engine().activeTracks();
	const int columns = shownTiles_ <= 3 ? shownTiles_ : 3;
	const int rows = (shownTiles_ + columns - 1) / columns;
	const int tileWidth = area.getWidth() / columns;
	const int tileHeight = area.getHeight() / rows;

	for (int t = 0; t < numTiles; ++t)
	{
		auto& tile = *tiles_[static_cast<std::size_t>(t)];
		tile.setVisible(t < shownTiles_);

		if (t < shownTiles_)
			tile.setBounds(area.getX() + (t % columns) * tileWidth, area.getY() + (t / columns) * tileHeight, tileWidth, tileHeight);
	}

	// Réglages, par-dessus les boucles.
	settingsPanel_.setBounds(bodyArea_);
	{
		auto settings = settingsPanel_.getLocalBounds().reduced(14, 10);
		const int rowHeight = 40;

		auto layoutSlider = [](juce::Rectangle<int> r, juce::Label& caption, juce::Slider& slider)
		{
			caption.setBounds(r.removeFromLeft(72));
			slider.setBounds(r);
		};

		auto line = settings.removeFromTop(rowHeight);
		quantizeBox_.setBounds(line.removeFromLeft(juce::jmin(240, line.getWidth() / 2)).reduced(0, 4));
		line.removeFromLeft(12);
		tracksCaption_.setBounds(line.removeFromLeft(64));
		tracksBox_.setBounds(line.removeFromLeft(70).reduced(0, 4));

		line = settings.removeFromTop(rowHeight);
		metronomeToggle_.setBounds(line.removeFromLeft(line.getWidth() / 2));
		monitorToggle_.setBounds(line);

		layoutSlider(settings.removeFromTop(rowHeight), latencyCaption_, latencySlider_);
		layoutSlider(settings.removeFromTop(rowHeight), tempoCaption_, tempoSlider_);
		layoutSlider(settings.removeFromTop(rowHeight), masterCaption_, masterSlider_);
	}
}
