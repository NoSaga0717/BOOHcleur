#include "boohcleur/core/Engine.h"

#include <algorithm>
#include <cmath>

namespace boohcleur
{
	namespace
	{
		constexpr double pi = 3.14159265358979323846;
	}

	Engine::Engine()
		: tempoMap_(*TempoMap::create(48000.0, 120.0, {}))
		, clock_(tempoMap_)
	{
	}

	Engine::~Engine() = default;

	bool Engine::prepare(const EngineConfig& config)
	{
		const auto map = TempoMap::create(config.sampleRate, config.bpm, config.signature);

		if (!map || config.maxBlockSize < 1 || config.numChannels < 1 || config.numChannels > 2
			|| config.maxTracks < 1 || config.numTracks < 1 || config.numTracks > config.maxTracks
			|| config.maxLayersPerTrack < 1 || config.maxLoopSeconds <= 0.0 || config.ringSeconds <= 0.0)
			return false;

		config_ = config;
		tempoMap_ = *map;
		clock_.stop();
		clock_.setTempoMap(tempoMap_);

		const auto ringFrames = static_cast<std::int64_t>(config.ringSeconds * config.sampleRate) + config.maxBlockSize;
		ring_.prepare(config.numChannels, ringFrames);

		TrackConfig trackConfig;
		trackConfig.numChannels = config.numChannels;
		trackConfig.maxLoopSamples = static_cast<SamplePos>(config.maxLoopSeconds * config.sampleRate);
		trackConfig.maxLayers = config.maxLayersPerTrack;
		trackConfig.fadeSamples = std::max(1, static_cast<int>(std::lround(config.fadeMilliseconds * 0.001 * config.sampleRate)));

		tracks_.clear();
		tracks_.resize(static_cast<std::size_t>(config.numTracks));

		for (auto& track : tracks_)
			track.prepare(trackConfig);

		pending_.assign(static_cast<std::size_t>(config.numTracks), Pending {});
		shared_ = std::make_unique<SharedTrackState[]>(static_cast<std::size_t>(config.numTracks));

		while (commands_.pop())
		{
		}

		clickLength_ = static_cast<int>(0.02 * config.sampleRate);
		clickRemaining_ = 0;

		deviceSample_ = 0;
		musicalOrigin_ = 0;
		transportStopAt_ = -1;
		clock_.start(0);

		bpm_.store(tempoMap_.bpm());
		numerator_.store(tempoMap_.signature().numerator);
		denominator_.store(tempoMap_.signature().denominator);
		transportPlaying_.store(true);
		return true;
	}

	bool Engine::post(const Command& command) noexcept
	{
		return commands_.push(command);
	}

	std::int64_t Engine::musicalToDevice() const noexcept
	{
		return musicalOrigin_ + latencySamples_.load(std::memory_order_relaxed);
	}

	void Engine::process(const float* const* in, int numIn, float* const* out, int numOut, int numFrames) noexcept
	{
		if (tracks_.empty() || numFrames <= 0)
			return;

		ring_.write(in, numIn, numFrames);

		for (int c = 0; c < numOut; ++c)
			std::fill(out[c], out[c] + numFrames, 0.0f);

		while (auto command = commands_.pop())
			handleCommand(*command, clock_.blockStart());

		const SamplePos blockStart = clock_.blockStart();

		if (clock_.isPlaying() && numOut > 0)
		{
			int cursor = 0;

			while (cursor < numFrames)
			{
				// Prochaine action planifiée dans ce bloc (ou déjà passée : tout de suite).
				int next = numFrames;
				int which = -1;

				for (std::size_t t = 0; t < pending_.size(); ++t)
				{
					if (pending_[t].action == PendingAction::None)
						continue;

					const SamplePos offset = std::max<SamplePos>(
						tempoMap_.samplePositionOf(pending_[t].target) - blockStart, cursor);

					if (offset < next)
					{
						next = static_cast<int>(offset);
						which = static_cast<int>(t);
					}
				}

				if (next > cursor)
				{
					float* segment[2] = { out[0] + cursor, out[std::min(1, numOut - 1)] + cursor };

					for (auto& track : tracks_)
						track.render(tempoMap_, blockStart + cursor, next - cursor, segment);
				}

				cursor = next;

				if (which >= 0)
				{
					const Pending pending = pending_[static_cast<std::size_t>(which)];
					pending_[static_cast<std::size_t>(which)].action = PendingAction::None;
					execute(which, pending, blockStart + cursor);
				}
			}

			if (metronome_.load(std::memory_order_relaxed))
				renderMetronome(blockStart, numFrames, out, numOut);
		}

		for (auto& track : tracks_)
			track.capture(tempoMap_, ring_);

		if (monitor_.load(std::memory_order_relaxed) && numIn > 0 && in != nullptr)
		{
			for (int c = 0; c < numOut; ++c)
			{
				const float* source = in[std::min(c, numIn - 1)];

				if (source != nullptr)
					for (int i = 0; i < numFrames; ++i)
						out[c][i] += source[i];
			}
		}

		publish(blockStart);

		deviceSample_ += numFrames;
		clock_.advance(numFrames);

		if (transportStopAt_ >= 0 && clock_.blockStart() >= transportStopAt_)
		{
			clock_.stop();
			transportStopAt_ = -1;
		}
	}

