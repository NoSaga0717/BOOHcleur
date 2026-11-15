#include "PluginEditor.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "BinaryData.h"

#if JucePlugin_Build_Standalone
	#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

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
	, midiPanel_(processor.engine().midiControl(), numTiles)
{
	setLookAndFeel(&lookAndFeel_);

	tempoCaption_.setText("Tempo", juce::dontSendNotification);
	quantizeCaption_.setText("Quantification", juce::dontSendNotification);
	latencyCaption_.setText("Latence (ms)", juce::dontSendNotification);
	toleranceCaption_.setText(fr("Tolérance (ms)"), juce::dontSendNotification);
	inputCaption_.setText(fr("Entrée enregistrée"), juce::dontSendNotification);
	metronomeToggle_.setButtonText(fr("Métronome"));
	monitorToggle_.setButtonText(fr("Écoute de l'entrée"));

	logo_ = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

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

	selectedLabel_.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
	selectedLabel_.setColour(juce::Label::textColourId, colours::accent);
	selectedLabel_.setJustificationType(juce::Justification::centredLeft);
	addAndMakeVisible(selectedLabel_);

	styleCaption(memoryLabel_);
	memoryLabel_.setJustificationType(juce::Justification::centredLeft);
	addAndMakeVisible(memoryLabel_);

	styleButton(mainButton_, colours::accent);
	mainButton_.onClick = [this] { post(CommandType::TrackMain); };
	addAndMakeVisible(mainButton_);

	for (auto* button : { &stopButton_, &undoButton_, &redoButton_, &clearButton_, &transportButton_, &midiButton_ })
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
		postTo(0, processor_.engine().transportSnapshot().playing ? CommandType::TransportStop : CommandType::TransportStart);
	};

	midiButton_.onClick = [this] { showMidiPanel(!midiPanel_.isVisible()); };
	midiButton_.setTooltip(fr("Pédalier MIDI : affectations et apprentissage"));

	midiPanel_.onClose = [this] { showMidiPanel(false); };

	styleButton(sessionButton_, colours::accent);
	sessionButton_.onClick = [this] { showSessionMenu(); };
	sessionButton_.setTooltip(fr("Enregistrer ou ouvrir une session (boucles, couches, réglages, pédalier)"));
	addAndMakeVisible(sessionButton_);

	sessionLabel_.setColour(juce::Label::textColourId, colours::text);
	sessionLabel_.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
	sessionLabel_.setJustificationType(juce::Justification::centredLeft);
	addAndMakeVisible(sessionLabel_);

