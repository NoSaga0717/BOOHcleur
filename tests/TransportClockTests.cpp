#include <catch2/catch_test_macros.hpp>

#include "boohcleur/core/TransportClock.h"

#include <cmath>

using namespace boohcleur;

namespace
{
	TempoMap make(double sr, double bpm, TimeSignature sig = { 4, 4 })
	{
		return *TempoMap::create(sr, bpm, sig);
	}
}

TEST_CASE("Transport : demarrage, avance, arret")
{
	TransportClock clock(make(48000, 120));

	CHECK_FALSE(clock.isPlaying());
	clock.advance(128);
	CHECK(clock.blockStart() == 0);	// à l'arrêt, l'horloge n'avance pas

	clock.start(3840);
	CHECK(clock.isPlaying());
	CHECK(clock.blockStart() == 96000);
	CHECK(clock.tickAtBlockStart() == 3840);

	clock.advance(128);
	CHECK(clock.blockStart() == 96128);

	clock.stop();
	clock.advance(128);
	CHECK(clock.blockStart() == 96128);
}

TEST_CASE("Transport : tempo verrouille pendant la lecture")
{
	TransportClock clock(make(48000, 120));
	clock.start(0);
	CHECK_FALSE(clock.setTempoMap(make(48000, 90)));
	CHECK(clock.tempoMap().bpm() == 120);
	clock.stop();
	CHECK(clock.setTempoMap(make(48000, 90)));
	CHECK(clock.tempoMap().bpm() == 90);
}

TEST_CASE("Transport : position d'un tick dans le bloc")
{
	TransportClock clock(make(48000, 120));
	clock.start(0);

	for (int i = 0; i < 749; ++i)
		clock.advance(128);	// 749 × 128 = 95872 ; la mesure 1 est à 96000

	CHECK(clock.offsetInBlock(3840, 256) == 128);
	CHECK_FALSE(clock.offsetInBlock(3840, 128).has_value());	// bloc [95872, 96000) : la frontière est juste après
	CHECK_FALSE(clock.offsetInBlock(0, 128).has_value());
}

TEST_CASE("Boucle : position dans le tour")
{
	const auto map = make(48000, 120);
	const LoopSpan loop { 3840, 4 * 3840 };	// 4 mesures à partir de la mesure 1

	auto p = locateInLoop(map, loop, 96000);
	CHECK(p.cycle == 0);
	CHECK(p.offset == 0);
	CHECK(p.cycleLength == 4 * 96000);

	p = locateInLoop(map, loop, 96000 + 4 * 96000 + 10);
	CHECK(p.cycle == 1);
	CHECK(p.offset == 10);

	p = locateInLoop(map, loop, 95999);
	CHECK(p.cycle == -1);
	CHECK(p.offset == 4 * 96000 - 1);
}

// Critère du jalon J1 : aucune dérive sur une heure simulée, à 44,1, 48 et 96 kHz,
// avec des blocs de 64, 128 et 512 échantillons.
// On simule le thread audio bloc par bloc avec deux boucles de 4 et 16 mesures.
// Contrôles :
// - continuité : d'un bloc à l'autre, la position dans chaque boucle avance
//   exactement de la taille du bloc (aucun échantillon sauté ni répété) ;
// - les frontières communes (toutes les 16 mesures) tombent sur le même échantillon ;
// - chaque frontière reste à moins d'un demi-échantillon de sa position exacte.
TEST_CASE("Derive : une heure simulee, boucles de 4 et 16 mesures")
{
	for (double sr : { 44100.0, 48000.0, 96000.0 })
	for (int blockSize : { 64, 128, 512 })
	{
		const double bpm = 127.3;	// durée de mesure non entière en échantillons
		const auto map = make(sr, bpm);
		const Tick bar = map.signature().ticksPerBar();

		const LoopSpan short4 { 0, 4 * bar };
		const LoopSpan long16 { 0, 16 * bar };

		TransportClock clock(map);
		clock.start(0);

		LoopPosition prev4 = locateInLoop(map, short4, 0);
		LoopPosition prev16 = locateInLoop(map, long16, 0);

		const SamplePos oneHour = static_cast<SamplePos>(sr * 3600.0);
		std::int64_t discontinuities = 0;
		std::int64_t misalignedBoundaries = 0;
		std::int64_t roundingErrors = 0;

		auto advanced = [blockSize](const LoopPosition& before, const LoopPosition& after)
		{
			if (after.cycle == before.cycle)
				return after.offset == before.offset + blockSize;

			return after.cycle == before.cycle + 1
				&& after.offset == before.offset + blockSize - before.cycleLength;
		};

		while (clock.blockStart() < oneHour)
		{
			clock.advance(blockSize);

			const auto p4 = locateInLoop(map, short4, clock.blockStart());
			const auto p16 = locateInLoop(map, long16, clock.blockStart());

			if (!advanced(prev4, p4) || !advanced(prev16, p16))
				++discontinuities;

			if (p16.cycle != prev16.cycle)
			{
				// Le tour de 16 mesures commence en même temps qu'un tour de 4.
				if (locateInLoop(map, short4, p16.cycleStart).offset != 0)
					++misalignedBoundaries;

				const double exact = static_cast<double>(p16.cycle * long16.length) * map.samplesPerTick();
				if (std::abs(static_cast<double>(p16.cycleStart) - exact) > 0.5 + 1e-6)
					++roundingErrors;
			}

			prev4 = p4;
			prev16 = p16;
		}

		INFO("Frequence : " << sr << ", bloc : " << blockSize);
		CHECK(discontinuities == 0);
		CHECK(misalignedBoundaries == 0);
		CHECK(roundingErrors == 0);
		CHECK(prev16.cycle > 0);
	}
}
