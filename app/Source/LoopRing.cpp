#include "LoopRing.h"

namespace boohcleur::ui
{
	LookAndFeel::LookAndFeel()
	{
		setColourScheme(getMidnightColourScheme());
		setColour(juce::ResizableWindow::backgroundColourId, colours::background);
		setColour(juce::ComboBox::backgroundColourId, colours::panel);
		setColour(juce::ComboBox::outlineColourId, colours::panelEdge);
		setColour(juce::PopupMenu::backgroundColourId, colours::panel);
		setColour(juce::ToggleButton::tickColourId, colours::accent);
	}

	juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
	{
		return juce::Font(juce::FontOptions(juce::jmin(34.0f, static_cast<float>(buttonHeight) * 0.38f), juce::Font::bold));
	}

	juce::Colour colours::forState(TrackState state)
	{
		switch (state)
		{
		case TrackState::Empty:			return juce::Colour(0xff3a3a40);
		case TrackState::Armed:			return juce::Colour(0xffff9f1c);
		case TrackState::Recording:		return juce::Colour(0xffff3b30);
		case TrackState::Playing:		return juce::Colour(0xff34c759);
		case TrackState::Overdubbing:	return juce::Colour(0xffffc21a);
		case TrackState::Stopped:		return juce::Colour(0xff5a7ea6);
		}

		return juce::Colours::grey;
	}

	juce::String stateLabel(TrackState state)
	{
		switch (state)
		{
		case TrackState::Empty:			return "VIDE";
		case TrackState::Armed:			return fr("ARMÉ");
		case TrackState::Recording:		return "REC";
		case TrackState::Playing:		return "PLAY";
		case TrackState::Overdubbing:	return "OVERDUB";
		case TrackState::Stopped:		return "STOP";
		}

		return {};
	}

	void LoopRing::setModel(const Model& model)
	{
		model_ = model;
		repaint();
	}

	void LoopRing::paint(juce::Graphics& g)
	{
		const auto& track = model_.track;
		const auto& transport = model_.transport;
		const auto stateColour = colours::forState(track.state);

		const float size = static_cast<float>(juce::jmin(getWidth(), getHeight()));
		const auto centre = getLocalBounds().toFloat().getCentre();
		const float thickness = size * 0.07f;
		const float radius = size * 0.5f - thickness;

		const double samplesPerBar = model_.sampleRate * 60.0 / transport.bpm
			* transport.signature.numerator * 4.0 / transport.signature.denominator;

		// Piste de fond
		juce::Path background;
		background.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
		g.setColour(colours::panelEdge);
		g.strokePath(background, juce::PathStrokeType(thickness));

		// Progression
		double progress = 0.0;
		int bars = 0;

		if (track.loopLength > 0)
		{
			progress = static_cast<double>(track.position) / static_cast<double>(track.loopLength);
			bars = static_cast<int>(std::lround(static_cast<double>(track.loopLength) / samplesPerBar));
		}
		else if (track.state == TrackState::Recording)
		{
			// Pendant la première prise : l'anneau fait un tour par mesure.
			const double inBars = static_cast<double>(track.recorded) / samplesPerBar;
			progress = inBars - std::floor(inBars);
		}

		if (progress > 0.0 || track.state == TrackState::Recording)
		{
			juce::Path arc;
			arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, 0.0f,
				static_cast<float>(progress * juce::MathConstants<double>::twoPi), true);
			g.setColour(stateColour);
			g.strokePath(arc, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
		}

		// Repères de mesure
		if (bars > 1 && bars <= 64)
		{
			g.setColour(colours::background.withAlpha(0.9f));

			for (int b = 0; b < bars; ++b)
			{
				const float angle = juce::MathConstants<float>::twoPi * static_cast<float>(b) / static_cast<float>(bars);
				const auto inner = centre.getPointOnCircumference(radius - thickness * 0.5f, angle);
				const auto outer = centre.getPointOnCircumference(radius + thickness * 0.5f, angle);
				g.drawLine({ inner, outer }, 2.0f);
			}
		}

		// Commande en attente : anneau extérieur pointillé qui clignote
		if (track.pending != PendingAction::None && model_.blink)
		{
			juce::Path outer;
			const float r = radius + thickness * 0.95f;
			outer.addCentredArc(centre.x, centre.y, r, r, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);

			juce::Path dashed;
			const float dashes[] = { 8.0f, 7.0f };
			juce::PathStrokeType(2.5f).createDashedStroke(dashed, outer, dashes, 2);
			g.setColour(colours::forState(TrackState::Armed));
			g.fillPath(dashed);
		}

		// Texte central
		auto text = getLocalBounds().toFloat().withSizeKeepingCentre(radius * 1.5f, radius * 1.1f);

		g.setColour(track.state == TrackState::Empty ? colours::textDim : stateColour);
		g.setFont(juce::Font(juce::FontOptions(size * 0.13f, juce::Font::bold)));
		g.drawFittedText(stateLabel(track.state), text.removeFromTop(text.getHeight() * 0.45f).toNearestInt(),
			juce::Justification::centredBottom, 1);

		juce::String detail;
		const auto pendingBar = juce::String(floorDiv(track.pendingTarget, transport.signature.ticksPerBar()) + 1);

		if (track.pending == PendingAction::StartRecording)
			detail = fr("départ mesure ") + pendingBar;
		else if (track.pending == PendingAction::CloseRecording || track.pending == PendingAction::CloseAndStop)
			detail = fr("fin mesure ") + pendingBar;
		else if (track.pending == PendingAction::Play)
			detail = fr("lecture mesure ") + pendingBar;
		else if (track.state == TrackState::Recording)
			detail = juce::String(static_cast<double>(track.recorded) / samplesPerBar, 1) + " mesures";
		else if (track.loopLength > 0)
			detail = juce::String(bars) + (bars > 1 ? " mesures" : " mesure") + fr("  ·  couches ")
				+ juce::String(track.numActiveLayers) + "/" + juce::String(track.numLayers);
		else
			detail = "appuie sur REC";

		g.setColour(colours::textDim);
		g.setFont(juce::Font(juce::FontOptions(size * 0.055f)));
		g.drawFittedText(detail, text.removeFromTop(text.getHeight() * 0.45f).toNearestInt(),
			juce::Justification::centredTop, 2);
	}
}
