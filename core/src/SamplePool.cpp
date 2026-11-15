#include "boohcleur/core/SamplePool.h"

namespace boohcleur
{
	bool SamplePool::prepare(int numChannels, std::size_t budgetBytes, int chunkFrames)
	{
		if (numChannels < 1 || chunkFrames < 1)
			return false;

		const std::size_t chunkBytes = static_cast<std::size_t>(numChannels) * static_cast<std::size_t>(chunkFrames) * sizeof(float);
		const std::size_t count = budgetBytes / chunkBytes;

		if (count == 0 || count > static_cast<std::size_t>(INT32_MAX))
			return false;

		numChannels_ = numChannels;
		chunkFrames_ = chunkFrames;

		// new float[] sans () : pas d'initialisation, pages fournies à l'usage.
		data_.reset(new float[count * static_cast<std::size_t>(numChannels) * static_cast<std::size_t>(chunkFrames)]);
		free_.resize(count);

		// Les premiers blocs sortent en premier : la mémoire utilisée reste groupée.
		for (std::size_t i = 0; i < count; ++i)
			free_[i] = static_cast<int>(count - 1 - i);

		numFree_ = static_cast<int>(count);
		deferred_.assign(count, -1);
		numDeferred_ = 0;
		pinned_ = false;
		return true;
	}

	std::size_t SamplePool::bytes() const noexcept
	{
		return free_.size() * static_cast<std::size_t>(numChannels_) * static_cast<std::size_t>(chunkFrames_) * sizeof(float);
	}

	int SamplePool::acquire() noexcept
	{
		if (numFree_ == 0)
			return -1;

		return free_[static_cast<std::size_t>(--numFree_)];
	}

	void SamplePool::release(int chunk) noexcept
	{
		if (chunk < 0)
			return;

		if (pinned_)
		{
			if (numDeferred_ < static_cast<int>(deferred_.size()))
				deferred_[static_cast<std::size_t>(numDeferred_++)] = chunk;
		}
		else if (numFree_ < static_cast<int>(free_.size()))
		{
			free_[static_cast<std::size_t>(numFree_++)] = chunk;
		}
	}

	void SamplePool::unpin() noexcept
	{
		pinned_ = false;

		while (numDeferred_ > 0)
			release(deferred_[static_cast<std::size_t>(--numDeferred_)]);
	}
}