	void Engine::handleCommand(const Command& command, SamplePos blockStart) noexcept
	{
		const bool playing = clock_.isPlaying() && transportStopAt_ < 0;
		const Tick pressTick = tempoMap_.tickAtOrBefore(blockStart);
		const bool validTrack = command.track >= 0 && command.track < numTracks();
		auto* track = validTrack ? &tracks_[static_cast<std::size_t>(command.track)] : nullptr;
		auto* pending = validTrack ? &pending_[static_cast<std::size_t>(command.track)] : nullptr;

		switch (command.type)
		{
		case CommandType::TrackMain:
			if (track != nullptr && playing)
				handleTrackMain(command.track, pressTick, blockStart);
			break;

		case CommandType::TrackStop:
			if (track == nullptr)
				break;

			switch (track->state())
			{
			case TrackState::Armed:
				track->disarm();
				pending->action = PendingAction::None;
				break;
			case TrackState::Recording:
				if (playing)
					schedule(command.track, PendingAction::CloseAndStop, pressTick, blockStart);
				break;
			case TrackState::Playing:
			case TrackState::Overdubbing:
				track->stop(blockStart);
				pending->action = PendingAction::None;
				break;
			case TrackState::Stopped:
				pending->action = PendingAction::None;
				break;
			default:
				break;
			}
			break;

		case CommandType::TrackPlay:
			if (track != nullptr && playing && track->state() == TrackState::Stopped)
				schedule(command.track, PendingAction::Play, pressTick, blockStart);
			break;

		case CommandType::TrackUndo:
			if (track != nullptr)
				track->undo(blockStart);
			break;

		case CommandType::TrackRedo:
			if (track != nullptr)
				track->redo();
			break;

		case CommandType::TrackClear:
			if (track != nullptr)
			{
				track->clear();
				pending->action = PendingAction::None;
			}
			break;

		case CommandType::TransportStart:
			if (!clock_.isPlaying())
			{
				clock_.start(0);
				musicalOrigin_ = deviceSample_;
				transportStopAt_ = -1;
			}
			else if (transportStopAt_ >= 0)
			{
				transportStopAt_ = -1;	// arrêt annulé pendant le fondu
			}
			break;

		case CommandType::TransportStop:
			if (!clock_.isPlaying())
				break;

			for (std::size_t t = 0; t < tracks_.size(); ++t)
			{
				auto& each = tracks_[t];

				if (each.state() == TrackState::Recording)
					each.cancelRecording();
				else if (each.state() == TrackState::Armed)
					each.disarm();
				else
					each.stop(blockStart);

				pending_[t].action = PendingAction::None;
			}

			transportStopAt_ = blockStart + static_cast<SamplePos>(config_.fadeMilliseconds * 0.001 * config_.sampleRate) + 1;
			break;

		case CommandType::SetTempo:
		{
			if (clock_.isPlaying())
				break;

			const bool allEmpty = std::all_of(tracks_.begin(), tracks_.end(),
				[](const LoopTrack& t) { return t.state() == TrackState::Empty && !t.hasContent(); });

			if (!allEmpty)
				break;

			if (const auto map = TempoMap::create(config_.sampleRate, command.value, tempoMap_.signature()))
			{
				tempoMap_ = *map;
				clock_.setTempoMap(tempoMap_);
				bpm_.store(tempoMap_.bpm());
			}
			break;
		}
		}
	}

