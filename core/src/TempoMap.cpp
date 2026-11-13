#include "boohcleur/core/TempoMap.h"

#include <cmath>

namespace boohcleur
{
	std::optional<TempoMap> TempoMap::create(double sampleRate, double bpm, TimeSignature signature) noexcept
	{
		if (!(sampleRate >= minSampleRate && sampleRate <= maxSampleRate))
			return std::nullopt;

		if (!(bpm >= minBpm && bpm <= maxBpm))
			return std::nullopt;

		if (!TimeSignature::create(signature.numerator, signature.denominator))
			return std::nullopt;

		return TempoMap(sampleRate, bpm, signature);
	}

	TempoMap::TempoMap(double sampleRate, double bpm, TimeSignature signature) noexcept
		: sampleRate_(sampleRate)
		, bpm_(bpm)
		, signature_(signature)
		, samplesPerTick_(sampleRate * 60.0 / (bpm * static_cast<double>(ticksPerQuarter)))
	{
	}

	double TempoMap::exactSamplePositionOf(Tick tick) const noexcept
	{
		// Calcul en une seule multiplication depuis l'origine : l'erreur ne
		// s'accumule pas avec la durée.
		return static_cast<double>(tick) * samplesPerTick_;
	}

	SamplePos TempoMap::samplePositionOf(Tick tick) const noexcept
	{
		return static_cast<SamplePos>(std::llround(exactSamplePositionOf(tick)));
	}

	double TempoMap::exactTickAt(double samplePosition) const noexcept
	{
		return samplePosition / samplesPerTick_;
	}

	Tick TempoMap::tickAtOrBefore(SamplePos samplePosition) const noexcept
	{
		// Estimation, puis correction pour être exactement cohérent avec samplePositionOf.
		Tick tick = static_cast<Tick>(std::floor(exactTickAt(static_cast<double>(samplePosition))));

		while (samplePositionOf(tick + 1) <= samplePosition)
			++tick;

		while (samplePositionOf(tick) > samplePosition)
			--tick;

		return tick;
	}

	Tick TempoMap::millisecondsToTicks(double milliseconds) const noexcept
	{
		return static_cast<Tick>(std::llround(milliseconds * 0.001 * sampleRate_ / samplesPerTick_));
	}

	double TempoMap::ticksToMilliseconds(Tick ticks) const noexcept
	{
		return static_cast<double>(ticks) * samplesPerTick_ * 1000.0 / sampleRate_;
	}
}
