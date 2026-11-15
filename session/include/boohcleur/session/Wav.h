#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace boohcleur::session
{
	// WAV en flottants 32 bits (format 3, IEEE float), le format natif du
	// moteur : aucune perte à l'écriture ni à la relecture. Lisible par les
	// éditeurs audio courants.

	// Fichier complet en mémoire : channels[c] a frames échantillons.
	std::vector<std::uint8_t> encodeWav(const std::vector<const float*>& channels, std::int64_t frames, double sampleRate);

	struct WavData
	{
		double sampleRate = 0.0;
		std::vector<std::vector<float>> channels;	// [canal][image]
	};

	// false (et une raison) si ce n'est pas un WAV flottant 32 bits lisible.
	bool decodeWav(const std::vector<std::uint8_t>& bytes, WavData& out, std::string& reason);
}
