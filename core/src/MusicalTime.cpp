#include "boohcleur/core/MusicalTime.h"

namespace boohcleur
{
	std::optional<TimeSignature> TimeSignature::create(int numerator, int denominator) noexcept
	{
		if (numerator < 1 || numerator > 32)
			return std::nullopt;

		switch (denominator)
		{
		case 1: case 2: case 4: case 8: case 16: case 32:
			return TimeSignature { numerator, denominator };
		default:
			return std::nullopt;
		}
	}

	BarBeatTick toBarBeatTick(Tick tick, const TimeSignature& signature) noexcept
	{
		const Tick perBar = signature.ticksPerBar();
		const Tick perBeat = signature.ticksPerBeat();

		const std::int64_t bar = floorDiv(tick, perBar);
		const Tick inBar = tick - bar * perBar;

		return { bar, static_cast<int>(inBar / perBeat), inBar % perBeat };
	}

	Tick toTick(const BarBeatTick& position, const TimeSignature& signature) noexcept
	{
		return position.bar * signature.ticksPerBar() + position.beat * signature.ticksPerBeat() + position.tick;
	}
}