#if JucePlugin_Build_Standalone
	if (processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
	{
		midiPanel_.onOpenDeviceSettings = []
		{
			if (auto* holder = juce::StandalonePluginHolder::getInstance())
				holder->showAudioSettingsDialog();
		};
	}
#endif

	addChildComponent(midiPanel_);

	positionLabel_.setColour(juce::Label::textColourId, colours::text);
	positionLabel_.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
	positionLabel_.setJustificationType(juce::Justification::centredRight);
	addAndMakeVisible(positionLabel_);

	styleCaption(hintLabel_);
	hintLabel_.setJustificationType(juce::Justification::centred);
	addAndMakeVisible(hintLabel_);

	tracksCaption_.setText("Boucles", juce::dontSendNotification);
	masterCaption_.setText(fr("Volume général"), juce::dontSendNotification);

	for (auto* caption : { &tempoCaption_, &quantizeCaption_, &latencyCaption_, &toleranceCaption_, &inputCaption_,
			 &tracksCaption_, &masterCaption_ })
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

	inputBox_.addItem(fr("Mono · entrée 1"), 1);
	inputBox_.addItem(fr("Mono · entrée 2"), 2);
	inputBox_.addItem(fr("Stéréo · entrées 1 et 2"), 3);
	inputBox_.setSelectedId(static_cast<int>(engine.inputMode()) + 1, juce::dontSendNotification);
	inputBox_.onChange = [this]
	{
		processor_.engine().setInputMode(static_cast<boohcleur::InputMode>(inputBox_.getSelectedId() - 1));
	};
	inputBox_.setWantsKeyboardFocus(false);
	addAndMakeVisible(inputBox_);

	for (int n = 1; n <= numTiles; ++n)
		tracksBox_.addItem(juce::String(n), n);

	tracksBox_.setSelectedId(engine.activeTracks(), juce::dontSendNotification);
	tracksBox_.onChange = [this]
	{
		processor_.engine().setActiveTracks(tracksBox_.getSelectedId());
		resized();
	};
	tracksBox_.setWantsKeyboardFocus(false);
	tracksBox_.setTooltip(fr("Nombre de boucles affichées et pilotables (les autres gardent leur contenu, muettes)"));
	addAndMakeVisible(tracksBox_);

	styleSlider(masterSlider_, 0.0, 1.5, 0.01, "");
	masterSlider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
	masterSlider_.setSkewFactorFromMidPoint(0.5);
	masterSlider_.setDoubleClickReturnValue(true, 1.0);
	masterSlider_.setValue(engine.masterVolume(), juce::dontSendNotification);
	masterSlider_.onValueChange = [this] { processor_.engine().setMasterVolume(static_cast<float>(masterSlider_.getValue())); };
	addAndMakeVisible(masterSlider_);

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
	setSize(1040, 720);
	enableNewMidiInputs();
	refresh();
	startTimerHz(30);

	// Application autonome : sauvegarde automatique, et proposition de
	// récupérer le travail d'une exécution qui s'est mal terminée.
	if (juce::JUCEApplicationBase::isStandaloneApp())
	{
		processor_.sessions().enableAutosave();

		if (processor_.sessions().hasRecovery())
			juce::Timer::callAfterDelay(800, [safe = juce::Component::SafePointer<BoohcleurEditor>(this)]
			{
				if (safe != nullptr)
					safe->offerRecovery();
			});
	}
}

BoohcleurEditor::~BoohcleurEditor()
{
	stopTimer();
	setLookAndFeel(nullptr);
}

void BoohcleurEditor::post(CommandType type, double value)
{
	postTo(processor_.engine().selectedTrack(), type, value);
}

void BoohcleurEditor::postTo(int track, CommandType type, double value)
{
	processor_.engine().post({ type, track, value });
}

void BoohcleurEditor::select(int track)
{
	processor_.engine().setSelectedTrack(track);
	refresh();
}

void BoohcleurEditor::showMidiPanel(bool show)
{
	midiPanel_.setVisible(show);
	midiButton_.setToggleState(show, juce::dontSendNotification);

	if (show)
	{
		midiPanel_.toFront(false);
		resized();
	}
	else
	{
		boohcleur::ControlRequest cancel;
		cancel.type = boohcleur::ControlRequest::Type::CancelLearn;
		processor_.engine().midiControl().request(cancel);
	}
}

// Standalone : un pédalier branché doit marcher sans passer par les réglages.
// Chaque nouvelle entrée MIDI est activée une fois ; si on la désactive ensuite
// dans les réglages, elle le reste.
void BoohcleurEditor::enableNewMidiInputs()
{
#if JucePlugin_Build_Standalone
	if (processor_.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
		return;

	auto* holder = juce::StandalonePluginHolder::getInstance();

	if (holder == nullptr)
		return;

	for (const auto& device : juce::MidiInput::getAvailableDevices())
	{
		if (knownMidiInputs_.contains(device.identifier))
			continue;

		knownMidiInputs_.add(device.identifier);
		holder->deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
	}
#endif
}

void BoohcleurEditor::confirmClear()
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

bool BoohcleurEditor::keyPressed(const juce::KeyPress& key)
{
	const auto c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

	// Raccourcis de session (Cmd sur Mac, Ctrl ailleurs) avant les touches simples.
	if (key.getModifiers().isCommandDown())
	{
		const auto k = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode()));

		if (k == 's')	{ saveSession(key.getModifiers().isShiftDown()); return true; }
		if (k == 'o')	{ openSession(); return true; }
		if (k == 'n')	{ newSession(); return true; }
		return false;
	}

	if (key == juce::KeyPress::spaceKey)	{ post(CommandType::TrackMain); return true; }
	if (c == 's')							{ post(CommandType::TrackStop); return true; }
	if (c == 'z')							{ post(CommandType::TrackUndo); return true; }
	if (c == 'y')							{ post(CommandType::TrackRedo); return true; }
	if (c == 'c')							{ confirmClear(); return true; }

	if (c >= '1' && c <= '0' + numTiles)	{ select(static_cast<int>(c - '1')); return true; }

	if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
	{
		const int count = processor_.engine().activeTracks();
		const int step = key == juce::KeyPress::rightKey ? 1 : count - 1;
		select((processor_.engine().selectedTrack() + step) % count);
		return true;
	}

	return false;
}

