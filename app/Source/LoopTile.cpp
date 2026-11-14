#include "LoopTile.h"

namespace boohcleur::ui
{
	LoopTile::LoopTile(int index)
		: index_(index)
	{
		ring_.onTap = [this] { if (onTap) onTap(); };
		addAndMakeVisible(ring_);

		name_.setText("Boucle " + juce::String(index + 1), juce::dontSendNotification);
		name_.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
		name_.setColour(juce::Label::textColourId, colours::text);
		name_.setInterceptsMouseClicks(false, false);
		addAndMakeVisible(name_);

		volume_.setSliderStyle(juce::Slider::LinearHorizontal);
		volume_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
		volume_.setRange(0.0, 1.5, 0.01);
		volume_.setSkewFactorFromMidPoint(0.5);
		volume_.setDoubleClickReturnValue(true, 1.0);
		volume_.setColour(juce::Slider::thumbColourId, colours::accent);
		volume_.setColour(juce::Slider::trackColourId, colours::accent.withAlpha(0.5f));
		volume_.setColour(juce::Slider::backgroundColourId, colours::panelEdge);
		volume_.setWantsKeyboardFocus(false);
		volume_.setTooltip("Volume (double-clic : 100 %)");
		volume_.onValueChange = [this] { if (onVolume) onVolume(static_cast<float>(volume_.getValue())); };
		addAndMakeVisible(volume_);

		mute_.setClickingTogglesState(true);
		mute_.setColour(juce::TextButton::buttonColourId, colours::panelEdge);
		mute_.setColour(juce::TextButton::buttonOnColourId, colours::forState(TrackState::Recording));
		mute_.setColour(juce::TextButton::textColourOffId, colours::text);
		mute_.setColour(juce::TextButton::textColourOnId, colours::text);
		mute_.setWantsKeyboardFocus(false);
		mute_.setTooltip("Muet");
		mute_.onClick = [this] { if (onMute) onMute(mute_.getToggleState()); };
		addAndMakeVisible(mute_);
	}

	void LoopTile::setModel(const LoopRing::Model& model, bool selected, float volume, bool muted)
	{
		auto compact = model;
		compact.compact = true;
		ring_.setModel(compact);

		if (selected != selected_)
		{
			selected_ = selected;
			repaint();
		}

		if (!volume_.isMouseButtonDown())
			volume_.setValue(volume, juce::dontSendNotification);

		mute_.setToggleState(muted, juce::dontSendNotification);
		ring_.setAlpha(muted ? 0.45f : 1.0f);
	}

	void LoopTile::mouseUp(const juce::MouseEvent& event)
	{
		if (onSelect && !event.mouseWasDraggedSinceMouseDown())
			onSelect();
	}

	void LoopTile::paint(juce::Graphics& g)
	{
		const auto bounds = getLocalBounds().toFloat().reduced(3.0f);
		g.setColour(colours::panel);
		g.fillRoundedRectangle(bounds, 10.0f);
		g.setColour(selected_ ? colours::accent : colours::panelEdge);
		g.drawRoundedRectangle(bounds, 10.0f, selected_ ? 2.5f : 1.0f);
	}

	void LoopTile::resized()
	{
		auto area = getLocalBounds().reduced(10, 8);
		auto bottom = area.removeFromBottom(26);
		mute_.setBounds(bottom.removeFromRight(32));
		bottom.removeFromRight(6);
		volume_.setBounds(bottom);

		name_.setBounds(area.removeFromTop(20));
		ring_.setBounds(area.reduced(2));
	}
}
