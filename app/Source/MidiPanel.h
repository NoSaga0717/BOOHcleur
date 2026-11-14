#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LoopRing.h"
#include "boohcleur/core/MidiControl.h"

#include <array>
#include <functional>

namespace boohcleur::ui
{
	// Panneau « Pédalier MIDI » : une ligne par action, une colonne par geste.
	// Cliquer une case lance l'apprentissage ; cliquer une case affectée ouvre
	// un menu (réapprendre, pédale à bascule, retirer).
	class MidiPanel final : public juce::Component
	{
	public:
		explicit MidiPanel(MidiControl& control);

		std::function<void()> onClose;
		std::function<void()> onOpenDeviceSettings;	// Standalone seulement

		void refresh(const ControlState& state, bool blink);
		void paint(juce::Graphics& g) override;
		void resized() override;

		static juce::String describe(const MidiSource& source);

	private:
		static constexpr int numActions = static_cast<int>(ControlAction::Count) - 1;
		static constexpr int numGestures = 3;

		struct Cell
		{
			juce::TextButton button;
			int bindingIndex = -1;
		};

		void cellClicked(int action, int gesture);
		void send(const ControlRequest& request);
		ControlRequest learnRequest(int action, int gesture) const;

		MidiControl& control_;
		ControlState state_ {};

		std::array<juce::Label, numActions> actionLabels_;
		std::array<juce::Label, numGestures> gestureLabels_;
		std::array<std::array<Cell, numGestures>, numActions> cells_;

		juce::Label title_, lastMessage_, help_, longCaption_, doubleCaption_;
		juce::Slider longSlider_, doubleSlider_;
		juce::TextButton closeButton_ { "Fermer" }, devicesButton_, clearAllButton_;
	};
}
