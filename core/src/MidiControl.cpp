#include "boohcleur/core/MidiControl.h"

#include <algorithm>
#include <cmath>

namespace boohcleur
{
	void MidiControl::prepare(double sampleRate) noexcept
	{
		sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

		// Les affectations survivent à un changement de carte son ; l'état des
		// boutons (appuis en cours) repart de zéro.
		for (auto& b : buttons_)
			b = Button { b.source };

		clearArmedUntil_ = INT64_MIN;
		now_ = 0;

		// Réglages restaurés avant le démarrage de l'audio : pris en compte dès maintenant.
		processRequests();
		publish();
	}

	std::int64_t MidiControl::ms(double milliseconds) const noexcept
	{
		return static_cast<std::int64_t>(std::llround(milliseconds * 0.001 * sampleRate_));
	}

	MidiControl::Decoded MidiControl::decode(const MidiEvent& event, PressMode mode) noexcept
	{
		Decoded d;
		const std::uint8_t type = event.status & 0xF0;
		const auto channel = static_cast<std::uint8_t>((event.status & 0x0F) + 1);
		const bool every = mode == PressMode::EveryMessage;

		switch (type)
		{
		case 0x90:	// Note On (vélocité 0 = Note Off)
			d.source = { MidiSourceKind::Note, channel, event.data1 };
			d.press = event.data2 > 0;
			d.release = every ? d.press : event.data2 == 0;
			break;

		case 0x80:	// Note Off
			d.source = { MidiSourceKind::Note, channel, event.data1 };
			d.release = !every;
			break;

		case 0xB0:	// Control Change
			d.source = { MidiSourceKind::ControlChange, channel, event.data1 };

			if (every)
				d.press = d.release = true;
			else if (event.data2 >= 64)
				d.press = true;
			else
				d.release = true;
			break;

		case 0xC0:	// Program Change : toujours un appui bref
			d.source = { MidiSourceKind::ProgramChange, channel, event.data1 };
			d.press = d.release = true;
			break;

		default:
			break;
		}

		return d;
	}

	// ---------------------------------------------------------------- demandes

	void MidiControl::processRequests() noexcept
	{
		while (auto r = requests_.pop())
		{
			switch (r->type)
			{
			case ControlRequest::Type::StartLearn:
				state_.learning = true;
				state_.learnTarget = r->binding;
				break;

			case ControlRequest::Type::CancelLearn:
				state_.learning = false;
				break;

			case ControlRequest::Type::AddBinding:
				addBinding(r->binding);
				break;

			case ControlRequest::Type::RemoveBinding:
				removeBinding(r->index);
				break;

			case ControlRequest::Type::SetMode:
				if (r->index >= 0 && r->index < state_.numBindings)
				{
					const MidiSource source = state_.bindings[static_cast<std::size_t>(r->index)].source;

					for (int i = 0; i < state_.numBindings; ++i)
						if (state_.bindings[static_cast<std::size_t>(i)].source == source)
							state_.bindings[static_cast<std::size_t>(i)].mode = r->mode;

					if (auto* b = button(source))
						*b = Button { source };
				}
				break;

			case ControlRequest::Type::ClearAll:
				state_.numBindings = 0;
				state_.learning = false;
				numButtons_ = 0;
				break;

			case ControlRequest::Type::SetSettings:
				state_.settings = r->settings;
				break;
			}
		}
	}

	void MidiControl::removeBinding(int index) noexcept
	{
		if (index < 0 || index >= state_.numBindings)
			return;

		for (int i = index; i + 1 < state_.numBindings; ++i)
			state_.bindings[static_cast<std::size_t>(i)] = state_.bindings[static_cast<std::size_t>(i + 1)];

		--state_.numBindings;
	}

	// ---------------------------------------------------------------- recherche

	bool MidiControl::hasGesture(const MidiSource& source, Gesture gesture) const noexcept
	{
		for (int i = 0; i < state_.numBindings; ++i)
		{
			const auto& b = state_.bindings[static_cast<std::size_t>(i)];

			if (b.source == source && b.gesture == gesture)
				return true;
		}

		return false;
	}

	PressMode MidiControl::modeFor(const MidiSource& source) const noexcept
	{
		for (int i = 0; i < state_.numBindings; ++i)
		{
			const auto& b = state_.bindings[static_cast<std::size_t>(i)];

			if (b.source == source)
				return b.mode;
		}

		return source.kind == MidiSourceKind::ProgramChange ? PressMode::EveryMessage : PressMode::Momentary;
	}

