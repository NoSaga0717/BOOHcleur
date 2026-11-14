#include "MidiPanel.h"

namespace boohcleur::ui
{
	namespace
	{
		const char* actionName(ControlAction action)
		{
			switch (action)
			{
			case ControlAction::TrackMain: return "Principal (Rec · Play · Overdub)";
			case ControlAction::TrackStop: return "Stop";
			case ControlAction::TrackPlay: return "Play";
			case ControlAction::TrackUndo: return "Undo";
			case ControlAction::TrackRedo: return "Redo";
			case ControlAction::TrackClear: return "Effacer (deux appuis)";
			case ControlAction::TransportToggle: return "Transport (démarrer / arrêter)";
			case ControlAction::MetronomeToggle: return "Métronome";
			case ControlAction::None:
			case ControlAction::Count: break;
			}

			return "";
		}

		const char* gestureName(int gesture)
		{
			switch (gesture)
			{
			case 0: return "Appui court";
			case 1: return "Appui long";
			default: return "Double appui";
			}
		}

		void styleLabel(juce::Label& label, float size, juce::Colour colour, bool bold = false)
		{
			label.setFont(juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)));
			label.setColour(juce::Label::textColourId, colour);
		}
	}

	juce::String MidiPanel::describe(const MidiSource& source)
	{
		const juce::String channel = fr(" · canal ") + juce::String(source.channel);

		switch (source.kind)
		{
		case MidiSourceKind::Note: return "Note " + juce::String(source.number) + channel;
		case MidiSourceKind::ControlChange: return "CC " + juce::String(source.number) + channel;
		case MidiSourceKind::ProgramChange: return "PC " + juce::String(source.number) + channel;
		case MidiSourceKind::None: break;
		}

		return {};
	}

	MidiPanel::MidiPanel(MidiControl& control)
		: control_(control)
	{
		title_.setText(fr("Pédalier MIDI"), juce::dontSendNotification);
		styleLabel(title_, 22.0f, colours::text, true);
		addAndMakeVisible(title_);

		styleLabel(lastMessage_, 14.0f, colours::accent);
		addAndMakeVisible(lastMessage_);

		help_.setText(fr("Cliquez une case, puis appuyez sur la pédale. Une case affectée : clic pour réapprendre, "
			"passer en pédale à bascule ou retirer."), juce::dontSendNotification);
		styleLabel(help_, 13.0f, colours::textDim);
		addAndMakeVisible(help_);

		for (int g = 0; g < numGestures; ++g)
		{
			auto& label = gestureLabels_[static_cast<std::size_t>(g)];
			label.setText(fr(gestureName(g)), juce::dontSendNotification);
			label.setJustificationType(juce::Justification::centred);
			styleLabel(label, 13.0f, colours::textDim, true);
			addAndMakeVisible(label);
		}

		for (int a = 0; a < numActions; ++a)
		{
			auto& label = actionLabels_[static_cast<std::size_t>(a)];
			label.setText(fr(actionName(static_cast<ControlAction>(a + 1))), juce::dontSendNotification);
			styleLabel(label, 14.0f, colours::text);
			addAndMakeVisible(label);

			for (int g = 0; g < numGestures; ++g)
			{
				auto& button = cells_[static_cast<std::size_t>(a)][static_cast<std::size_t>(g)].button;
				button.setColour(juce::TextButton::buttonColourId, colours::background);
				button.setColour(juce::TextButton::buttonOnColourId, colours::accent);
				button.setColour(juce::TextButton::textColourOffId, colours::text);
				button.setColour(juce::TextButton::textColourOnId, colours::background);
				button.setWantsKeyboardFocus(false);
				button.onClick = [this, a, g] { cellClicked(a, g); };
				addAndMakeVisible(button);
			}
		}

		auto styleSlider = [](juce::Slider& slider, double min, double max)
		{
			slider.setSliderStyle(juce::Slider::LinearHorizontal);
			slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
			slider.setRange(min, max, 10.0);
			slider.setTextValueSuffix(" ms");
			slider.setColour(juce::Slider::thumbColourId, colours::accent);
			slider.setColour(juce::Slider::trackColourId, colours::accent.withAlpha(0.5f));
			slider.setColour(juce::Slider::backgroundColourId, colours::panelEdge);
			slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
			slider.setWantsKeyboardFocus(false);
		};

		longCaption_.setText(fr("Durée d'un appui long"), juce::dontSendNotification);
		doubleCaption_.setText(fr("Délai du double appui"), juce::dontSendNotification);

		for (auto* caption : { &longCaption_, &doubleCaption_ })
		{
			styleLabel(*caption, 13.0f, colours::textDim);
			addAndMakeVisible(*caption);
		}

		const auto settings = control_.snapshot().settings;
		styleSlider(longSlider_, 200.0, 1500.0);
		longSlider_.setValue(settings.longPressMs, juce::dontSendNotification);
		styleSlider(doubleSlider_, 150.0, 600.0);
		doubleSlider_.setValue(settings.doubleTapMs, juce::dontSendNotification);

		for (auto* slider : { &longSlider_, &doubleSlider_ })
		{
			slider->onValueChange = [this]
			{
				ControlRequest request;
				request.type = ControlRequest::Type::SetSettings;
				request.settings = state_.settings;
				request.settings.longPressMs = longSlider_.getValue();
				request.settings.doubleTapMs = doubleSlider_.getValue();
				send(request);
			};
			addAndMakeVisible(*slider);
		}

		for (auto* button : { &closeButton_, &devicesButton_, &clearAllButton_ })
		{
			button->setColour(juce::TextButton::buttonColourId, colours::panelEdge);
			button->setColour(juce::TextButton::textColourOffId, colours::text);
			button->setWantsKeyboardFocus(false);
			addAndMakeVisible(*button);
		}

		devicesButton_.setButtonText(fr("Réglages audio / MIDI…"));
		devicesButton_.onClick = [this] { if (onOpenDeviceSettings) onOpenDeviceSettings(); };
		clearAllButton_.setButtonText("Tout retirer");
		clearAllButton_.onClick = [this]
		{
			juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, fr("Retirer toutes les affectations ?"),
				fr("Les pédales ne commanderont plus rien jusqu'au prochain apprentissage."), "Retirer", "Annuler", this,
				juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer(this)](int result)
				{
					if (safe != nullptr && result == 1)
					{
						ControlRequest request;
						request.type = ControlRequest::Type::ClearAll;
						safe->send(request);
					}
				}));
		};
		closeButton_.onClick = [this] { if (onClose) onClose(); };
	}

	void MidiPanel::send(const ControlRequest& request)
	{
		control_.request(request);
	}

	ControlRequest MidiPanel::learnRequest(int action, int gesture) const
	{
		ControlRequest request;
		request.type = ControlRequest::Type::StartLearn;
		request.binding.action = static_cast<ControlAction>(action + 1);
		request.binding.gesture = static_cast<Gesture>(gesture);
		request.binding.track = 0;
		return request;
	}

	void MidiPanel::cellClicked(int action, int gesture)
	{
		auto& cell = cells_[static_cast<std::size_t>(action)][static_cast<std::size_t>(gesture)];
		const auto target = learnRequest(action, gesture).binding;

		// Clic sur la case en cours d'apprentissage : annulation.
		if (state_.learning && state_.learnTarget.action == target.action && state_.learnTarget.gesture == target.gesture)
		{
			ControlRequest request;
			request.type = ControlRequest::Type::CancelLearn;
			send(request);
			return;
		}

		if (cell.bindingIndex < 0)
		{
			send(learnRequest(action, gesture));
			return;
		}

		const int index = cell.bindingIndex;
		const auto binding = state_.bindings[static_cast<std::size_t>(index)];
		const bool toggle = binding.mode == PressMode::EveryMessage;

		juce::PopupMenu menu;
		menu.addSectionHeader(describe(binding.source));
		menu.addItem(1, fr("Réapprendre"));
		menu.addItem(2, fr("Pédale à bascule (chaque message = un appui)"),
			binding.source.kind != MidiSourceKind::ProgramChange, toggle);
		menu.addSeparator();
		menu.addItem(3, "Retirer");

		menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&cell.button),
			[safe = juce::Component::SafePointer(this), action, gesture, index, binding, toggle](int result)
			{
				if (safe == nullptr || result == 0)
					return;

				// L'affectation a pu bouger pendant que le menu était ouvert.
				const auto& now = safe->state_;

				if (index >= now.numBindings || !(now.bindings[static_cast<std::size_t>(index)] == binding))
					return;

				ControlRequest request;

				if (result == 1)
				{
					request.type = ControlRequest::Type::RemoveBinding;
					request.index = index;
					safe->send(request);
					safe->send(safe->learnRequest(action, gesture));
				}
				else if (result == 2)
				{
					request.type = ControlRequest::Type::SetMode;
					request.index = index;
					request.mode = toggle ? PressMode::Momentary : PressMode::EveryMessage;
					safe->send(request);
				}
				else if (result == 3)
				{
					request.type = ControlRequest::Type::RemoveBinding;
					request.index = index;
					safe->send(request);
				}
			});
	}

	void MidiPanel::refresh(const ControlState& state, bool blink)
	{
		state_ = state;

		if (state.messageCount == 0)
			lastMessage_.setText(fr("Aucun message MIDI reçu. Branchez le pédalier et activez-le dans les réglages audio / MIDI."),
				juce::dontSendNotification);
		else
			lastMessage_.setText(fr("Dernier message : ") + describe(state.lastSource) + fr(" · valeur ")
				+ juce::String(state.lastValue) + "   (" + juce::String(state.messageCount) + fr(" reçus)"),
				juce::dontSendNotification);

		for (int a = 0; a < numActions; ++a)
		{
			for (int g = 0; g < numGestures; ++g)
			{
				auto& cell = cells_[static_cast<std::size_t>(a)][static_cast<std::size_t>(g)];
				const auto action = static_cast<ControlAction>(a + 1);
				const auto gesture = static_cast<Gesture>(g);
				cell.bindingIndex = -1;
				juce::String text;

				for (int i = 0; i < state.numBindings; ++i)
				{
					const auto& b = state.bindings[static_cast<std::size_t>(i)];

					if (b.action == action && b.gesture == gesture)
					{
						if (cell.bindingIndex < 0)
						{
							cell.bindingIndex = i;
							text = describe(b.source) + (b.mode == PressMode::EveryMessage ? fr(" · bascule") : juce::String());
						}
						else
						{
							text << " +";
						}
					}
				}

				const bool learning = state.learning && state.learnTarget.action == action && state.learnTarget.gesture == gesture;

				if (learning)
					text = fr("Appuyez sur la pédale…");
				else if (text.isEmpty())
					text = fr("—");

				cell.button.setButtonText(text);
				cell.button.setToggleState(learning && blink, juce::dontSendNotification);
				cell.button.setColour(juce::TextButton::textColourOffId,
					cell.bindingIndex >= 0 || learning ? colours::text : colours::textDim);
			}
		}

		if (!longSlider_.isMouseButtonDown())
			longSlider_.setValue(state.settings.longPressMs, juce::dontSendNotification);

		if (!doubleSlider_.isMouseButtonDown())
			doubleSlider_.setValue(state.settings.doubleTapMs, juce::dontSendNotification);

		devicesButton_.setVisible(onOpenDeviceSettings != nullptr);
		clearAllButton_.setEnabled(state.numBindings > 0);
	}

	void MidiPanel::paint(juce::Graphics& g)
	{
		g.fillAll(colours::background.withAlpha(0.96f));
		g.setColour(colours::panel);
		g.fillRoundedRectangle(getLocalBounds().reduced(12).toFloat(), 10.0f);
		g.setColour(colours::panelEdge);
		g.drawRoundedRectangle(getLocalBounds().reduced(12).toFloat(), 10.0f, 1.0f);
	}

	void MidiPanel::resized()
	{
		auto area = getLocalBounds().reduced(28, 22);

		auto top = area.removeFromTop(32);
		closeButton_.setBounds(top.removeFromRight(100));
		top.removeFromRight(10);
		clearAllButton_.setBounds(top.removeFromRight(120));
		top.removeFromRight(10);

		if (devicesButton_.isVisible())
		{
			devicesButton_.setBounds(top.removeFromRight(190));
			top.removeFromRight(10);
		}

		title_.setBounds(top);
		area.removeFromTop(6);
		lastMessage_.setBounds(area.removeFromTop(22));
		help_.setBounds(area.removeFromTop(20));
		area.removeFromTop(8);

		auto bottom = area.removeFromBottom(30);
		const int half = bottom.getWidth() / 2;
		auto left = bottom.removeFromLeft(half).reduced(0, 2);
		longCaption_.setBounds(left.removeFromLeft(150));
		longSlider_.setBounds(left.reduced(6, 0));
		auto right = bottom.reduced(0, 2);
		doubleCaption_.setBounds(right.removeFromLeft(150).translated(12, 0));
		doubleSlider_.setBounds(right.reduced(6, 0).withTrimmedLeft(12));
		area.removeFromBottom(8);

		const int labelWidth = 230;
		auto header = area.removeFromTop(22);
		header.removeFromLeft(labelWidth);
		const int cellWidth = header.getWidth() / numGestures;

		for (int g = 0; g < numGestures; ++g)
			gestureLabels_[static_cast<std::size_t>(g)].setBounds(header.removeFromLeft(cellWidth));

		const int rowHeight = juce::jmin(36, area.getHeight() / numActions);

		for (int a = 0; a < numActions; ++a)
		{
			auto row = area.removeFromTop(rowHeight).reduced(0, 3);
			actionLabels_[static_cast<std::size_t>(a)].setBounds(row.removeFromLeft(labelWidth));

			for (int g = 0; g < numGestures; ++g)
				cells_[static_cast<std::size_t>(a)][static_cast<std::size_t>(g)].button.setBounds(
					row.removeFromLeft(cellWidth).reduced(4, 0));
		}
	}
}