	void Engine::handleTrackMain(int index, Tick pressTick, SamplePos blockStart) noexcept
	{
		auto& track = tracks_[static_cast<std::size_t>(index)];

		switch (track.state())
		{
		case TrackState::Empty:
			track.arm();
			schedule(index, PendingAction::StartRecording, pressTick, blockStart);
			break;

		case TrackState::Recording:
			schedule(index, PendingAction::CloseRecording, pressTick, blockStart);
			break;

		case TrackState::Playing:
			track.beginOverdub(tempoMap_, blockStart, musicalToDevice());
			break;

		case TrackState::Overdubbing:
			track.endOverdub(blockStart);
			break;

		case TrackState::Stopped:
			schedule(index, PendingAction::Play, pressTick, blockStart);
			break;

		case TrackState::Armed:
			break;
		}
	}

	void Engine::schedule(int index, PendingAction action, Tick pressTick, SamplePos blockStart) noexcept
	{
		(void)blockStart;

		const auto& signature = tempoMap_.signature();
		const GridDivision division = quantizeDivision();
		const Tick tolerance = tempoMap_.millisecondsToTicks(lateToleranceMs_.load(std::memory_order_relaxed));
		const TriggerResolution resolution = resolveTrigger(pressTick, division, signature, tolerance);

		Tick target = resolution.target;
		auto& track = tracks_[static_cast<std::size_t>(index)];
		auto& pending = pending_[static_cast<std::size_t>(index)];

		if (action == PendingAction::Play && resolution.retroactive)
			target = pressTick;	// on ne peut pas rejouer le passé : tout de suite

		if (action == PendingAction::CloseRecording || action == PendingAction::CloseAndStop)
		{
			const Tick start = track.recordStartTick();

			if (target <= start)
				target = division == GridDivision::Immediate ? start + 1 : boundaryAfter(start, division, signature);

			// Une demande de fermeture plus tôt que celle déjà prévue (limite de durée) la remplace.
			if (pending.action == PendingAction::CloseRecording || pending.action == PendingAction::CloseAndStop)
				target = std::min(target, pending.target);
		}

		pending = { action, target };
	}

	void Engine::execute(int index, const Pending& pending, SamplePos at) noexcept
	{
		auto& track = tracks_[static_cast<std::size_t>(index)];

		switch (pending.action)
		{
		case PendingAction::StartRecording:
			if (track.beginRecording(tempoMap_, pending.target, musicalToDevice(), ring_))
			{
				// Fermeture automatique à la durée maximale, sur une frontière de mesure.
				const auto maxTicks = static_cast<Tick>(
					std::floor(static_cast<double>(config_.maxLoopSeconds * config_.sampleRate) / tempoMap_.samplesPerTick())) - 1;
				Tick maxEnd = boundaryAtOrBefore(pending.target + maxTicks, GridDivision::Bar, tempoMap_.signature());

				if (maxEnd <= pending.target)
					maxEnd = pending.target + maxTicks;

				pending_[static_cast<std::size_t>(index)] = { PendingAction::CloseRecording, maxEnd };
			}
			break;

		case PendingAction::CloseRecording:
			track.closeRecording(tempoMap_, pending.target, false);
			break;

		case PendingAction::CloseAndStop:
			track.closeRecording(tempoMap_, pending.target, true);
			break;

		case PendingAction::Play:
			track.play();
			break;

		case PendingAction::None:
			break;
		}

		(void)at;
	}

