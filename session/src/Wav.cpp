#include "boohcleur/session/Wav.h"

#include <cstring>

namespace boohcleur::session
{
	namespace
	{
		void put16(std::vector<std::uint8_t>& out, std::uint32_t v)
		{
			out.push_back(static_cast<std::uint8_t>(v & 0xff));
			out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
		}

		void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
		{
			put16(out, v & 0xffff);
			put16(out, v >> 16);
		}

		void putTag(std::vector<std::uint8_t>& out, const char* tag)
		{
			out.insert(out.end(), tag, tag + 4);
		}

		std::uint32_t get16(const std::uint8_t* p) { return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8); }
		std::uint32_t get32(const std::uint8_t* p) { return get16(p) | (get16(p + 2) << 16); }

		float floatFromBits(std::uint32_t bits)
		{
			float value;
			std::memcpy(&value, &bits, sizeof value);
			return value;
		}

		std::uint32_t bitsFromFloat(float value)
		{
			std::uint32_t bits;
			std::memcpy(&bits, &value, sizeof bits);
			return bits;
		}
	}

	std::vector<std::uint8_t> encodeWav(const std::vector<const float*>& channels, std::int64_t frames, double sampleRate)
	{
		const auto numChannels = static_cast<std::uint32_t>(channels.size());
		const auto rate = static_cast<std::uint32_t>(sampleRate + 0.5);
		const auto dataBytes = static_cast<std::uint32_t>(frames) * numChannels * 4u;

		std::vector<std::uint8_t> out;
		out.reserve(58 + dataBytes);

		putTag(out, "RIFF");
		put32(out, 4 + (8 + 18) + (8 + 4) + (8 + dataBytes));
		putTag(out, "WAVE");

		putTag(out, "fmt ");
		put32(out, 18);
		put16(out, 3);							// WAVE_FORMAT_IEEE_FLOAT
		put16(out, numChannels);
		put32(out, rate);
		put32(out, rate * numChannels * 4u);	// octets par seconde
		put16(out, numChannels * 4u);			// octets par image
		put16(out, 32);
		put16(out, 0);							// pas d'extension

		putTag(out, "fact");
		put32(out, 4);
		put32(out, static_cast<std::uint32_t>(frames));

		putTag(out, "data");
		put32(out, dataBytes);

		for (std::int64_t i = 0; i < frames; ++i)
			for (const float* channel : channels)
				put32(out, bitsFromFloat(channel[i]));

		return out;
	}

	bool decodeWav(const std::vector<std::uint8_t>& bytes, WavData& out, std::string& reason)
	{
		if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
		{
			reason = "pas un fichier WAV";
			return false;
		}

		std::uint32_t format = 0, numChannels = 0, bits = 0, rate = 0;
		const std::uint8_t* data = nullptr;
		std::size_t dataSize = 0;
		std::size_t pos = 12;

		while (pos + 8 <= bytes.size())
		{
			const std::uint8_t* chunk = bytes.data() + pos;
			const std::size_t size = get32(chunk + 4);
			const std::size_t body = pos + 8;

			if (body + size > bytes.size())
			{
				reason = "fichier WAV tronqué";
				return false;
			}

			if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16)
			{
				format = get16(chunk + 8);
				numChannels = get16(chunk + 10);
				rate = get32(chunk + 12);
				bits = get16(chunk + 22);
			}
			else if (std::memcmp(chunk, "data", 4) == 0)
			{
				data = chunk + 8;
				dataSize = size;
			}

			pos = body + size + (size & 1);	// les blocs sont alignés sur 2 octets
		}

		if (format != 3 || bits != 32 || numChannels < 1 || numChannels > 2 || rate == 0)
		{
			reason = "format WAV inattendu (attendu : flottant 32 bits, 1 ou 2 canaux)";
			return false;
		}

		if (data == nullptr)
		{
			reason = "fichier WAV sans données";
			return false;
		}

		const std::size_t frames = dataSize / (numChannels * 4u);
		out.sampleRate = rate;
		out.channels.assign(numChannels, std::vector<float>(frames));

		for (std::size_t i = 0; i < frames; ++i)
			for (std::uint32_t c = 0; c < numChannels; ++c)
				out.channels[c][i] = floatFromBits(get32(data + (i * numChannels + c) * 4u));

		return true;
	}
}
