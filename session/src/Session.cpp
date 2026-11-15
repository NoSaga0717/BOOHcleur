#include "boohcleur/session/Session.h"
#include "boohcleur/session/Wav.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

namespace boohcleur::session
{
	namespace fs = std::filesystem;
	using json = nlohmann::json;

	namespace
	{
		constexpr const char* manifestName = "session.json";
		constexpr const char* formatName = "boohcleur-session";

		std::string fnv1a(const std::uint8_t* data, std::size_t size)
		{
			std::uint64_t hash = 14695981039346656037ull;

			for (std::size_t i = 0; i < size; ++i)
			{
				hash ^= data[i];
				hash *= 1099511628211ull;
			}

			char text[32];
			std::snprintf(text, sizeof text, "fnv1a64:%016llx", static_cast<unsigned long long>(hash));
			return text;
		}

		std::string fnv1a(const std::string& text)
		{
			return fnv1a(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
		}

		bool writeFile(const fs::path& path, const std::uint8_t* data, std::size_t size)
		{
			std::ofstream out(path, std::ios::binary | std::ios::trunc);

			if (!out)
				return false;

			out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
			out.flush();
			return static_cast<bool>(out);
		}

		bool readFile(const fs::path& path, std::vector<std::uint8_t>& out)
		{
			std::ifstream in(path, std::ios::binary);

			if (!in)
				return false;

			out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			return !in.bad();
		}

		fs::path fromUtf8(const std::string& text)
		{
			return fs::path(std::u8string(text.begin(), text.end()));
		}

		std::string utf8(const fs::path& path)
		{
			const auto text = path.u8string();
			return std::string(text.begin(), text.end());
		}

		std::string layerFileName(int loop, int layer)
		{
			return "audio/boucle" + std::to_string(loop + 1) + "-couche" + std::to_string(layer + 1) + ".wav";
		}

		bool isPlainName(const std::string& name)
		{
			return !name.empty() && name.find_first_of("/\\:") == std::string::npos && name != "." && name != ".."
				&& name != manifestName;
		}

		// Lit le manifeste et tous les fichiers d'un dossier de session.
		Result loadFolder(const fs::path& folder, Session& out)
		{
			std::vector<std::uint8_t> bytes;

			if (!readFile(folder / manifestName, bytes))
				return Result::failure("Pas une session BOOHcleur : « session.json » est introuvable.");

			json manifest;

			try
			{
				manifest = json::parse(bytes.begin(), bytes.end());
			}
			catch (const json::exception&)
			{
				return Result::failure("Le manifeste « session.json » est illisible (fichier abîmé).");
			}

			try
			{
				if (manifest.value("format", std::string {}) != formatName)
					return Result::failure("Ce dossier n'est pas une session BOOHcleur.");

				const int version = manifest.at("version").get<int>();

				if (version > formatVersion)
					return Result::failure("Session écrite par une version plus récente de BOOHcleur (format "
						+ std::to_string(version) + ") : mettre l'application à jour pour l'ouvrir.");

				Session session;
				session.application = manifest.value("application", std::string {});
				session.sampleRate = manifest.at("sampleRate").get<double>();
				session.numChannels = manifest.at("channels").get<int>();
				const auto& tempo = manifest.at("tempo");
				session.bpm = tempo.at("bpm").get<double>();
				session.signature.numerator = tempo.at("numerator").get<int>();
				session.signature.denominator = tempo.at("denominator").get<int>();

				if (session.numChannels < 1 || session.numChannels > 2 || session.sampleRate <= 0.0)
					return Result::failure("Le manifeste est incohérent (canaux ou fréquence).");

				for (const auto& item : manifest.at("loops"))
				{
					Loop loop;
					loop.index = item.at("index").get<int>();
					loop.playing = item.value("playing", false);
					loop.span.anchor = item.at("anchorTick").get<Tick>();
					loop.span.length = item.at("lengthTicks").get<Tick>();
					loop.loopLength = item.at("lengthSamples").get<SamplePos>();
					loop.numActive = item.at("activeLayers").get<int>();

					for (const auto& entry : item.at("layers"))
					{
						Layer layer;
						layer.startOffset = entry.at("startOffset").get<SamplePos>();
						layer.count = entry.at("samples").get<SamplePos>();
						const auto name = entry.at("file").get<std::string>();
						const auto expected = entry.at("checksum").get<std::string>();

						std::vector<std::uint8_t> audio;

						if (name.find("..") != std::string::npos || !readFile(folder / fromUtf8(name), audio))
							return Result::failure("Fichier audio manquant : « " + name + " » (boucle "
								+ std::to_string(loop.index + 1) + ").");

						if (fnv1a(audio.data(), audio.size()) != expected)
							return Result::failure("Fichier audio abîmé : « " + name + " » (empreinte différente).");

						WavData wav;
						std::string reason;

						if (!decodeWav(audio, wav, reason))
							return Result::failure("« " + name + " » : " + reason + ".");

						if (static_cast<SamplePos>(wav.channels.front().size()) < layer.count)
							return Result::failure("« " + name + " » est plus court que prévu.");

						for (auto& channel : wav.channels)
							channel.resize(static_cast<std::size_t>(layer.count));

						layer.audio = std::move(wav.channels);
						loop.layers.push_back(std::move(layer));
					}

					session.loops.push_back(std::move(loop));
				}

				for (const auto& entry : manifest.value("files", json::array()))
				{
					ExtraFile file;
					file.name = entry.at("name").get<std::string>();

					if (!isPlainName(file.name))
						continue;

					std::vector<std::uint8_t> content;

					if (!readFile(folder / fromUtf8(file.name), content))
						return Result::failure("Fichier manquant : « " + file.name + " ».");

					if (fnv1a(content.data(), content.size()) != entry.at("checksum").get<std::string>())
						return Result::failure("Fichier abîmé : « " + file.name + " » (empreinte différente).");

					file.content.assign(content.begin(), content.end());
					session.files.push_back(std::move(file));
				}

				out = std::move(session);
				return {};
			}
			catch (const json::exception&)
			{
				return Result::failure("Le manifeste « session.json » est incomplet ou d'un format inattendu.");
			}
		}
	}

