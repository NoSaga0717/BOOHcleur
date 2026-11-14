#pragma once

#include <cstdint>
#include <vector>

namespace boohcleur
{
	// Tampon circulaire d'entrée toujours actif (ADR 0001, D6).
	//
	// Chaque trame reçue de la carte son est rangée sous son index absolu
	// (compteur d'échantillons de l'appareil, qui ne s'arrête jamais). On peut
	// relire n'importe quelle plage encore présente : c'est ce qui permet de
	// commencer un enregistrement un peu dans le passé (pression tardive) et de
	// compenser la latence aller-retour.
	class InputRingBuffer
	{
	public:
		// Hors temps réel : alloue capacityFrames trames par canal.
		void prepare(int numChannels, std::int64_t capacityFrames);

		int numChannels() const noexcept { return static_cast<int>(channels_.size()); }
		std::int64_t capacity() const noexcept { return capacity_; }

		// Index absolu de la prochaine trame écrite.
		std::int64_t writePosition() const noexcept { return written_; }

		// Plus ancienne trame encore disponible.
		std::int64_t oldestAvailable() const noexcept { return written_ > capacity_ ? written_ - capacity_ : 0; }

		// Temps réel. inputs[c] peut être nul (canal absent) : on écrit alors des zéros.
		// Si numInputs vaut 1 et que le tampon est stéréo, le canal est dupliqué.
		void write(const float* const* inputs, int numInputs, int numFrames) noexcept;

		// Temps réel. Copie [start, start + numFrames) dans outputs[c] (un pointeur
		// par canal du tampon). Rend false, sans rien copier, si une partie de la
		// plage n'est pas (ou plus) disponible.
		bool read(std::int64_t start, int numFrames, float* const* outputs) const noexcept;

		// Lecture d'une trame, sans contrôle de disponibilité (à vérifier avant).
		float sample(int channel, std::int64_t index) const noexcept
		{
			return channels_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(index % capacity_)];
		}

		bool isAvailable(std::int64_t start, std::int64_t numFrames) const noexcept
		{
			return start >= oldestAvailable() && start + numFrames <= written_;
		}

	private:
		std::vector<std::vector<float>> channels_;
		std::int64_t capacity_ = 0;
		std::int64_t written_ = 0;
	};
}
