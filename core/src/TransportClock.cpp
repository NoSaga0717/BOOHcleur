#include "boohcleur/core/TransportClock.h"

namespace boohcleur
{
	TransportClock::TransportClock(const TempoMap& tempoMap) noexcept
		: tempoMap_(tempoMap)
	{
	}

	bool TransportClock::setTempoMap(const TempoMap& tempoMap) noexcept
	{
		if (playing_)
			return false;

		tempoMap_ = tempoMap;
		return true;
	}

	void TransportClock::start(Tick fromTick) noexcept
	{
		blockStart_ = tempoMap_.samplePositionOf(fromTick);
		playing_ = true;
	}

	void TransportClock::stop() noexcept
	{
		playing_ = false;
	}

	std::optional<int> TransportClock::offsetInBlock(Tick tick, int blockSize) const noexcept
	{
		const SamplePos position = tempoMap_.samplePositionOf(tick);

		if (position < blockStart_ || position >= blockStart_ + blockSize)
			return std::nullopt;

		return static_cast<int>(position - blockStart_);
	}

	void TransportClock::advance(int blockSize) noexcept
	{
		if (playing_ && blockSize > 0)
			blockStart_ += blockSize;
	}

	LoopPosition locateInLoop(const TempoMap& tempoMap, const LoopSpan& loop, SamplePos sample) noexcept
	{
		const Tick tick = tempoMap.tickAtOrBefore(sample);
		const std::int64_t cycle = floorDiv(tick - loop.anchor, loop.length);

		const Tick cycleStartTick = loop.anchor + cycle * loop.length;
		const SamplePos cycleStart = tempoMap.samplePositionOf(cycleStartTick);
		const SamplePos nextCycleStart = tempoMap.samplePositionOf(cycleStartTick + loop.length);

		return { cycle, cycleStart, nextCycleStart - cycleStart, sample - cycleStart };
	}
}
