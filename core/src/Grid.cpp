#include "boohcleur/core/Grid.h"

#include <algorithm>

namespace boohcleur
{
	Tick divisionLength(GridDivision division, const TimeSignature& signature) noexcept
	{
		switch (division)
		{
		case GridDivision::Immediate:			return 0;
		case GridDivision::Bar:					return signature.ticksPerBar();
		case GridDivision::Beat:				return signature.ticksPerBeat();
		case GridDivision::Eighth:				return ticksPerQuarter / 2;
		case GridDivision::Sixteenth:			return ticksPerQuarter / 4;
		case GridDivision::QuarterTriplet:		return ticksPerQuarter * 2 / 3;
		case GridDivision::EighthTriplet:		return ticksPerQuarter / 3;
		case GridDivision::SixteenthTriplet:	return ticksPerQuarter / 6;
		}

		return 0;
	}

	Tick boundaryAtOrBefore(Tick tick, GridDivision division, const TimeSignature& signature) noexcept
	{
		const Tick length = divisionLength(division, signature);

		if (length == 0)
			return tick;

		const Tick perBar = signature.ticksPerBar();
		const Tick barStart = floorDiv(tick, perBar) * perBar;
		const Tick inBar = tick - barStart;

		return barStart + (inBar / length) * length;
	}

	Tick boundaryAfter(Tick tick, GridDivision division, const TimeSignature& signature) noexcept
	{
		const Tick length = divisionLength(division, signature);

		if (length == 0)
			return tick + 1;

		const Tick perBar = signature.ticksPerBar();
		const Tick barStart = floorDiv(tick, perBar) * perBar;
		const Tick inBar = tick - barStart;
		const Tick next = barStart + (inBar / length + 1) * length;

		// Une division qui ne tombe pas juste dans la mesure s'arrête à la mesure suivante.
		return std::min(next, barStart + perBar);
	}

	TriggerResolution resolveTrigger(Tick pressTick, GridDivision division, const TimeSignature& signature,
		Tick lateToleranceTicks) noexcept
	{
		if (division == GridDivision::Immediate)
			return { pressTick, false };

		const Tick previous = boundaryAtOrBefore(pressTick, division, signature);

		if (previous == pressTick)
			return { previous, false };

		if (pressTick - previous <= std::max<Tick>(lateToleranceTicks, 0))
			return { previous, true };

		return { boundaryAfter(pressTick, division, signature), false };
	}
}
