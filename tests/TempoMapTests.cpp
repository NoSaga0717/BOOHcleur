#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "boohcleur/core/TempoMap.h"

#include <cmath>

using namespace boohcleur;
using Catch::Matchers::WithinAbs;

namespace
{
	TempoMap make(double sr, double bpm, TimeSignature sig = { 4, 4 })
	{
		return *TempoMap::create(sr, bpm, sig);
	}
}

TEST_CASE("Tempo map : bornes des parametres")
{
	CHECK(TempoMap::create(48000, 120, { 4, 4 }).has_value());
	CHECK_FALSE(TempoMap::create(48000, 10, { 4, 4 }).has_value());
	CHECK_FALSE(TempoMap::create(48000, 500, { 4, 4 }).has_value());
	CHECK_FALSE(TempoMap::create(1000, 120, { 4, 4 }).has_value());
	CHECK_FALSE(TempoMap::create(48000, 120, { 4, 3 }).has_value());
	CHECK_FALSE(TempoMap::create(std::nan(""), 120, { 4, 4 }).has_value());
}

TEST_CASE("Tempo map : valeurs exactes a 120 BPM 48 kHz")
{
	const auto map = make(48000, 120);

	// Une noire = 0,5 s = 24000 échantillons ; une mesure 4/4 = 96000.
	CHECK(map.samplePositionOf(960) == 24000);
	CHECK(map.samplePositionOf(3840) == 96000);
	CHECK(map.samplePositionOf(3840 * 16) == 96000 * 16);
	CHECK(map.tickAtOrBefore(24000) == 960);
	CHECK(map.tickAtOrBefore(23999) == 959);
	CHECK(map.millisecondsToTicks(500) == 960);
	CHECK_THAT(map.ticksToMilliseconds(960), WithinAbs(500.0, 1e-9));
}

TEST_CASE("Tempo map : tickAtOrBefore coherent avec samplePositionOf")
{
	for (double sr : { 44100.0, 48000.0, 96000.0 })
	{
		for (double bpm : { 60.0, 97.3, 127.0, 180.5 })
		{
			const auto map = make(sr, bpm, { 7, 8 });

			for (SamplePos s = -100000; s < 2000000; s += 997)
			{
				const Tick t = map.tickAtOrBefore(s);
				CHECK(map.samplePositionOf(t) <= s);
				CHECK(map.samplePositionOf(t + 1) > s);
			}
		}
	}
}

TEST_CASE("Tempo map : arrondi a moins d'un demi echantillon sur une heure")
{
	for (double sr : { 44100.0, 48000.0, 96000.0 })
	{
		const auto map = make(sr, 127.3);
		const Tick oneHour = static_cast<Tick>(std::ceil(map.exactTickAt(sr * 3600.0)));

		for (Tick t = 0; t <= oneHour; t += 3840)
		{
			const double exact = static_cast<double>(t) * sr * 60.0 / (127.3 * 960.0);
			CHECK(std::abs(static_cast<double>(map.samplePositionOf(t)) - exact) <= 0.5 + 1e-6);
		}
	}
}
