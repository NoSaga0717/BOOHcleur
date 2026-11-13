#include <catch2/catch_test_macros.hpp>

#include "boohcleur/core/Grid.h"

using namespace boohcleur;

TEST_CASE("Grille : longueurs des divisions en 4/4")
{
	const TimeSignature sig { 4, 4 };
	CHECK(divisionLength(GridDivision::Immediate, sig) == 0);
	CHECK(divisionLength(GridDivision::Bar, sig) == 3840);
	CHECK(divisionLength(GridDivision::Beat, sig) == 960);
	CHECK(divisionLength(GridDivision::Eighth, sig) == 480);
	CHECK(divisionLength(GridDivision::Sixteenth, sig) == 240);
	CHECK(divisionLength(GridDivision::QuarterTriplet, sig) == 640);
	CHECK(divisionLength(GridDivision::EighthTriplet, sig) == 320);
	CHECK(divisionLength(GridDivision::SixteenthTriplet, sig) == 160);
}

TEST_CASE("Grille : frontieres avant et apres")
{
	const TimeSignature sig { 4, 4 };

	CHECK(boundaryAtOrBefore(3840, GridDivision::Bar, sig) == 3840);
	CHECK(boundaryAtOrBefore(3839, GridDivision::Bar, sig) == 0);
	CHECK(boundaryAfter(3840, GridDivision::Bar, sig) == 7680);
	CHECK(boundaryAfter(0, GridDivision::Beat, sig) == 960);
	CHECK(boundaryAtOrBefore(-1, GridDivision::Bar, sig) == -3840);
	CHECK(boundaryAfter(-1, GridDivision::Bar, sig) == 0);
}

TEST_CASE("Grille : la division repart de chaque mesure en 7/8")
{
	// Noires en 7/8 (3360 ticks) : 0, 960, 1920, 2880, puis mesure suivante à 3360.
	const TimeSignature sig { 7, 8 };

	CHECK(boundaryAfter(2880, GridDivision::QuarterTriplet, sig) == 3200);
	CHECK(boundaryAfter(3200, GridDivision::QuarterTriplet, sig) == 3360);
	CHECK(boundaryAtOrBefore(3300, GridDivision::QuarterTriplet, sig) == 3200);

	// Beat en 7/8 = croche (480).
	CHECK(boundaryAfter(3000, GridDivision::Beat, sig) == 3360);
	CHECK(boundaryAtOrBefore(3359, GridDivision::Beat, sig) == 2880);
}

TEST_CASE("Regle de declenchement : avant, sur et apres la frontiere")
{
	const TimeSignature sig { 4, 4 };
	const Tick tolerance = 96;	// environ 50 ms à 120 BPM

	SECTION("Exactement sur la frontiere")
	{
		const auto r = resolveTrigger(3840, GridDivision::Bar, sig, tolerance);
		CHECK(r.target == 3840);
		CHECK_FALSE(r.retroactive);
	}
	SECTION("Juste apres, dans la tolerance : recadre en arriere")
	{
		const auto r = resolveTrigger(3840 + 50, GridDivision::Bar, sig, tolerance);
		CHECK(r.target == 3840);
		CHECK(r.retroactive);
	}
	SECTION("Pile a la limite de la tolerance : encore recadre")
	{
		const auto r = resolveTrigger(3840 + tolerance, GridDivision::Bar, sig, tolerance);
		CHECK(r.target == 3840);
		CHECK(r.retroactive);
	}
	SECTION("Apres la tolerance : frontiere suivante")
	{
		const auto r = resolveTrigger(3840 + tolerance + 1, GridDivision::Bar, sig, tolerance);
		CHECK(r.target == 7680);
		CHECK_FALSE(r.retroactive);
	}
	SECTION("Juste avant la frontiere : frontiere suivante")
	{
		const auto r = resolveTrigger(3840 - 10, GridDivision::Bar, sig, tolerance);
		CHECK(r.target == 3840);
		CHECK_FALSE(r.retroactive);
	}
	SECTION("Immediat : au tick de l'appui")
	{
		const auto r = resolveTrigger(1234, GridDivision::Immediate, sig, tolerance);
		CHECK(r.target == 1234);
		CHECK_FALSE(r.retroactive);
	}
	SECTION("Tolerance nulle ou negative : jamais retroactif")
	{
		CHECK(resolveTrigger(3841, GridDivision::Bar, sig, 0).target == 7680);
		CHECK(resolveTrigger(3841, GridDivision::Bar, sig, -5).target == 7680);
	}
}