	MidiControl::Button* MidiControl::button(const MidiSource& source) noexcept
	{
		for (int i = 0; i < numButtons_; ++i)
			if (buttons_[static_cast<std::size_t>(i)].source == source)
				return &buttons_[static_cast<std::size_t>(i)];

		if (numButtons_ >= maxBindings)
		{
			// Plein : on recycle un bouton qui n'a plus d'affectation et qui est au repos.
			for (int i = 0; i < numButtons_; ++i)
			{
				auto& b = buttons_[static_cast<std::size_t>(i)];

				if (!b.pressed && !b.waitingDouble && modeFor(b.source) == PressMode::Momentary
					&& !hasGesture(b.source, Gesture::Short) && !hasGesture(b.source, Gesture::Long)
					&& !hasGesture(b.source, Gesture::Double))
				{
					b = Button { source };
					return &b;
				}
			}

			return nullptr;
		}

		auto& b = buttons_[static_cast<std::size_t>(numButtons_++)];
		b = Button { source };
		return &b;
	}

	// ---------------------------------------------------------------- messages

	void MidiControl::onMidi(const MidiEvent& event, TriggerList& out) noexcept
	{
		// Les minuteries qui expirent avant ce message passent d'abord.
		update(event.device, out);

		const Decoded probe = decode(event, PressMode::Momentary);

		if (probe.source.kind == MidiSourceKind::None)
			return;

		state_.lastSource = probe.source;
		state_.lastValue = probe.source.kind == MidiSourceKind::ProgramChange ? event.data1 : event.data2;
		++state_.messageCount;

		const PressMode mode = modeFor(probe.source);
		const Decoded d = decode(event, mode);

		if (state_.learning && d.press)
		{
			learn(d.source, d.source.kind == MidiSourceKind::ProgramChange ? PressMode::EveryMessage : mode);

			if (auto* b = button(d.source))
			{
				*b = Button { d.source };
				b->pressed = !d.release;
				b->consumed = true;
				b->pressDevice = event.device;
				b->releaseDevice = d.release ? event.device : INT64_MIN / 2;
			}
			return;
		}

		if (d.press)
			handlePress(d.source, event.device, out);

		if (d.release)
			handleRelease(d.source, event.device, out);
	}

	void MidiControl::addBinding(const Binding& binding) noexcept
	{
		if (binding.source.kind == MidiSourceKind::None || binding.action == ControlAction::None
			|| binding.action >= ControlAction::Count)
			return;

		// Une source et un geste ne déclenchent qu'une action : on remplace.
		bool replaced = false;

		for (int i = 0; i < state_.numBindings && !replaced; ++i)
		{
			auto& b = state_.bindings[static_cast<std::size_t>(i)];

			if (b.source == binding.source && b.gesture == binding.gesture)
			{
				b = binding;
				replaced = true;
			}
		}

		if (!replaced && state_.numBindings < maxBindings)
			state_.bindings[static_cast<std::size_t>(state_.numBindings++)] = binding;

		// Le mode appartient à la source : toutes ses affectations le partagent.
		for (int i = 0; i < state_.numBindings; ++i)
			if (state_.bindings[static_cast<std::size_t>(i)].source == binding.source)
				state_.bindings[static_cast<std::size_t>(i)].mode = binding.mode;
	}

	void MidiControl::learn(const MidiSource& source, PressMode mode) noexcept
	{
		Binding binding = state_.learnTarget;
		binding.source = source;
		binding.mode = mode;
		state_.learning = false;
		addBinding(binding);
	}

	void MidiControl::handlePress(const MidiSource& source, std::int64_t t, TriggerList& out) noexcept
	{
		const bool any = hasGesture(source, Gesture::Short) || hasGesture(source, Gesture::Long)
			|| hasGesture(source, Gesture::Double);

		if (!any)
			return;

		Button* b = button(source);

		if (b == nullptr)
			return;

		// Rebond pendant le relâchement : le relâchement est annulé, l'appui continue.
		if (b->pressed && b->releasePending)
		{
			b->releasePending = false;
			return;
		}

		if (b->pressed)
			return;	// CC qui repasse au-dessus de 64 sans être retombé

		if (t - b->releaseDevice < ms(state_.settings.debounceMs))
			return;	// rebond juste après un relâchement

		b->pressed = true;
		b->longFired = false;
		b->consumed = false;
		b->pressDevice = t;

		if (b->waitingDouble)
		{
			b->waitingDouble = false;
			b->consumed = true;
			fire(source, Gesture::Double, b->firstPressDevice, out);
			return;
		}

		// Aucun geste à attendre : l'appui court part tout de suite.
		if (!hasGesture(source, Gesture::Long) && !hasGesture(source, Gesture::Double))
		{
			b->consumed = true;
			fire(source, Gesture::Short, t, out);
		}
	}

