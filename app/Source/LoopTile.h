#pragma once

#include "LoopRing.h"

#include <functional>

namespace boohcleur::ui
{
	// Une boucle dans la grille : anneau (clic = bouton principal de cette
	// boucle), nom, volume et mute. Cadre jaune quand elle est sélectionnée.
	class LoopTile final : public juce::Component
	{
	public:
		explicit LoopTile(int index);

		std::function<void()> onTap;					// clic sur l'anneau
		std::function<void()> onSelect;					// clic ailleurs sur la tuile
		std::function<void(float)> onVolume;
		std::function<void(bool)> onMute;

		void setModel(const LoopRing::Model& model, bool selected, float volume, bool muted);
		void paint(juce::Graphics& g) override;
		void resized() override;
		void mouseUp(const juce::MouseEvent& event) override;

	private:
		int index_;
		bool selected_ = false;
		LoopRing ring_;
		juce::Label name_;
		juce::Slider volume_;
		juce::TextButton mute_ { "M" };
	};
}
