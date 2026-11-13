#pragma once

#include <cstdint>
#include <optional>

namespace boohcleur
{
	// Position musicale absolue, en ticks depuis le début du projet (tick 0 = début de la mesure 0).
	using Tick = std::int64_t;

	// Position audio absolue, en échantillons.
	using SamplePos = std::int64_t;

	// Résolution : ticks par noire. 960 divise exactement les doubles-croches (240),
	// les triolets de croches (320) et de doubles-croches (160).
	inline constexpr Tick ticksPerQuarter = 960;

	// Signature rythmique, par exemple 4/4, 3/4, 7/8.
	struct TimeSignature
	{
		int numerator = 4;
		int denominator = 4;

		// Numérateur de 1 à 32, dénominateur puissance de 2 de 1 à 32.
		static std::optional<TimeSignature> create(int numerator, int denominator) noexcept;

		constexpr Tick ticksPerBeat() const noexcept { return ticksPerQuarter * 4 / denominator; }
		constexpr Tick ticksPerBar() const noexcept { return ticksPerBeat() * numerator; }

		friend constexpr bool operator==(const TimeSignature&, const TimeSignature&) = default;
	};

	// Position lisible : mesure, temps et tick dans le temps, tous comptés à partir de 0.
	struct BarBeatTick
	{
		std::int64_t bar = 0;
		int beat = 0;
		Tick tick = 0;

		friend constexpr bool operator==(const BarBeatTick&, const BarBeatTick&) = default;
	};

	// Division par le bas (vers moins l'infini), correcte pour les positions négatives.
	constexpr std::int64_t floorDiv(std::int64_t a, std::int64_t b) noexcept
	{
		const std::int64_t q = a / b;
		return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
	}

	BarBeatTick toBarBeatTick(Tick tick, const TimeSignature& signature) noexcept;
	Tick toTick(const BarBeatTick& position, const TimeSignature& signature) noexcept;
}
