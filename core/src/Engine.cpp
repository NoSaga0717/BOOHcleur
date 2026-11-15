#include "boohcleur/core/Engine.h"

#include <algorithm>
#include <cmath>
#include <thread>

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
			|| config.maxTracks < 1 || config.maxTracks > maxTrackCount || config.numTracks < 1 || config.numTracks > config.maxTracks
			|| config.maxLayersPerTrack < 1 || config.maxLoopSeconds <= 0.0 || config.ringSeconds <= 0.0)
			return false;

		config_ = config;
		tempoMap_ = *map;
		clock_.stop();
		clock_.setTempoMap(tempoMap_);

		const auto ringFrames = static_cast<std::int64_t>(config.ringSeconds * config.sampleRate) + config.maxBlockSize;
		ring_.prepare(config.numChannels, ringFrames);

		if (!pool_.prepare(config.numChannels, config.memoryBudgetBytes))
			return false;

		TrackConfig trackConfig;
		trackConfig.numChannels = config.numChannels;
		trackConfig.maxLoopSamples = static_cast<SamplePos>(config.maxLoopSeconds * config.sampleRate);
		trackConfig.maxLayers = config.maxLayersPerTrack;
		trackConfig.fadeSamples = std::max(1, static_cast<int>(std::lround(config.fadeMilliseconds * 0.001 * config.sampleRate)));

		tracks_.clear();
		tracks_.resize(static_cast<std::size_t>(config.numTracks));

		for (auto& track : tracks_)
			track.prepare(trackConfig, pool_);

		scratch_.assign(static_cast<std::size_t>(config.numChannels),
			std::vector<float>(static_cast<std::size_t>(config.maxBlockSize), 0.0f));
		mixGain_.assign(static_cast<std::size_t>(config.numTracks), 0.0f);
		masterGain_ = master_.load();
		usedChunks_.store(0);

		// Sessions : la liste des blocs de chaque couche est réservée ici, le
		// thread audio n'a plus qu'à la recopier.
		snapshotState_.store(snapshotIdle);
		content_.tracks.assign(static_cast<std::size_t>(config.numTracks), TrackContent {});

		for (auto& content : content_.tracks)
		{
			content.layers.assign(static_cast<std::size_t>(config.maxLayersPerTrack), LayerContent {});

			for (auto& layer : content.layers)
				layer.chunks.assign(static_cast<std::size_t>(tracks_.front().chunksPerLayer()), -1);
		}

		pending_.assign(static_cast<std::size_t>(config.numTracks), Pending {});
		shared_ = std::make_unique<SharedTrackState[]>(static_cast<std::size_t>(config.numTracks));
		activeTracks_.store(std::clamp(activeTracks_.load(), 1, config.numTracks));
		selected_.store(std::clamp(selected_.load(), 0, config.numTracks - 1));

		while (commands_.pop())
		{
		}

		midi_.prepare(config.sampleRate);

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

	void Engine::process(const float* const* in, int numIn, float* const* out, int numOut, int numFrames,
		const MidiEvent* midi, int numMidi) noexcept
	{
		// Poignée de main avec suspend() : ordre séquentiel (seq_cst) des deux
		// côtés, donc soit suspend() voit ce bloc en cours et l'attend, soit ce
		// bloc voit la demande et ne touche à rien.
		inProcess_.store(true);

		if (suspendRequested_.load())
		{
			for (int c = 0; c < numOut; ++c)
				std::fill(out[c], out[c] + std::max(numFrames, 0), 0.0f);

			inProcess_.store(false);
			return;
		}

		processBlock(in, numIn, out, numOut, numFrames, midi, numMidi);
		inProcess_.store(false);
	}

	void Engine::processBlock(const float* const* in, int numIn, float* const* out, int numOut, int numFrames,
		const MidiEvent* midi, int numMidi) noexcept
	{
		if (tracks_.empty() || numFrames <= 0)
			return;

		// Sauvegarde en cours : figer ou libérer, en tête de bloc.
		switch (snapshotState_.load(std::memory_order_acquire))
		{
		case snapshotRequested:
			captureContent();
			snapshotState_.store(snapshotReady, std::memory_order_release);
			break;
		case snapshotReleasing:
			pool_.unpin();
			snapshotState_.store(snapshotIdle, std::memory_order_release);
			break;
		default:
			break;
		}

		// Sélection de l'entrée : en mono, un seul canal, que le tampon duplique.
		const InputMode mode = static_cast<InputMode>(inputMode_.load(std::memory_order_relaxed));
		const float* selected[2] = { nullptr, nullptr };
		int numSelected = 0;

		if (in != nullptr && numIn > 0)
		{
			if (mode == InputMode::Stereo && numIn >= 2)
			{
				selected[0] = in[0];
				selected[1] = in[1];
				numSelected = 2;
			}
			else
			{
				selected[0] = in[mode == InputMode::MonoInput2 && numIn >= 2 ? 1 : 0];
				numSelected = 1;
			}
		}

		in = selected;
		numIn = numSelected;
		ring_.write(in, numIn, numFrames);

		for (int c = 0; c < numOut; ++c)
			std::fill(out[c], out[c] + numFrames, 0.0f);

		while (auto command = commands_.pop())
			handleCommand(*command, clock_.blockStart());

		// Pédalier : gestes reconnus dans ce bloc, horodatés à l'appui.
		{
			midi_.processRequests();
			TriggerList triggers;

			for (int i = 0; midi != nullptr && i < numMidi; ++i)
			{
				MidiEvent event = midi[i];
				event.device = deviceSample_ + std::clamp<std::int64_t>(event.device, 0, numFrames - 1);
				midi_.onMidi(event, triggers);
			}

			midi_.update(deviceSample_ + numFrames - 1, triggers);

			for (int i = 0; i < triggers.count; ++i)
				handleTrigger(triggers.items[static_cast<std::size_t>(i)], clock_.blockStart());

			midi_.publish();
		}

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
					renderTracks(blockStart + cursor, next - cursor, segment, numOut);
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

		// Volume général, lissé.
		{
			const float target = master_.load(std::memory_order_relaxed);
			const float step = 1.0f / static_cast<float>(std::max(1, static_cast<int>(config_.fadeMilliseconds * 0.001 * config_.sampleRate)));

			for (int i = 0; i < numFrames; ++i)
			{
				masterGain_ += std::clamp(target - masterGain_, -step, step);

				for (int c = 0; c < numOut; ++c)
					out[c][i] *= masterGain_;
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

	void Engine::handleTrigger(const Trigger& trigger, SamplePos blockStart) noexcept
	{
		Command command;
		command.pressDevice = trigger.pressDevice;

		const int active = activeTracks_.load(std::memory_order_relaxed);

		if (trigger.track == boohcleur::selectedTrack)
		{
			command.track = selected_.load(std::memory_order_relaxed);
		}
		else
		{
			command.track = trigger.track;

			// Une pédale dédiée à une boucle la sélectionne aussi.
			if (command.track < active)
				selected_.store(command.track, std::memory_order_relaxed);
		}

		switch (trigger.action)
		{
		case ControlAction::TrackMain: command.type = CommandType::TrackMain; break;
		case ControlAction::TrackStop: command.type = CommandType::TrackStop; break;
		case ControlAction::TrackPlay: command.type = CommandType::TrackPlay; break;
		case ControlAction::TrackUndo: command.type = CommandType::TrackUndo; break;
		case ControlAction::TrackRedo: command.type = CommandType::TrackRedo; break;
		case ControlAction::TrackClear: command.type = CommandType::TrackClear; break;

		case ControlAction::TransportToggle:
			command.type = clock_.isPlaying() && transportStopAt_ < 0 ? CommandType::TransportStop : CommandType::TransportStart;
			break;

		case ControlAction::MetronomeToggle:
			metronome_.store(!metronome_.load(std::memory_order_relaxed), std::memory_order_relaxed);
			return;

		case ControlAction::SelectNext:
			selected_.store((selected_.load(std::memory_order_relaxed) + 1) % active, std::memory_order_relaxed);
			return;

		case ControlAction::SelectPrevious:
			selected_.store((selected_.load(std::memory_order_relaxed) + active - 1) % active, std::memory_order_relaxed);
			return;

		case ControlAction::None:
		case ControlAction::Count:
			return;
		}

		handleCommand(command, blockStart);
	}

	void Engine::handleCommand(const Command& command, SamplePos blockStart) noexcept
	{
		const bool playing = clock_.isPlaying() && transportStopAt_ < 0;

		// Le pédalier date l'appui : la quantification part du moment où le pied
		// a appuyé, même si le geste (appui long, double) a été reconnu plus tard.
		SamplePos pressSample = blockStart;

		if (command.pressDevice >= 0 && playing)
			pressSample = std::max<SamplePos>(0, command.pressDevice - musicalOrigin_);

		const Tick pressTick = tempoMap_.tickAtOrBefore(pressSample);
		const bool validTrack = command.track >= 0 && command.track < std::min(numTracks(), activeTracks_.load(std::memory_order_relaxed));
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

				pending_[static_cast<std::size_t>(index)] = { PendingAction::CloseRecording, maxEnd, true };
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

	void Engine::renderTracks(SamplePos at, int numFrames, float* const* out, int numOut) noexcept
	{
		const int active = activeTracks_.load(std::memory_order_relaxed);
		const int channels = config_.numChannels;
		const float step = 1.0f / static_cast<float>(std::max(1, static_cast<int>(config_.fadeMilliseconds * 0.001 * config_.sampleRate)));
		float* scratch[2] = { scratch_[0].data(), scratch_[static_cast<std::size_t>(std::min(1, channels - 1))].data() };

		for (std::size_t t = 0; t < tracks_.size(); ++t)
		{
			const auto& mixer = mixer_[t];
			const float target = static_cast<int>(t) < active && !mixer.muted.load(std::memory_order_relaxed)
				? mixer.volume.load(std::memory_order_relaxed) : 0.0f;
			float& gain = mixGain_[t];

			for (int c = 0; c < channels; ++c)
				std::fill(scratch_[static_cast<std::size_t>(c)].begin(), scratch_[static_cast<std::size_t>(c)].begin() + numFrames, 0.0f);

			// La boucle avance même muette : elle reste calée sur l'horloge.
			tracks_[t].render(tempoMap_, at, numFrames, scratch);

			if (gain == 0.0f && target == 0.0f)
				continue;

			for (int i = 0; i < numFrames; ++i)
			{
				gain += std::clamp(target - gain, -step, step);

				for (int c = 0; c < numOut; ++c)
					out[c][i] += scratch[std::min(c, channels - 1)][i] * gain;
			}
		}
	}

	void Engine::setActiveTracks(int count) noexcept
	{
		// Avant prepare(), la configuration par défaut fait foi (réglages restaurés).
		activeTracks_.store(std::clamp(count, 1, config_.numTracks));

		if (selected_.load() >= activeTracks_.load())
			selected_.store(activeTracks_.load() - 1);
	}

	void Engine::setSelectedTrack(int track) noexcept
	{
		if (track >= 0 && track < activeTracks_.load())
			selected_.store(track);
	}

	void Engine::setTrackVolume(int track, float gain) noexcept
	{
		if (track >= 0 && track < maxTrackCount)
			mixer_[static_cast<std::size_t>(track)].volume.store(std::clamp(gain, 0.0f, 4.0f));
	}

	float Engine::trackVolume(int track) const noexcept
	{
		return track >= 0 && track < maxTrackCount ? mixer_[static_cast<std::size_t>(track)].volume.load() : 0.0f;
	}

	void Engine::setTrackMuted(int track, bool muted) noexcept
	{
		if (track >= 0 && track < maxTrackCount)
			mixer_[static_cast<std::size_t>(track)].muted.store(muted);
	}

	bool Engine::trackMuted(int track) const noexcept
	{
		return track >= 0 && track < maxTrackCount && mixer_[static_cast<std::size_t>(track)].muted.load();
	}

	int Engine::waveform(int track, float* out, int maxBins) const noexcept
	{
		if (track < 0 || track >= numTracks() || shared_ == nullptr || maxBins < 1)
			return 0;

		const auto snapshot = trackSnapshot(track);
		const auto& loop = tracks_[static_cast<std::size_t>(track)];
		constexpr SamplePos hop = LoopTrack::peakHop;

		// Longueur d'un tour : la boucle, ou une mesure pendant la première prise.
		SamplePos turn = snapshot.loopLength;
		const bool firstTake = turn <= 0;

		if (firstTake)
		{
			const double bpm = bpm_.load(std::memory_order_relaxed);
			turn = static_cast<SamplePos>(config_.sampleRate * 60.0 / bpm * numerator_.load(std::memory_order_relaxed)
				* 4.0 / denominator_.load(std::memory_order_relaxed));

			if (snapshot.state != TrackState::Recording || turn <= 0)
				return 0;
		}

		constexpr int maxPreviewBins = 1024;
		const int bins = static_cast<int>(std::clamp<SamplePos>(turn / hop, 1, std::min(maxBins, maxPreviewBins)));
		std::fill(out, out + bins, 0.0f);

		// Crête par tranche dans une couche, somme des couches (elles s'empilent).
		float layerPeak[maxPreviewBins];

		const int layers = firstTake ? 1 : snapshot.numActiveLayers;

		for (int l = 0; l < layers; ++l)
		{
			const SamplePos start = firstTake ? 0 : loop.publishedStart(l);
			const SamplePos count = loop.publishedCount(l);

			// Première prise : seul le dernier tour est montré.
			const SamplePos from = firstTake ? std::max<SamplePos>(0, count - turn) : 0;
			std::fill(layerPeak, layerPeak + bins, 0.0f);

			for (SamplePos index = from - from % hop; index < count; index += hop)
			{
				const SamplePos position = (start + index) % turn;
				auto& slot = layerPeak[position * bins / turn];
				slot = std::max(slot, loop.peak(l, static_cast<std::size_t>(index / hop)));
			}

			for (int b = 0; b < bins; ++b)
				out[b] += layerPeak[b];
		}

		for (int b = 0; b < bins; ++b)
			out[b] = std::min(out[b], 1.0f);

		return bins;
	}

	MemorySnapshot Engine::memorySnapshot() const noexcept
	{
		MemorySnapshot snapshot;
		const std::size_t chunkBytes = static_cast<std::size_t>(pool_.numChannels()) * static_cast<std::size_t>(pool_.chunkFrames()) * sizeof(float);
		const auto used = static_cast<std::size_t>(usedChunks_.load(std::memory_order_relaxed));
		snapshot.totalBytes = pool_.bytes();
		snapshot.usedBytes = used * chunkBytes;
		const auto freeChunks = static_cast<double>(pool_.numChunks()) - static_cast<double>(used);
		snapshot.secondsLeft = freeChunks * pool_.chunkFrames() / config_.sampleRate;
		return snapshot;
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
			shared.pendingAutomatic.store(pending_[t].automatic, std::memory_order_relaxed);
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

		usedChunks_.store(pool_.usedChunks(), std::memory_order_relaxed);
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
		snapshot.pendingAutomatic = shared.pendingAutomatic.load(std::memory_order_relaxed);
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

	// --- Sessions ---

	void Engine::captureContent() noexcept
	{
		content_.sampleRate = config_.sampleRate;
		content_.bpm = tempoMap_.bpm();
		content_.signature = tempoMap_.signature();
		content_.numChannels = config_.numChannels;

		for (std::size_t t = 0; t < tracks_.size(); ++t)
		{
			const auto& track = tracks_[t];
			auto& content = content_.tracks[t];
			const int stable = track.stableLayers();

			content.numLayers = stable;
			content.numActive = std::min(track.numActiveLayers(), stable);
			content.playing = track.state() == TrackState::Playing || track.state() == TrackState::Overdubbing;
			content.span = track.span();
			content.loopLength = stable > 0 ? track.loopLength() : 0;

			for (int l = 0; l < stable; ++l)
			{
				auto& layer = content.layers[static_cast<std::size_t>(l)];
				const auto& chunks = track.layerChunks(l);
				layer.startOffset = track.layerStart(l);
				layer.count = track.layerCount(l);
				std::copy(chunks.begin(), chunks.end(), layer.chunks.begin());
			}
		}

		pool_.pin();
	}

	bool Engine::beginSnapshot() noexcept
	{
		if (tracks_.empty())
			return false;

		int expected = snapshotIdle;
		return snapshotState_.compare_exchange_strong(expected, snapshotRequested);
	}

	const ContentSnapshot* Engine::snapshot() const noexcept
	{
		return snapshotState_.load(std::memory_order_acquire) == snapshotReady ? &content_ : nullptr;
	}

	bool Engine::captureSnapshotNow() noexcept
	{
		if (!suspendRequested_.load() || snapshotState_.load() != snapshotRequested)
			return false;

		captureContent();
		snapshotState_.store(snapshotReady, std::memory_order_release);
		return true;
	}

	void Engine::readSnapshot(int track, int layer, int channel, SamplePos start, int count, float* dest) const noexcept
	{
		if (dest == nullptr || count <= 0)
			return;

		std::fill(dest, dest + count, 0.0f);

		if (snapshot() == nullptr || track < 0 || track >= numTracks() || channel < 0 || channel >= config_.numChannels)
			return;

		const auto& content = content_.tracks[static_cast<std::size_t>(track)];

		if (layer < 0 || layer >= content.numLayers)
			return;

		const auto& source = content.layers[static_cast<std::size_t>(layer)];
		const int frames = pool_.chunkFrames();
		SamplePos index = std::max<SamplePos>(start, 0);
		const SamplePos end = std::min<SamplePos>(start + count, source.count);

		while (index < end)
		{
			const auto slot = static_cast<std::size_t>(index / frames);
			const SamplePos within = index % frames;
			const SamplePos n = std::min<SamplePos>(frames - within, end - index);

			if (slot < source.chunks.size() && source.chunks[slot] >= 0)
			{
				const float* from = pool_.channel(source.chunks[slot], channel) + within;
				std::copy(from, from + n, dest + (index - start));
			}

			index += n;
		}
	}

	void Engine::endSnapshot() noexcept
	{
		int state = snapshotState_.load();

		if (state == snapshotRequested && snapshotState_.compare_exchange_strong(state, snapshotIdle))
			return;

		if (state != snapshotReady)
			return;

		// Audio suspendu : personne d'autre ne touche à la réserve.
		if (suspendRequested_.load())
		{
			pool_.unpin();
			snapshotState_.store(snapshotIdle);
			return;
		}

		snapshotState_.store(snapshotReleasing);
	}

	void Engine::suspend() noexcept
	{
		suspendRequested_.store(true);

		while (inProcess_.load())
			std::this_thread::yield();

		// Une libération demandée au thread audio se fait ici, puisqu'il ne la fera plus.
		if (snapshotState_.load() == snapshotReleasing)
		{
			pool_.unpin();
			snapshotState_.store(snapshotIdle);
		}
	}

	void Engine::resume() noexcept
	{
		suspendRequested_.store(false);
	}

	RestoreResult Engine::restore(const RestoreRequest& request) noexcept
	{
		if (tracks_.empty() || !suspendRequested_.load())
			return RestoreResult::NotPrepared;

		const auto map = TempoMap::create(config_.sampleRate, request.bpm, request.signature);

		if (!map)
			return RestoreResult::InvalidTempo;

		for (std::size_t t = tracks_.size(); t < request.tracks.size(); ++t)
			if (!request.tracks[t].layers.empty())
				return RestoreResult::TooManyTracks;

		// Tout repart de zéro : transport arrêté, actions planifiées oubliées.
		clock_.stop();
		transportStopAt_ = -1;

		for (std::size_t t = 0; t < tracks_.size(); ++t)
		{
			tracks_[t].reset();
			pending_[t] = {};
			mixGain_[t] = 0.0f;
		}

		tempoMap_ = *map;
		clock_.setTempoMap(tempoMap_);
		bpm_.store(tempoMap_.bpm());
		numerator_.store(tempoMap_.signature().numerator);
		denominator_.store(tempoMap_.signature().denominator);

		RestoreResult result = RestoreResult::Ok;

		for (std::size_t t = 0; t < std::min(tracks_.size(), request.tracks.size()) && result == RestoreResult::Ok; ++t)
		{
			const auto& source = request.tracks[t];

			if (source.layers.empty())
				continue;

			// La longueur en échantillons doit être celle que donne le tempo
			// (même fréquence d'échantillonnage qu'à la sauvegarde).
			const SamplePos expected = tempoMap_.samplePositionOf(source.span.anchor + source.span.length)
				- tempoMap_.samplePositionOf(source.span.anchor);

			if (source.span.length <= 0 || source.loopLength != expected)
			{
				result = RestoreResult::InvalidLoop;
				break;
			}

			if (!tracks_[t].restore(source.span, source.loopLength, source.numActive, source.layers.data(),
					static_cast<int>(source.layers.size()), source.playing))
			{
				const bool tooBig = static_cast<int>(source.layers.size()) > config_.maxLayersPerTrack
					|| source.loopLength > static_cast<SamplePos>(config_.maxLoopSeconds * config_.sampleRate);
				result = tooBig ? RestoreResult::InvalidLoop : RestoreResult::OutOfMemory;
			}
		}

		if (result != RestoreResult::Ok)
			for (auto& track : tracks_)
				track.reset();

		publish(clock_.blockStart());
		return result;
	}
}
