#include "boohcleur/core/LoopTrack.h"

#include <algorithm>

namespace boohcleur
{
	namespace
	{
		float approach(float current, float target, float step) noexcept
		{
			if (current < target)
				return std::min(current + step, target);

			if (current > target)
				return std::max(current - step, target);

			return current;
		}
	}

	void LoopTrack::prepare(const TrackConfig& config, SamplePool& pool)
	{
		config_ = config;
		config_.fadeSamples = std::max(config_.fadeSamples, 1);
		pool_ = &pool;

		// +2 : un tour peut durer un échantillon de plus que le premier (J1).
		const SamplePos capacity = config_.maxLoopSamples + 2;
		const auto numChunks = static_cast<std::size_t>((capacity + pool.chunkFrames() - 1) / pool.chunkFrames());

		layers_.assign(static_cast<std::size_t>(config_.maxLayers), Layer {});

		for (auto& layer : layers_)
			layer.chunks.assign(numChunks, -1);

		preRoll_.assign(static_cast<std::size_t>(config_.numChannels),
			std::vector<float>(static_cast<std::size_t>(config_.fadeSamples), 0.0f));

		captures_ = {};
		state_ = TrackState::Empty;
		resetContent();
	}

	bool LoopTrack::isCapturing() const noexcept
	{
		return std::any_of(captures_.begin(), captures_.end(),
			[](const Capture& c) { return c.kind != CaptureKind::None; });
	}

	LoopTrack::Capture* LoopTrack::freeCapture() noexcept
	{
		for (auto& capture : captures_)
			if (capture.kind == CaptureKind::None)
				return &capture;

		return nullptr;
	}

	void LoopTrack::releaseLayer(Layer& layer) noexcept
	{
		for (auto& chunk : layer.chunks)
		{
			if (chunk >= 0 && pool_ != nullptr)
				pool_->release(chunk);

			chunk = -1;
		}

		layer.count = 0;
	}

	bool LoopTrack::ensure(Layer& layer, SamplePos index) noexcept
	{
		const auto slot = static_cast<std::size_t>(index / pool_->chunkFrames());

		if (slot >= layer.chunks.size())
			return false;

		if (layer.chunks[slot] < 0)
			layer.chunks[slot] = pool_->acquire();

		return layer.chunks[slot] >= 0;
	}

	float LoopTrack::read(const Layer& layer, int c, SamplePos index) const noexcept
	{
		const int frames = pool_->chunkFrames();
		const auto slot = static_cast<std::size_t>(index / frames);

		if (slot >= layer.chunks.size() || layer.chunks[slot] < 0)
			return 0.0f;

		return pool_->channel(layer.chunks[slot], c)[index % frames];
	}

	float& LoopTrack::sample(Layer& layer, int c, SamplePos index) noexcept
	{
		const int frames = pool_->chunkFrames();
		return pool_->channel(layer.chunks[static_cast<std::size_t>(index / frames)], c)[index % frames];
	}

	void LoopTrack::resetContent() noexcept
	{
		for (auto& capture : captures_)
			capture.kind = CaptureKind::None;

		for (auto& layer : layers_)
		{
			releaseLayer(layer);
			layer.count = 0;
			layer.startOffset = 0;
			layer.gain = 0.0f;
		}

		hasSpan_ = false;
		span_ = {};
		loopLength_ = 0;
		numLayers_ = 0;
		numActive_ = 0;
		trackGain_ = 0.0f;
		trackTarget_ = 0.0f;
		clearing_ = false;
	}

	bool LoopTrack::arm() noexcept
	{
		if (state_ != TrackState::Empty && state_ != TrackState::Armed)
			return false;

		state_ = TrackState::Armed;
		return true;
	}

	bool LoopTrack::disarm() noexcept
	{
		if (state_ != TrackState::Armed)
			return false;

		state_ = TrackState::Empty;
		return true;
	}

