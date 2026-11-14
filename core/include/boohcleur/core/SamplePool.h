#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace boohcleur
{
	// Réserve d'audio partagée par toutes les boucles (ADR 0001, budget RAM).
	//
	// La mémoire est réservée une fois, hors temps réel, puis découpée en blocs
	// de taille fixe (chunkFrames images × numChannels). Les couches prennent
	// des blocs au fur et à mesure qu'elles s'allongent et les rendent quand
	// elles sont effacées : six boucles se partagent le budget au lieu de
	// réserver chacune leur longueur maximale.
	//
	// acquire() et release() sont temps réel et appelés par le seul thread audio.
	// La mémoire n'est pas mise à zéro : le système ne la fournit vraiment
	// qu'au premier accès, ce qui garde l'empreinte proche de l'usage réel.
	class SamplePool
	{
	public:
		static constexpr int defaultChunkFrames = 16384;	// 0,34 s à 48 kHz

		// Rend false si le budget ne permet pas un seul bloc.
		bool prepare(int numChannels, std::size_t budgetBytes, int chunkFrames = defaultChunkFrames);

		int numChannels() const noexcept { return numChannels_; }
		int chunkFrames() const noexcept { return chunkFrames_; }
		int numChunks() const noexcept { return static_cast<int>(free_.size()); }
		int usedChunks() const noexcept { return numChunks() - numFree_; }
		int freeChunks() const noexcept { return numFree_; }
		std::size_t bytes() const noexcept;

		// Bloc libre, ou -1 si la réserve est vide.
		int acquire() noexcept;
		void release(int chunk) noexcept;

		float* channel(int chunk, int c) noexcept
		{
			return data_.get() + (static_cast<std::size_t>(chunk) * static_cast<std::size_t>(numChannels_)
				+ static_cast<std::size_t>(c)) * static_cast<std::size_t>(chunkFrames_);
		}

		const float* channel(int chunk, int c) const noexcept
		{
			return const_cast<SamplePool*>(this)->channel(chunk, c);
		}

	private:
		std::unique_ptr<float[]> data_;
		std::vector<int> free_;		// pile des blocs libres
		int numFree_ = 0;
		int numChannels_ = 2;
		int chunkFrames_ = defaultChunkFrames;
	};
}
