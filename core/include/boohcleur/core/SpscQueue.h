#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace boohcleur
{
	// File sans verrou à un seul producteur et un seul consommateur, de capacité fixe.
	// Aucune allocation : utilisable depuis le thread audio dans les deux sens.
	template <typename T, std::size_t Capacity>
	class SpscQueue
	{
		static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacité : puissance de 2");

	public:
		// Producteur. Rend false si la file est pleine (l'élément est perdu).
		bool push(const T& item) noexcept
		{
			const std::size_t tail = tail_.load(std::memory_order_relaxed);
			const std::size_t next = (tail + 1) & (Capacity - 1);

			if (next == head_.load(std::memory_order_acquire))
				return false;

			items_[tail] = item;
			tail_.store(next, std::memory_order_release);
			return true;
		}

		// Consommateur.
		std::optional<T> pop() noexcept
		{
			const std::size_t head = head_.load(std::memory_order_relaxed);

			if (head == tail_.load(std::memory_order_acquire))
				return std::nullopt;

			T item = items_[head];
			head_.store((head + 1) & (Capacity - 1), std::memory_order_release);
			return item;
		}

	private:
		std::array<T, Capacity> items_ {};
		alignas(64) std::atomic<std::size_t> head_ { 0 };
		alignas(64) std::atomic<std::size_t> tail_ { 0 };
	};
}