	bool LoopTrack::beginRecording(const TempoMap& tempoMap, Tick startTick, std::int64_t musicalToDevice,
		const InputRingBuffer& ring) noexcept
	{
		if (state_ != TrackState::Empty && state_ != TrackState::Armed)
			return false;

		if (layers_.empty())
			return false;

		if (clearing_ || numLayers_ > 0)
			resetContent();

		const SamplePos start = tempoMap.samplePositionOf(startTick);

		// Son juste avant le début : sert au fondu de raccord fin -> début.
		const int fade = config_.fadeSamples;
		const std::int64_t preDevice = start - fade + musicalToDevice;
		const bool available = ring.isAvailable(preDevice, fade);

		for (int c = 0; c < config_.numChannels; ++c)
			for (int i = 0; i < fade; ++i)
				preRoll_[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] =
					available ? ring.sample(std::min(c, ring.numChannels() - 1), preDevice + i) : 0.0f;

		auto& layer = layers_[0];
		layer.startOffset = 0;
		layer.count = 0;
		layer.gain = 1.0f;

		captures_[0] = { CaptureKind::First, 0, start, start, INT64_MAX, musicalToDevice };
		captures_[1].kind = CaptureKind::None;

		recordStartTick_ = startTick;
		state_ = TrackState::Recording;
		return true;
	}

	bool LoopTrack::closeRecording(const TempoMap& tempoMap, Tick endTick, bool thenStop) noexcept
	{
		if (state_ != TrackState::Recording || endTick <= recordStartTick_)
			return false;

		const SamplePos start = tempoMap.samplePositionOf(recordStartTick_);
		const SamplePos end = tempoMap.samplePositionOf(endTick);

		if (end - start > config_.maxLoopSamples)
			return false;

		span_ = { recordStartTick_, endTick - recordStartTick_ };
		hasSpan_ = true;
		loopLength_ = end - start;

		for (auto& capture : captures_)
			if (capture.kind == CaptureKind::First)
				capture.end = end;

		numLayers_ = 1;
		numActive_ = 1;
		layers_[0].gain = 1.0f;

		// La lecture enchaîne directement sur la fin de la prise : pas de fondu d'entrée.
		trackGain_ = thenStop ? 0.0f : 1.0f;
		trackTarget_ = trackGain_;
		state_ = thenStop ? TrackState::Stopped : TrackState::Playing;
		return true;
	}

	bool LoopTrack::cancelRecording() noexcept
	{
		if (state_ != TrackState::Recording)
			return false;

		resetContent();
		state_ = TrackState::Empty;
		return true;
	}

	bool LoopTrack::beginOverdub(const TempoMap& tempoMap, SamplePos at, std::int64_t musicalToDevice) noexcept
	{
		if (state_ != TrackState::Playing)
			return false;

		// Les couches annulées vont être réutilisées : on arrête toute capture qui y écrit encore.
		for (auto& capture : captures_)
			if (capture.kind == CaptureKind::Overdub && capture.layer >= numActive_)
				capture.kind = CaptureKind::None;

		if (numActive_ >= config_.maxLayers)
			return false;

		Capture* capture = freeCapture();

		if (capture == nullptr)
			return false;

		// Les couches annulées sont abandonnées : leur mémoire retourne à la réserve.
		for (int l = numActive_; l < numLayers_; ++l)
			releaseLayer(layers_[static_cast<std::size_t>(l)]);

		numLayers_ = numActive_;

		const int index = numLayers_;
		auto& layer = layers_[static_cast<std::size_t>(index)];
		releaseLayer(layer);
		layer.startOffset = locateInLoop(tempoMap, span_, at).offset;
		layer.count = 0;
		layer.gain = 1.0f;

		++numLayers_;
		++numActive_;

		*capture = { CaptureKind::Overdub, index, at, at, INT64_MAX, musicalToDevice };
		state_ = TrackState::Overdubbing;
		return true;
	}

	bool LoopTrack::endOverdub(SamplePos at) noexcept
	{
		if (state_ != TrackState::Overdubbing)
			return false;

		for (auto& capture : captures_)
			if (capture.kind == CaptureKind::Overdub && capture.end == INT64_MAX)
				capture.end = std::max(at, capture.start);

		state_ = TrackState::Playing;
		return true;
	}