void BoohcleurEditor::refresh()
{
	auto& engine = processor_.engine();
	const int selected = engine.selectedTrack();
	const auto track = engine.trackSnapshot(selected);
	const auto transport = engine.transportSnapshot();

	++frame_;

	if (frame_ % 60 == 0)
		enableNewMidiInputs();	// pédalier branché en cours de route

	const auto control = engine.midiControl().snapshot();

	if (midiPanel_.isVisible())
		midiPanel_.refresh(control, (frame_ / 8) % 2 == 0);

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

	const auto memory = engine.memorySnapshot();

	if (memory.totalBytes > 0)
	{
		const int percent = static_cast<int>(std::lround(100.0 * static_cast<double>(memory.usedBytes) / static_cast<double>(memory.totalBytes)));
		const int minutes = static_cast<int>(memory.secondsLeft / 60.0);
		memoryLabel_.setText(memory.secondsLeft <= 0.0
				? fr("Mémoire pleine : effacez une boucle")
				: fr("Mémoire ") + juce::String(percent) + fr(" %  ·  reste ") + juce::String(minutes) + " min "
					+ juce::String(static_cast<int>(memory.secondsLeft) % 60).paddedLeft('0', 2) + " s",
			juce::dontSendNotification);
		memoryLabel_.setColour(juce::Label::textColourId,
			memory.secondsLeft < 60.0 ? colours::forState(TrackState::Recording) : colours::textDim);
	}

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

	auto& sessions = processor_.sessions();
	sessionLabel_.setText(sessions.currentName() + (sessions.hasUnsavedChanges() ? fr("  •") : juce::String()),
		juce::dontSendNotification);
	sessionLabel_.setTooltip(sessions.currentFile() == juce::File {} ? fr("Session pas encore enregistrée")
		: sessions.currentFile().getFullPathName());

	const auto bbt = boohcleur::toBarBeatTick(transport.tick, transport.signature);
	positionLabel_.setText("Mesure " + juce::String(bbt.bar + 1) + fr("  ·  Temps ") + juce::String(bbt.beat + 1)
		+ "     " + juce::String(transport.bpm, 1) + " BPM", juce::dontSendNotification);

	// Garde-fou du pédalier : le premier appui sur « effacer » arme pendant 2 s.
	if (control.clearArmed)
	{
		hintLabel_.setText(fr("Appuyez encore sur la pédale « effacer » pour confirmer"), juce::dontSendNotification);
		hintLabel_.setColour(juce::Label::textColourId, colours::forState(TrackState::Recording));
	}
	else if (juce::Time::getMillisecondCounter() < statusUntil_ || processor_.sessions().isBusy())
	{
		hintLabel_.setText(processor_.sessions().isBusy() && !statusError_ ? fr("Session : opération en cours…") : status_,
			juce::dontSendNotification);
		hintLabel_.setColour(juce::Label::textColourId, statusError_ ? colours::forState(TrackState::Recording) : colours::accent);
	}
	else
	{
		hintLabel_.setText(fr("Clic sur une boucle = son bouton principal  ·  1 à 6 ou ← → = choisir  ·  espace = principal  ·  "
			"S = stop  ·  Z / Y = undo / redo  ·  C = effacer"), juce::dontSendNotification);
		hintLabel_.setColour(juce::Label::textColourId, colours::textDim);
	}

	for (int n = 1; n <= numTiles; ++n)
		tracksBox_.addItem(juce::String(n), n);

	tracksBox_.setSelectedId(engine.activeTracks(), juce::dontSendNotification);
	tracksBox_.onChange = [this]
	{
		processor_.engine().setActiveTracks(tracksBox_.getSelectedId());
		resized();
	};
	tracksBox_.setWantsKeyboardFocus(false);
	tracksBox_.setTooltip(fr("Nombre de boucles affichées et pilotables (les autres gardent leur contenu, muettes)"));
	addAndMakeVisible(tracksBox_);

	styleSlider(masterSlider_, 0.0, 1.5, 0.01, "");
	masterSlider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
	masterSlider_.setSkewFactorFromMidPoint(0.5);
	masterSlider_.setDoubleClickReturnValue(true, 1.0);
	masterSlider_.setValue(engine.masterVolume(), juce::dontSendNotification);
	masterSlider_.onValueChange = [this] { processor_.engine().setMasterVolume(static_cast<float>(masterSlider_.getValue())); };
	addAndMakeVisible(masterSlider_);

	metronomeToggle_.setToggleState(engine.metronomeEnabled(), juce::dontSendNotification);

	// Tempo modifiable seulement transport arrêté et boucles vides (ADR 0001, arbitrage 5).
	bool allEmpty = true;

	for (int t = 0; t < engine.numTracks(); ++t)
		allEmpty = allEmpty && engine.trackSnapshot(t).state == TrackState::Empty && engine.trackSnapshot(t).numLayers == 0;

	tempoSlider_.setEnabled(!transport.playing && allEmpty);
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

void BoohcleurEditor::layoutTiles(juce::Rectangle<int> area)
{
	shownTiles_ = processor_.engine().activeTracks();
	const int columns = shownTiles_ <= 3 ? shownTiles_ : 3;
	const int rows = (shownTiles_ + columns - 1) / columns;
	const int width = area.getWidth() / columns;
	const int height = juce::jmin(area.getHeight() / rows, width + 30);

	for (int t = 0; t < numTiles; ++t)
	{
		auto& tile = *tiles_[static_cast<std::size_t>(t)];
		tile.setVisible(t < shownTiles_);

		if (t < shownTiles_)
			tile.setBounds(area.getX() + (t % columns) * width, area.getY() + (t / columns) * height, width, height);
	}
}

void BoohcleurEditor::resized()
{
	auto area = getLocalBounds();

	midiPanel_.setBounds(area.withTrimmedTop(80));

	auto header = area.removeFromTop(80).reduced(16, 18);
	transportButton_.setBounds(header.removeFromRight(130));
	header.removeFromRight(10);
	midiButton_.setBounds(header.removeFromRight(80));
	header.removeFromRight(10);
	sessionButton_.setBounds(header.removeFromRight(100));
	header.removeFromRight(16);
	positionLabel_.setBounds(header.removeFromRight(340));
	header.removeFromLeft(150);	// logo
	sessionLabel_.setBounds(header);

	auto settings = area.removeFromBottom(150).reduced(16, 12).reduced(16, 12);
	hintLabel_.setBounds(area.removeFromBottom(24));

	auto main = area.reduced(12, 10);

	// Commandes de la boucle sélectionnée, à droite.
	auto controls = main.removeFromRight(300);
	main.removeFromRight(12);
	controls = controls.withSizeKeepingCentre(controls.getWidth(), juce::jmin(controls.getHeight(), 330));
	selectedLabel_.setBounds(controls.removeFromTop(28));
	controls.removeFromTop(6);
	mainButton_.setBounds(controls.removeFromTop(120));
	controls.removeFromTop(12);

	for (auto pair : { std::pair { &stopButton_, &undoButton_ }, std::pair { &redoButton_, &clearButton_ } })
	{
		auto row = controls.removeFromTop(54);
		pair.first->setBounds(row.removeFromLeft((row.getWidth() - 10) / 2));
		row.removeFromLeft(10);
		pair.second->setBounds(row);
		controls.removeFromTop(10);
	}

	memoryLabel_.setBounds(controls.removeFromTop(22));

	tilesArea_ = main;
	layoutTiles(main);

	auto column = [&settings](int width) { return settings.removeFromLeft(width).reduced(8, 0); };

	auto first = column(settings.getWidth() / 4);
	tempoCaption_.setBounds(first.removeFromTop(20));
	tempoSlider_.setBounds(first.removeFromTop(28));
	first.removeFromTop(8);
	quantizeCaption_.setBounds(first.removeFromTop(20));
	quantizeBox_.setBounds(first.removeFromTop(28).withWidth(150));

	auto second = column(settings.getWidth() / 3);
	latencyCaption_.setBounds(second.removeFromTop(20));
	latencySlider_.setBounds(second.removeFromTop(28));
	second.removeFromTop(8);
	toleranceCaption_.setBounds(second.removeFromTop(20));
	toleranceSlider_.setBounds(second.removeFromTop(28));

	auto third = column(settings.getWidth() / 2);
	tracksCaption_.setBounds(third.removeFromTop(20));
	tracksBox_.setBounds(third.removeFromTop(28).withWidth(80));
	third.removeFromTop(8);
	masterCaption_.setBounds(third.removeFromTop(20));
	masterSlider_.setBounds(third.removeFromTop(28));

	auto fourth = column(settings.getWidth());
	inputCaption_.setBounds(fourth.removeFromTop(20));
	inputBox_.setBounds(fourth.removeFromTop(28));
	fourth.removeFromTop(6);
	metronomeToggle_.setBounds(fourth.removeFromTop(24));
	monitorToggle_.setBounds(fourth.removeFromTop(24));
}

// --- Sessions ---

void BoohcleurEditor::showStatus(const juce::String& text, bool error)
{
	status_ = text;
	statusError_ = error;
	statusUntil_ = juce::Time::getMillisecondCounter() + (error ? 9000u : 4000u);
	refresh();
}

void BoohcleurEditor::showSessionMenu()
{
	juce::PopupMenu menu;
	const bool busy = processor_.sessions().isBusy();
	menu.addItem(1, fr("Nouvelle session"), !busy);
	menu.addItem(2, fr("Ouvrir…"), !busy);
	menu.addSeparator();
	menu.addItem(3, fr("Enregistrer"), !busy);
	menu.addItem(4, fr("Enregistrer sous…"), !busy);

	menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&sessionButton_),
		[safe = juce::Component::SafePointer<BoohcleurEditor>(this)](int result)
		{
			if (safe == nullptr)
				return;

			switch (result)
			{
			case 1: safe->newSession(); break;
			case 2: safe->openSession(); break;
			case 3: safe->saveSession(false); break;
			case 4: safe->saveSession(true); break;
			default: break;
			}
		});
}

