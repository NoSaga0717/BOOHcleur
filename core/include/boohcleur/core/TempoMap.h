#pragma once

#include "boohcleur/core/MusicalTime.h"

#include <optional>

namespace boohcleur
{
	// Correspondance entre temps musical (ticks) et temps audio (échantillons).
	//
	// V1 : tempo et signature constants sur tout le projet. L'interface est celle
	// d'une future tempo map à plusieurs segments : le reste du moteur ne doit
	// passer que par ces fonctions, jamais recalculer les conversions lui-même.
	//
	// Règle anti-dérive (ADR 0001, D5) : une position en échantillons se calcule
	// toujours depuis une position musicale absolue (samplePositionOf), jamais en
	// additionnant des longueurs arrondies.
	class TempoMap
	{
	public:
		static constexpr double minBpm = 20.0;
		static constexpr double maxBpm = 400.0;
		static constexpr double minSampleRate = 8000.0;
		static constexpr double maxSampleRate = 384000.0;

		// Rend nullopt si un paramètre est hors des bornes ci-dessus.
		static std::optional<TempoMap> create(double sampleRate, double bpm, TimeSignature signature) noexcept;

		double sampleRate() const noexcept { return sampleRate_; }
		double bpm() const noexcept { return bpm_; }
		const TimeSignature& signature() const noexcept { return signature_; }
		double samplesPerTick() const noexcept { return samplesPerTick_; }

		// Position exacte (non arrondie) d'un tick, en échantillons.
		double exactSamplePositionOf(Tick tick) const noexcept;

		// Position arrondie à l'échantillon le plus proche. Toujours la même valeur
		// pour un même tick : deux boucles qui partagent une frontière musicale
		// partagent aussi l'échantillon.
		SamplePos samplePositionOf(Tick tick) const noexcept;

		// Position musicale (fractionnaire) d'un échantillon.
		double exactTickAt(double samplePosition) const noexcept;

		// Dernier tick dont la position arrondie est <= samplePosition.
		Tick tickAtOrBefore(SamplePos samplePosition) const noexcept;

		// Durées, pour les réglages exprimés en millisecondes (tolérances, fondus).
		Tick millisecondsToTicks(double milliseconds) const noexcept;
		double ticksToMilliseconds(Tick ticks) const noexcept;

	private:
		TempoMap(double sampleRate, double bpm, TimeSignature signature) noexcept;

		double sampleRate_;
		double bpm_;
		TimeSignature signature_;
		double samplesPerTick_;
	};
}
