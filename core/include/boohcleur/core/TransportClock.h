#pragma once

#include "boohcleur/core/TempoMap.h"

#include <cstdint>
#include <optional>

namespace boohcleur
{
	// Horloge de transport du moteur, avancée par le thread audio bloc par bloc.
	// Aucune allocation, aucune exception : utilisable dans process().
	//
	// L'échantillon 0 de la timeline correspond au tick 0. Standalone : horloge
	// interne. Plugin : la position sera recalée sur celle de l'hôte (jalon J6).
	class TransportClock
	{
	public:
		explicit TransportClock(const TempoMap& tempoMap) noexcept;

		const TempoMap& tempoMap() const noexcept { return tempoMap_; }

		// V1 : tempo verrouillé pendant la lecture (arbitrage 5 de l'ADR 0001).
		// Rend false et ne change rien si le transport tourne.
		bool setTempoMap(const TempoMap& tempoMap) noexcept;

		bool isPlaying() const noexcept { return playing_; }

		// Démarre la lecture à la position musicale donnée.
		void start(Tick fromTick) noexcept;
		void stop() noexcept;

		// Position du premier échantillon du bloc en cours.
		SamplePos blockStart() const noexcept { return blockStart_; }
		Tick tickAtBlockStart() const noexcept { return tempoMap_.tickAtOrBefore(blockStart_); }

		// Si le tick tombe dans le bloc [blockStart, blockStart + blockSize),
		// rend sa position dans le bloc.
		std::optional<int> offsetInBlock(Tick tick, int blockSize) const noexcept;

		// Fin du bloc : avance de blockSize échantillons si le transport tourne.
		void advance(int blockSize) noexcept;

	private:
		TempoMap tempoMap_;
		SamplePos blockStart_ = 0;
		bool playing_ = false;
	};

	// Étendue musicale d'une boucle : début (ancre) et longueur, en ticks.
	struct LoopSpan
	{
		Tick anchor = 0;
		Tick length = 0;	// > 0
	};

	// Où en est une boucle à un échantillon donné.
	struct LoopPosition
	{
		std::int64_t cycle = 0;			// numéro du tour (négatif avant l'ancre)
		SamplePos cycleStart = 0;		// premier échantillon de ce tour
		SamplePos cycleLength = 0;		// longueur de ce tour, en échantillons
		SamplePos offset = 0;			// position dans le tour, 0 <= offset < cycleLength
	};

	// Les frontières de tour sont calculées depuis la position musicale absolue
	// (ADR 0001, D5) : aucune dérive, même après des heures, et deux boucles de
	// longueurs différentes qui partagent une frontière musicale tombent sur le
	// même échantillon. La longueur d'un tour peut varier d'un échantillon d'un
	// tour à l'autre quand la durée exacte n'est pas entière.
	LoopPosition locateInLoop(const TempoMap& tempoMap, const LoopSpan& loop, SamplePos sample) noexcept;
}