void BoohcleurEditor::askBeforeDiscarding(std::function<void()> then)
{
	if (!processor_.sessions().hasUnsavedChanges())
	{
		then();
		return;
	}

	auto options = juce::MessageBoxOptions::makeOptionsOkCancel(juce::MessageBoxIconType::WarningIcon,
		fr("Boucles non enregistrées"),
		fr("La session « ") + processor_.sessions().currentName() + fr(" » a des changements non enregistrés. Les abandonner ?"),
		fr("Abandonner"), fr("Annuler"), this);

	juce::AlertWindow::showAsync(options, [then](int result)
	{
		if (result == 1)
			then();
	});
}

void BoohcleurEditor::saveSession(bool chooseFile)
{
	auto& sessions = processor_.sessions();
	const auto done = [safe = juce::Component::SafePointer<BoohcleurEditor>(this)](const SessionManager::Outcome& outcome)
	{
		if (safe != nullptr)
			safe->showStatus(outcome.message, !outcome.ok);
	};

	if (!chooseFile && sessions.currentFile() != juce::File {})
	{
		sessions.save(sessions.currentFile(), done);
		showStatus(fr("Enregistrement…"));
		return;
	}

	const auto folder = SessionManager::defaultFolder();
	folder.createDirectory();
	chooser_ = std::make_unique<juce::FileChooser>(fr("Enregistrer la session"),
		folder.getChildFile(sessions.currentFile() == juce::File {} ? fr("Ma session") : sessions.currentName()),
		juce::String("*") + boohcleur::session::extension);

	chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
			| juce::FileBrowserComponent::warnAboutOverwriting,
		[this, done, safe = juce::Component::SafePointer<BoohcleurEditor>(this)](const juce::FileChooser& chooser)
		{
			if (safe == nullptr || chooser.getResult() == juce::File {})
				return;

			processor_.sessions().save(chooser.getResult(), done);
			showStatus(fr("Enregistrement…"));
		});
}