	void MidiControl::handleRelease(const MidiSource& source, std::int64_t t, TriggerList& out) noexcept
	{
		Button* b = nullptr;

		for (int i = 0; i < numButtons_; ++i)
			if (buttons_[static_cast<std::size_t>(i)].source == source)
				b = &buttons_[static_cast<std::size_t>(i)];

		if (b == nullptr || !b->pressed || b->releasePending)
			return;

		// Relâchement trop proche de l'appui : peut-être un rebond, on le confirme
		// dans update() après le délai anti-rebond.
		if (t - b->pressDevice < ms(state_.settings.debounceMs))
		{
			b->releasePending = true;
			b->pendingReleaseDevice = t;
			return;
		}

		releaseNow(*b, t, out);
	}

	void MidiControl::releaseNow(Button& b, std::int64_t t, TriggerList& out) noexcept
	{
		b.pressed = false;
		b.releasePending = false;
		b.releaseDevice = t;

		if (b.consumed || b.longFired)
			return;

		if (hasGesture(b.source, Gesture::Double))
		{
			b.waitingDouble = true;
			b.firstPressDevice = b.pressDevice;
			b.doubleDeadline = t + ms(state_.settings.doubleTapMs);
			return;
		}

		fire(b.source, Gesture::Short, b.pressDevice, out);
	}

	void MidiControl::update(std::int64_t nowDevice, TriggerList& out) noexcept
	{
		now_ = std::max(now_, nowDevice);

		const std::int64_t debounce = ms(state_.settings.debounceMs);
		const std::int64_t longPress = ms(state_.settings.longPressMs);

		for (int i = 0; i < numButtons_; ++i)
		{
			auto& b = buttons_[static_cast<std::size_t>(i)];

			if (b.pressed && b.releasePending && now_ - b.pressDevice >= debounce)
				releaseNow(b, b.pendingReleaseDevice, out);

			if (b.pressed && !b.releasePending && !b.consumed && !b.longFired
				&& now_ - b.pressDevice >= longPress && hasGesture(b.source, Gesture::Long))
			{
				b.longFired = true;
				fire(b.source, Gesture::Long, b.pressDevice, out);
			}

			if (b.waitingDouble && now_ > b.doubleDeadline)
			{
				b.waitingDouble = false;

				if (hasGesture(b.source, Gesture::Short))
					fire(b.source, Gesture::Short, b.firstPressDevice, out);
			}
		}

		if (clearArmedUntil_ != INT64_MIN && now_ > clearArmedUntil_)
			clearArmedUntil_ = INT64_MIN;
	}

	void MidiControl::fire(const MidiSource& source, Gesture gesture, std::int64_t pressDevice, TriggerList& out) noexcept
	{
		for (int i = 0; i < state_.numBindings; ++i)
		{
			const auto& b = state_.bindings[static_cast<std::size_t>(i)];

			if (!(b.source == source) || b.gesture != gesture)
				continue;

			if (b.action == ControlAction::TrackClear)
			{
				// Garde-fou : effacer demande deux déclenchements rapprochés.
				if (clearArmedUntil_ != INT64_MIN && now_ <= clearArmedUntil_)
				{
					clearArmedUntil_ = INT64_MIN;
					out.add({ b.action, b.track, pressDevice });
				}
				else
				{
					clearArmedUntil_ = now_ + ms(state_.settings.clearConfirmMs);
				}
				continue;
			}

			out.add({ b.action, b.track, pressDevice });
		}
	}

	// ---------------------------------------------------------------- publication

	void MidiControl::publish() noexcept
	{
		state_.clearArmed = clearArmedUntil_ != INT64_MIN;

		const std::uint32_t v = version_.load(std::memory_order_relaxed);
		version_.store(v + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		published_ = state_;
		std::atomic_thread_fence(std::memory_order_release);
		version_.store(v + 2, std::memory_order_relaxed);
	}

	ControlState MidiControl::snapshot() const noexcept
	{
		ControlState copy;

		for (;;)
		{
			const std::uint32_t before = version_.load(std::memory_order_acquire);

			if ((before & 1u) != 0)
				continue;

			copy = published_;
			std::atomic_thread_fence(std::memory_order_acquire);

			if (version_.load(std::memory_order_relaxed) == before)
				return copy;
		}
	}
}
