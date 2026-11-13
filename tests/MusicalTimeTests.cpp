#include <catch2/catch_test_macros.hpp>

#include "boohcleur/core/MusicalTime.h"

using namespace boohcleur;

TEST_CASE("Signature : valeurs valides et invalides")
{
	CHECK(TimeSignature::create(4, 4).has_value());
	CHECK(TimeSignature::create(7, 8).has_value());
	CHECK(TimeSignature::create(12, 16).has_value());
	CHECK_FALSE(TimeSignature::create(0, 4).has_value());
	CHECK_FALSE(TimeSignature::create(33, 4).has_value());
	CHECK_FALSE(TimeSignature::create(4, 3).has_value());
	CHECK_FALSE(TimeSignature::create(4, 64).has_value());
}

TEST_CASE("Signature : ticks par temps et par mesure")
{
	CHECK(TimeSignature { 4, 4 }.ticksPerBeat() == 960);
	CHECK(TimeSignature { 4, 4 }.ticksPerBar() == 3840);
	CHECK(TimeSignature { 3, 4 }.ticksPerBar() == 2880);
	CHECK(TimeSignature { 7, 8 }.ticksPerBeat() == 480);
	CHECK(TimeSignature { 7, 8 }.ticksPerBar() == 3360);
	CHECK(TimeSignature { 6, 8 }.ticksPerBar() == 2880);
}

TEST_CASE("Mesure, temps, tick : aller-retour")
{
	const TimeSignature sig { 7, 8 };

	for (Tick t = -20000; t <= 20000; t += 37)
		CHECK(toTick(toBarBeatTick(t, sig), sig) == t);

	CHECK(toBarBeatTick(0, sig) == BarBeatTick { 0, 0, 0 });
	CHECK(toBarBeatTick(3360 + 480 * 2 + 5, sig) == BarBeatTick { 1, 2, 5 });
	CHECK(toBarBeatTick(-1, sig) == BarBeatTick { -1, 6, 479 });
}

TEST_CASE("Division par le bas sur les negatifs")
{
	CHECK(floorDiv(7, 2) == 3);
	CHECK(floorDiv(-7, 2) == -4);
	CHECK(floorDiv(-8, 2) == -4);
	CHECK(floorDiv(0, 5) == 0);
}