void BoohcleurEditor::openSession()
{
	askBeforeDiscarding([safe = juce::Component::SafePointer<BoohcleurEditor>(this)]
	{
		if (safe == nullptr)
			return;

		const auto folder = SessionManager::defaultFolder();
		safe->chooser_ = std::make_unique<juce::FileChooser>(fr("Ouvrir une session (dossier .boohcleur)"),
			folder.isDirectory() ? folder : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
			juce::String("*") + boohcleur::session::extension + ";session.json");

		safe->chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
				| juce::FileBrowserComponent::canSelectDirectories,
			[safe](const juce::FileChooser& chooser)
			{
				if (safe == nullptr || chooser.getResult() == juce::File {})
					return;

				safe->processor_.sessions().open(chooser.getResult(), [safe](const SessionManager::Outcome& outcome)
				{
					if (safe == nullptr)
						return;

					safe->showStatus(outcome.message, !outcome.ok);
					safe->refreshSettingsFromEngine();
				});
				safe->showStatus(fr("Ouverture…"));
			});
	});
}

void BoohcleurEditor::newSession()
{
	askBeforeDiscarding([safe = juce::Component::SafePointer<BoohcleurEditor>(this)]
	{
		if (safe != nullptr)
			safe->processor_.sessions().clearAll([safe](const SessionManager::Outcome& outcome)
			{
				if (safe != nullptr)
					safe->showStatus(outcome.message, !outcome.ok);
			});
	});
}