	void Engine::renderMetronome(SamplePos blockStart, int numFrames, float* const* out, int numOut) noexcept
	{
		const auto& signature = tempoMap_.signature();

		// Temps qui tombent dans ce bloc.
		int triggers[8];
		bool accents[8];
		int numTriggers = 0;

		Tick beat = boundaryAtOrBefore(tempoMap_.tickAtOrBefore(blockStart), GridDivision::Beat, signature);

		if (tempoMap_.samplePositionOf(beat) < blockStart)
			beat = boundaryAfter(beat, GridDivision::Beat, signature);

		while (numTriggers < 8)
		{
			const SamplePos position = tempoMap_.samplePositionOf(beat);

			if (position >= blockStart + numFrames)
				break;

			triggers[numTriggers] = static_cast<int>(position - blockStart);
			accents[numTriggers] = (beat % signature.ticksPerBar()) == 0;
			++numTriggers;
			beat = boundaryAfter(beat, GridDivision::Beat, signature);
		}

		int nextTrigger = 0;

		for (int i = 0; i < numFrames; ++i)
		{
			if (nextTrigger < numTriggers && triggers[nextTrigger] == i)
			{
				clickRemaining_ = clickLength_;
				clickPhase_ = 0.0;
				clickIncrement_ = 2.0 * pi * (accents[nextTrigger] ? 1600.0 : 1000.0) / config_.sampleRate;
				++nextTrigger;
			}

			if (clickRemaining_ <= 0)
				continue;

			const float envelope = static_cast<float>(clickRemaining_) / static_cast<float>(clickLength_);
			const float value = 0.25f * envelope * envelope * static_cast<float>(std::sin(clickPhase_));
			clickPhase_ += clickIncrement_;
			--clickRemaining_;

			for (int c = 0; c < numOut; ++c)
				out[c][i] += value;
		}
	}

	void Engine::publish(SamplePos blockStart) noexcept
	{
		for (std::size_t t = 0; t < tracks_.size(); ++t)
		{
			const auto& track = tracks_[t];
			auto& shared = shared_[t];

			shared.state.store(static_cast<int>(track.state()), std::memory_order_relaxed);
			shared.pending.store(static_cast<int>(pending_[t].action), std::memory_order_relaxed);
			shared.pendingTarget.store(pending_[t].target, std::memory_order_relaxed);
			shared.numLayers.store(track.numLayers(), std::memory_order_relaxed);
			shared.numActive.store(track.numActiveLayers(), std::memory_order_relaxed);
			shared.loopLength.store(track.hasContent() ? track.loopLength() : 0, std::memory_order_relaxed);
			shared.position.store(track.hasContent() ? locateInLoop(tempoMap_, track.span(), blockStart).offset : 0,
				std::memory_order_relaxed);
			shared.recorded.store(track.state() == TrackState::Recording
					? std::max<SamplePos>(0, blockStart - tempoMap_.samplePositionOf(track.recordStartTick()))
					: 0,
				std::memory_order_relaxed);
		}

		transportPlaying_.store(clock_.isPlaying() && transportStopAt_ < 0, std::memory_order_relaxed);
		transportTick_.store(tempoMap_.tickAtOrBefore(blockStart), std::memory_order_relaxed);
	}

	TrackSnapshot Engine::trackSnapshot(int track) const noexcept
	{
		TrackSnapshot snapshot;

		if (track < 0 || track >= numTracks() || shared_ == nullptr)
			return snapshot;

		const auto& shared = shared_[static_cast<std::size_t>(track)];
		snapshot.state = static_cast<TrackState>(shared.state.load(std::memory_order_relaxed));
		snapshot.pending = static_cast<PendingAction>(shared.pending.load(std::memory_order_relaxed));
		snapshot.pendingTarget = shared.pendingTarget.load(std::memory_order_relaxed);
		snapshot.numLayers = shared.numLayers.load(std::memory_order_relaxed);
		snapshot.numActiveLayers = shared.numActive.load(std::memory_order_relaxed);
		snapshot.loopLength = shared.loopLength.load(std::memory_order_relaxed);
		snapshot.position = shared.position.load(std::memory_order_relaxed);
		snapshot.recorded = shared.recorded.load(std::memory_order_relaxed);
		return snapshot;
	}

	TransportSnapshot Engine::transportSnapshot() const noexcept
	{
		TransportSnapshot snapshot;
		snapshot.playing = transportPlaying_.load(std::memory_order_relaxed);
		snapshot.tick = transportTick_.load(std::memory_order_relaxed);
		snapshot.bpm = bpm_.load(std::memory_order_relaxed);
		snapshot.signature = { numerator_.load(std::memory_order_relaxed), denominator_.load(std::memory_order_relaxed) };
		return snapshot;
	}
}