	fs::path withExtension(fs::path folder)
	{
		if (folder.extension() != extension)
			folder += extension;

		return folder;
	}

	Result save(const fs::path& requested, const Session& session, const AudioReader& reader)
	{
		const fs::path folder = withExtension(requested);
		fs::path temporary = folder;
		temporary += ".tmp";
		fs::path previous = folder;
		previous += ".old";

		std::error_code error;
		fs::remove_all(temporary, error);
		fs::create_directories(temporary / "audio", error);

		if (error)
			return Result::failure("Impossible de créer « " + utf8(temporary) + " » : " + error.message());

		const auto fail = [&](const std::string& text)
		{
			std::error_code ignored;
			fs::remove_all(temporary, ignored);
			return Result::failure(text);
		};

		json manifest;
		manifest["format"] = formatName;
		manifest["version"] = formatVersion;
		manifest["application"] = session.application;
		manifest["sampleRate"] = session.sampleRate;
		manifest["channels"] = session.numChannels;
		manifest["tempo"] = { { "bpm", session.bpm }, { "numerator", session.signature.numerator },
			{ "denominator", session.signature.denominator } };
		manifest["loops"] = json::array();

		std::vector<std::vector<float>> buffers(static_cast<std::size_t>(session.numChannels));

		for (const auto& loop : session.loops)
		{
			json item;
			item["index"] = loop.index;
			item["playing"] = loop.playing;
			item["anchorTick"] = loop.span.anchor;
			item["lengthTicks"] = loop.span.length;
			item["lengthSamples"] = loop.loopLength;
			item["activeLayers"] = loop.numActive;
			item["layers"] = json::array();

			for (std::size_t l = 0; l < loop.layers.size(); ++l)
			{
				const auto& layer = loop.layers[l];
				const auto name = layerFileName(loop.index, static_cast<int>(l));
				std::vector<const float*> channels;

				for (int c = 0; c < session.numChannels; ++c)
				{
					auto& buffer = buffers[static_cast<std::size_t>(c)];
					buffer.assign(static_cast<std::size_t>(layer.count), 0.0f);

					if (reader && layer.count > 0)
						reader(loop.index, static_cast<int>(l), c, 0, static_cast<int>(layer.count), buffer.data());

					channels.push_back(buffer.data());
				}

				const auto wav = encodeWav(channels, layer.count, session.sampleRate);

				if (!writeFile(temporary / fromUtf8(name), wav.data(), wav.size()))
					return fail("Écriture impossible : « " + name + " » (disque plein ou dossier protégé ?).");

				item["layers"].push_back({ { "file", name }, { "startOffset", layer.startOffset },
					{ "samples", layer.count }, { "checksum", fnv1a(wav.data(), wav.size()) } });
			}

			manifest["loops"].push_back(std::move(item));
		}

		manifest["files"] = json::array();

		for (const auto& file : session.files)
		{
			if (!isPlainName(file.name))
				return fail("Nom de fichier refusé dans une session : « " + file.name + " ».");

			if (!writeFile(temporary / fromUtf8(file.name), reinterpret_cast<const std::uint8_t*>(file.content.data()), file.content.size()))
				return fail("Écriture impossible : « " + file.name + " ».");

			manifest["files"].push_back({ { "name", file.name }, { "checksum", fnv1a(file.content) } });
		}

		// Le manifeste en dernier : sa présence dit que le dossier est complet.
		const auto text = manifest.dump(2);

		if (!writeFile(temporary / manifestName, reinterpret_cast<const std::uint8_t*>(text.data()), text.size()))
			return fail("Écriture impossible : « session.json ».");

		// Remplacement : ancienne version mise de côté, nouvelle à sa place.
		fs::remove_all(previous, error);
		const bool hadPrevious = fs::exists(folder, error);

		if (hadPrevious)
		{
			fs::rename(folder, previous, error);

			if (error)
				return fail("Impossible de remplacer « " + utf8(folder) + " » : " + error.message());
		}

		fs::rename(temporary, folder, error);

		if (error)
		{
			std::error_code ignored;

			if (hadPrevious)
				fs::rename(previous, folder, ignored);

			return fail("Impossible de finaliser « " + utf8(folder) + " » : " + error.message());
		}

		fs::remove_all(previous, error);
		return {};
	}