	bool LoopTrack::play() noexcept
	{
		if (state_ != TrackState::Stopped || !hasContent())
			return false;

		trackTarget_ = 1.0f;
		state_ = TrackState::Playing;
		return true;
	}

	bool LoopTrack::stop(SamplePos at) noexcept
	{
		if (state_ == TrackState::Overdubbing)
			endOverdub(at);

		if (state_ != TrackState::Playing)
			return false;

		trackTarget_ = 0.0f;
		state_ = TrackState::Stopped;
		return true;
	}

	bool LoopTrack::undo(SamplePos at) noexcept
	{
		if (state_ == TrackState::Overdubbing)
			endOverdub(at);

		if (!hasContent() || numActive_ == 0)
			return false;

		--numActive_;
		return true;
	}

	bool LoopTrack::redo() noexcept
	{
		if (state_ == TrackState::Overdubbing || numActive_ >= numLayers_)
			return false;

		++numActive_;
		return true;
	}

	bool LoopTrack::clear() noexcept
	{
		if (state_ == TrackState::Empty)
			return false;

		if (state_ == TrackState::Recording || state_ == TrackState::Armed)
		{
			resetContent();
			state_ = TrackState::Empty;
			return true;
		}

		for (auto& capture : captures_)
			capture.kind = CaptureKind::None;

		trackTarget_ = 0.0f;
		clearing_ = true;
		state_ = TrackState::Empty;

		if (trackGain_ <= 0.0f)
			resetContent();

		return true;
	}

	void LoopTrack::render(const TempoMap& tempoMap, SamplePos blockStart, int numFrames, float* const* out) noexcept
	{
		if (!hasContent() || (trackGain_ <= 0.0f && trackTarget_ <= 0.0f))
		{
			if (clearing_)
				resetContent();

			return;
		}

		const float step = 1.0f / static_cast<float>(config_.fadeSamples);
		LoopPosition position = locateInLoop(tempoMap, span_, blockStart);

		for (int i = 0; i < numFrames; ++i)
		{
			if (position.offset >= position.cycleLength)
				position = locateInLoop(tempoMap, span_, blockStart + i);

			trackGain_ = approach(trackGain_, trackTarget_, step);

			for (int l = 0; l < numLayers_; ++l)
			{
				auto& layer = layers_[static_cast<std::size_t>(l)];
				layer.gain = approach(layer.gain, l < numActive_ ? 1.0f : 0.0f, step);

				if (layer.gain <= 0.0f)
					continue;

				SamplePos rel = position.offset - layer.startOffset;

				if (rel < 0)
					rel += loopLength_;

				if (rel < 0 || rel >= layer.count)
					continue;

				const float gain = layer.gain * trackGain_;

				for (int c = 0; c < config_.numChannels; ++c)
					out[c][i] += read(layer, c, rel) * gain;
			}

			++position.offset;
		}

		if (clearing_ && trackGain_ <= 0.0f)
			resetContent();
	}

	void LoopTrack::capture(const TempoMap& tempoMap, const InputRingBuffer& ring) noexcept
	{
		for (auto& capture : captures_)
		{
			if (capture.kind == CaptureKind::First)
				captureFirst(capture, ring);
			else if (capture.kind == CaptureKind::Overdub)
				captureOverdub(capture, tempoMap, ring);
		}
	}

