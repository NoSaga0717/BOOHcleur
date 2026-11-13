#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "boohcleur/core/Engine.h"

namespace boohcleur::ui
{
	// Texte UTF-8 (accents) : juce::String lit les const char* en ASCII.
	inline juce::String fr(const char* utf8) { return juce::String::fromUTF8(utf8); }

	// Couleurs de l'interface : fond noir et jaune du logo, un code couleur par état.
	namespace colours
	{
		inline const juce::Colour background { 0xff0d0d0f };
		inline const juce::Colour panel { 0xff18181c };
		inline const juce::Colour panelEdge { 0xff2a2a30 };
		inline const juce::Colour text { 0xfff2f2f2 };
		inline const juce::Colour textDim { 0xff9a9aa2 };
		inline const juce::Colour accent { 0xffffc21a };

		juce::Colour forState(TrackState state);
	}

	juce::String stateLabel(TrackState state);

	// Polices plus grandes pour les boutons (lisibles de loin, sur scène).
	class LookAndFeel final : public juce::LookAndFeel_V4
	{
	public:
		LookAndFeel();
		juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
	};

	// Boucle en anneau (ADR 0001, D9) : progression, état, couches et commande
	// en attente. Ce sera le premier widget du futur canevas.
	class LoopRing final : public juce::Component
	{
	public:
		struct Model
		{
			TrackSnapshot track;
			TransportSnapshot transport;
			double sampleRate = 48000.0;
			bool blink = false;
		};

		void setModel(const Model& model);
		void paint(juce::Graphics& g) override;

	private:
		Model model_;
	};
}
