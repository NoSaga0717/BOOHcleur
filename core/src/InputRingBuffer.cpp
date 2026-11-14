#include "boohcleur/core/InputRingBuffer.h"

namespace boohcleur
{
	void InputRingBuffer::prepare(int numChannels, std::int64_t capacityFrames)
	{
		channels_.assign(static_cast<std::size_t>(numChannels),
			std::vector<float>(static_cast<std::size_t>(capacityFrames), 0.0f));
		capacity_ = capacityFrames;
		written_ = 0;
	}

	void InputRingBuffer::write(const float* const* inputs, int numInputs, int numFrames) noexcept
	{
		const int channels = numChannels();

		for (int c = 0; c < channels; ++c)
		{
			const int source = numInputs == 1 ? 0 : c;
			const float* in = (inputs != nullptr && source < numInputs) ? inputs[source] : nullptr;
			auto& dest = channels_[static_cast<std::size_t>(c)];

			std::int64_t index = written_ % capacity_;

			for (int i = 0; i < numFrames; ++i)
			{
				dest[static_cast<std::size_t>(index)] = in != nullptr ? in[i] : 0.0f;

				if (++index == capacity_)
					index = 0;
			}
		}

		written_ += numFrames;
	}

	bool InputRingBuffer::read(std::int64_t start, int numFrames, float* const* outputs) const noexcept
	{
		if (!isAvailable(start, numFrames))
			return false;

		for (int c = 0; c < numChannels(); ++c)
		{
			const auto& source = channels_[static_cast<std::size_t>(c)];
			std::int64_t index = start % capacity_;

			for (int i = 0; i < numFrames; ++i)
			{
				outputs[c][i] = source[static_cast<std::size_t>(index)];

				if (++index == capacity_)
					index = 0;
			}
		}

		return true;
	}
}