	void LoopTrack::captureFirst(Capture& capture, const InputRingBuffer& ring) noexcept
	{
		auto& layer = layers_[0];
		const SamplePos capacity = config_.maxLoopSamples + 2;

		// Un échantillon de plus que la boucle : un tour peut durer un échantillon de plus.
		const SamplePos stopAt = capture.end == INT64_MAX ? INT64_MAX : capture.end + 1;

		while (capture.next < stopAt)
		{
			const SamplePos index = capture.next - capture.start;
			const std::int64_t device = capture.next + capture.musicalToDevice;

			if (index >= capacity || device >= ring.writePosition())
				break;

			const bool available = device >= ring.oldestAvailable();

			if (ensure(layer, index))
				for (int c = 0; c < config_.numChannels; ++c)
					sample(layer, c, index) = available ? ring.sample(std::min(c, ring.numChannels() - 1), device) : 0.0f;

			++capture.next;
			layer.count = capture.next - capture.start;
		}

		if (capture.next >= stopAt)
		{
			finishFirstLayer(capture);
			capture.kind = CaptureKind::None;
		}
	}

	void LoopTrack::finishFirstLayer(const Capture& capture) noexcept
	{
		auto& layer = layers_[0];
		const SamplePos length = capture.end - capture.start;
		layer.count = std::min(length + 1, config_.maxLoopSamples + 2);

		// Raccord fin -> début : la fin de la boucle est fondue vers le son qui
		// précédait juste le début de la prise. Après le fondu, la lecture
		// enchaîne sur le début comme dans l'enregistrement : aucune discontinuité.
		const int fade = static_cast<int>(std::min<SamplePos>(config_.fadeSamples, length / 2));
		const int preOffset = config_.fadeSamples - fade;

		for (int i = 0; i < fade; ++i)
		{
			const float g = (static_cast<float>(i) + 0.5f) / static_cast<float>(fade);
			const auto index = static_cast<std::size_t>(length - fade + i);

			if (layer.chunks[index / static_cast<std::size_t>(pool_->chunkFrames())] < 0)
				continue;	// réserve vide pendant la prise : rien à fondre

			for (int c = 0; c < config_.numChannels; ++c)
			{
				auto& value = sample(layer, c, static_cast<SamplePos>(index));
				value = value * (1.0f - g)
					+ preRoll_[static_cast<std::size_t>(c)][static_cast<std::size_t>(preOffset + i)] * g;
			}
		}
	}

	void LoopTrack::captureOverdub(Capture& capture, const TempoMap& tempoMap, const InputRingBuffer& ring) noexcept
	{
		const int fade = config_.fadeSamples;
		const SamplePos stopAt = capture.end == INT64_MAX ? INT64_MAX : capture.end + fade;

		while (capture.next < stopAt)
		{
			const std::int64_t device = capture.next + capture.musicalToDevice;

			if (device >= ring.writePosition())
				return;

			auto* layer = &layers_[static_cast<std::size_t>(capture.layer)];

			if (layer->count >= loopLength_)
			{
				// Couche pleine (un tour complet). Si l'overdub continue, une
				// nouvelle couche commence : un tour = une couche annulable.
				if (capture.next >= capture.end || numLayers_ >= config_.maxLayers || capture.layer != numLayers_ - 1)
				{
					capture.kind = CaptureKind::None;
					return;
				}

				const int index = numLayers_;
				layer = &layers_[static_cast<std::size_t>(index)];
				releaseLayer(*layer);
				layer->startOffset = locateInLoop(tempoMap, span_, capture.next).offset;
				layer->count = 0;
				layer->gain = 1.0f;
				capture.layer = index;
				numActive_ = ++numLayers_;
			}

			const SamplePos sinceStart = capture.next - capture.start;
			float gain = 1.0f;

			if (sinceStart < fade)
				gain = (static_cast<float>(sinceStart) + 0.5f) / static_cast<float>(fade);

			if (capture.next >= capture.end)
				gain *= 1.0f - (static_cast<float>(capture.next - capture.end) + 0.5f) / static_cast<float>(fade);

			const bool available = device >= ring.oldestAvailable();
			const SamplePos index = layer->count;

			if (ensure(*layer, index))
				for (int c = 0; c < config_.numChannels; ++c)
					sample(*layer, c, index) = available ? ring.sample(std::min(c, ring.numChannels() - 1), device) * gain : 0.0f;

			++layer->count;
			++capture.next;
		}

		capture.kind = CaptureKind::None;
	}
}