	Result load(const fs::path& requested, Session& out)
	{
		fs::path folder = requested;

		// Un clic sur le manifeste lui-même ouvre son dossier.
		if (folder.filename() == manifestName)
			folder = folder.parent_path();

		fs::path previous = folder;
		previous += ".old";
		std::error_code error;

		Result result = fs::exists(folder, error) ? loadFolder(folder, out)
			: Result::failure("Session introuvable : « " + utf8(folder) + " ».");

		// Coupure pendant un remplacement : la version précédente est complète.
		if (!result.ok && fs::exists(previous / manifestName, error))
		{
			Result fallback = loadFolder(previous, out);

			if (fallback.ok)
			{
				fallback.recovered = true;
				fallback.message = "Session récupérée depuis la version précédente (la dernière sauvegarde était "
					"incomplète : " + result.message + ")";
				return fallback;
			}
		}

		return result;
	}

	Session fromSnapshot(const ContentSnapshot& snapshot)
	{
		Session session;
		session.sampleRate = snapshot.sampleRate;
		session.numChannels = snapshot.numChannels;
		session.bpm = snapshot.bpm;
		session.signature = snapshot.signature;

		for (std::size_t t = 0; t < snapshot.tracks.size(); ++t)
		{
			const auto& track = snapshot.tracks[t];

			if (track.numLayers <= 0)
				continue;

			Loop loop;
			loop.index = static_cast<int>(t);
			loop.playing = track.playing;
			loop.span = track.span;
			loop.loopLength = track.loopLength;
			loop.numActive = track.numActive;

			for (int l = 0; l < track.numLayers; ++l)
			{
				Layer layer;
				layer.startOffset = track.layers[static_cast<std::size_t>(l)].startOffset;
				layer.count = track.layers[static_cast<std::size_t>(l)].count;
				loop.layers.push_back(std::move(layer));
			}

			session.loops.push_back(std::move(loop));
		}

		return session;
	}