void BoohcleurEditor::offerRecovery()
{
	auto options = juce::MessageBoxOptions::makeOptionsYesNo(juce::MessageBoxIconType::QuestionIcon,
		fr("Récupérer les boucles ?"),
		fr("La dernière fois, BOOHcleur s'est fermé avec des boucles non enregistrées. "
			"La sauvegarde automatique les a gardées : les récupérer ?"),
		fr("Récupérer"), fr("Ignorer"), this);

	juce::AlertWindow::showAsync(options, [safe = juce::Component::SafePointer<BoohcleurEditor>(this)](int result)
	{
		if (safe == nullptr)
			return;

		auto& sessions = safe->processor_.sessions();

		if (result != 1)
		{
			sessions.discardRecovery();
			return;
		}

		sessions.recover([safe](const SessionManager::Outcome& outcome)
		{
			if (safe == nullptr)
				return;

			safe->showStatus(outcome.message, !outcome.ok);
			safe->refreshSettingsFromEngine();
		});
	});
}

// Réglages relus après l'ouverture d'une session.
void BoohcleurEditor::refreshSettingsFromEngine()
{
	auto& engine = processor_.engine();
	tempoSlider_.setValue(engine.transportSnapshot().bpm, juce::dontSendNotification);
	toleranceSlider_.setValue(engine.lateToleranceMilliseconds(), juce::dontSendNotification);
	quantizeBox_.setSelectedId(engine.quantizeDivision() == boohcleur::GridDivision::Beat ? 2
		: engine.quantizeDivision() == boohcleur::GridDivision::Immediate ? 3 : 1, juce::dontSendNotification);
	tracksBox_.setSelectedId(engine.activeTracks(), juce::dontSendNotification);
	masterSlider_.setValue(engine.masterVolume(), juce::dontSendNotification);
	metronomeToggle_.setToggleState(engine.metronomeEnabled(), juce::dontSendNotification);
	monitorToggle_.setToggleState(engine.monitorEnabled(), juce::dontSendNotification);
	midiPanel_.refresh(engine.midiControl().snapshot(), false);
	resized();
	refresh();
}