	AudioReader snapshotReader(const Engine& engine)
	{
		return [&engine](int loop, int layer, int channel, SamplePos start, int count, float* dest)
		{
			engine.readSnapshot(loop, layer, channel, start, count, dest);
		};
	}

	bool toRestoreRequest(const Session& session, const EngineConfig& config, RestoreRequest& request,
		std::vector<std::vector<const float*>>& pointers, std::string& reason)
	{
		if (std::abs(session.sampleRate - config.sampleRate) > 0.5)
		{
			reason = "Session enregistrée à " + std::to_string(static_cast<int>(session.sampleRate + 0.5))
				+ " Hz, carte son réglée à " + std::to_string(static_cast<int>(config.sampleRate + 0.5))
				+ " Hz : régler la carte son sur la fréquence de la session pour l'ouvrir.";
			return false;
		}

		request = {};
		request.bpm = session.bpm;
		request.signature = session.signature;
		request.tracks.assign(static_cast<std::size_t>(config.numTracks), RestoreTrack {});

		std::size_t totalLayers = 0;

		for (const auto& loop : session.loops)
			totalLayers += loop.layers.size();

		// Réservé d'avance : les pointeurs vers chaque couche restent valides.
		pointers.clear();
		pointers.reserve(totalLayers);

		for (const auto& loop : session.loops)
		{
			if (loop.index < 0 || loop.index >= config.numTracks)
			{
				reason = "La session utilise la boucle " + std::to_string(loop.index + 1) + ", cette version n'en a que "
					+ std::to_string(config.numTracks) + ".";
				return false;
			}

			auto& track = request.tracks[static_cast<std::size_t>(loop.index)];
			track.playing = loop.playing;
			track.span = loop.span;
			track.loopLength = loop.loopLength;
			track.numActive = loop.numActive;

			for (const auto& layer : loop.layers)
			{
				std::vector<const float*> channels;

				for (int c = 0; c < config.numChannels; ++c)
				{
					const auto source = std::min<std::size_t>(static_cast<std::size_t>(c), layer.audio.size() - 1);
					channels.push_back(layer.audio.empty() ? nullptr : layer.audio[source].data());
				}

				pointers.push_back(std::move(channels));

				LoopTrack::RestoredLayer restored;
				restored.startOffset = layer.startOffset;
				restored.count = layer.audio.empty() ? 0 : layer.count;
				restored.channels = pointers.back().data();
				track.layers.push_back(restored);
			}
		}

		return true;
	}

	std::string describe(RestoreResult result)
	{
		switch (result)
		{
		case RestoreResult::Ok:				return "Session chargée.";
		case RestoreResult::NotPrepared:	return "Le moteur audio n'est pas prêt : ouvrir la carte son puis recommencer.";
		case RestoreResult::InvalidTempo:	return "Tempo ou signature de la session invalides.";
		case RestoreResult::TooManyTracks:	return "La session a plus de boucles que cette version.";
		case RestoreResult::InvalidLoop:	return "Une boucle de la session est trop longue ou ne correspond pas à son tempo.";
		case RestoreResult::OutOfMemory:	return "Mémoire des boucles insuffisante pour cette session.";
		}

		return {};
	}
}
