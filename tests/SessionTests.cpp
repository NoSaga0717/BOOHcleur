#include <catch2/catch_test_macros.hpp>

#include "boohcleur/core/Engine.h"
#include "boohcleur/core/SamplePool.h"
#include "boohcleur/session/Session.h"
#include "boohcleur/session/Wav.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <vector>

using namespace boohcleur;
namespace fs = std::filesystem;

namespace
{
	constexpr double sampleRate = 48000.0;
	constexpr int blockSize = 128;
	constexpr SamplePos bar = 96000;

	struct Rig
	{
		Engine engine;
		std::function<float(std::int64_t)> input = [](std::int64_t) { return 0.0f; };
		std::multimap<std::int64_t, Command> commands;
		std::vector<float> left;
		std::int64_t sample = 0;

		explicit Rig(EngineConfig config = {})
		{
			config.sampleRate = sampleRate;
			config.maxBlockSize = blockSize;
			config.memoryBudgetBytes = std::size_t { 64 } << 20;
			REQUIRE(engine.prepare(config));
			engine.setMetronomeEnabled(false);
		}

		void at(SamplePos when, CommandType type, int track = 0)
		{
			commands.insert({ when, Command { type, track, 0.0 } });
		}

		void run(SamplePos until)
		{
			std::vector<float> in(blockSize), outL(blockSize), outR(blockSize);

			for (; sample < until; sample += blockSize)
			{
				for (auto it = commands.lower_bound(sample); it != commands.end() && it->first < sample + blockSize; ++it)
					REQUIRE(engine.post(it->second));

				for (int i = 0; i < blockSize; ++i)
					in[static_cast<std::size_t>(i)] = input(sample + i);

				const float* ins[1] = { in.data() };
				float* outs[2] = { outL.data(), outR.data() };
				engine.process(ins, 1, outs, 2, blockSize);
				left.insert(left.end(), outL.begin(), outL.end());
			}
		}

		float out(SamplePos i) const { return left[static_cast<std::size_t>(i)]; }
	};

	// Dossier temporaire effacé à la fin du test.
	struct TempFolder
	{
		fs::path path;

		TempFolder()
		{
			const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
			path = fs::temp_directory_path() / ("boohcleur-tests-" + std::to_string(stamp));
			fs::create_directories(path);
		}

		~TempFolder()
		{
			std::error_code ignored;
			fs::remove_all(path, ignored);
		}
	};

	// Scénario V1-2 du cahier des charges : une boucle, trois overdubs, deux
	// undo, un redo. Une impulsion par couche, à une place différente.
	void playScenario(Rig& rig)
	{
		rig.input = [](std::int64_t i)
		{
			if (i == 1000) return 1.0f;
			if (i == 2 * bar + 2000) return 0.5f;
			if (i == 4 * bar + 3000) return 0.25f;
			if (i == 6 * bar + 4000) return 0.125f;
			return 0.0f;
		};

		rig.at(0, CommandType::TrackMain);
		rig.at(bar, CommandType::TrackMain);
		rig.at(2 * bar, CommandType::TrackMain);
		rig.at(3 * bar, CommandType::TrackMain);
		rig.at(4 * bar, CommandType::TrackMain);
		rig.at(5 * bar, CommandType::TrackMain);
		rig.at(6 * bar, CommandType::TrackMain);
		rig.at(7 * bar, CommandType::TrackMain);
		rig.at(8 * bar, CommandType::TrackUndo);
		rig.at(8 * bar + 1000, CommandType::TrackUndo);
		rig.at(8 * bar + 2000, CommandType::TrackRedo);
		rig.run(10 * bar);
	}

	// Sauvegarde pendant que le moteur tourne : instantané pris au bloc suivant.
	session::Result saveRunning(Rig& rig, const fs::path& folder, std::vector<session::ExtraFile> files = {})
	{
		REQUIRE(rig.engine.beginSnapshot());
		CHECK(rig.engine.snapshot() == nullptr);
		rig.run(rig.sample + blockSize);
		const auto* snapshot = rig.engine.snapshot();
		REQUIRE(snapshot != nullptr);

		auto description = session::fromSnapshot(*snapshot);
		description.application = "tests";
		description.files = std::move(files);

		// Le son continue pendant l'écriture.
		rig.run(rig.sample + 4 * blockSize);
		const auto result = session::save(folder, description, session::snapshotReader(rig.engine));
		rig.engine.endSnapshot();
		rig.run(rig.sample + blockSize);
		return result;
	}

	session::Result loadInto(Rig& rig, const fs::path& folder, session::Session& loaded)
	{
		auto result = session::load(folder, loaded);

		if (!result.ok)
			return result;

		RestoreRequest request;
		std::vector<std::vector<const float*>> pointers;
		std::string reason;
		REQUIRE(session::toRestoreRequest(loaded, rig.engine.config(), request, pointers, reason));

		rig.engine.suspend();
		const auto restored = rig.engine.restore(request);
		rig.engine.resume();
		REQUIRE(restored == RestoreResult::Ok);
		return result;
	}
}

TEST_CASE("Reserve epinglee : blocs rendus mis de cote jusqu'au unpin")
{
	SamplePool pool;
	REQUIRE(pool.prepare(1, 3 * 1024 * sizeof(float), 1024));

	const int a = pool.acquire();
	const int b = pool.acquire();
	pool.pin();
	pool.release(a);
	CHECK(pool.usedChunks() == 1);
	CHECK(pool.freeChunks() == 1);

	// Le bloc rendu n'est pas redonné pendant que la sauvegarde le lit.
	const int c = pool.acquire();
	CHECK(c != a);
	CHECK(pool.acquire() == -1);

	pool.unpin();
	CHECK(pool.acquire() == a);
	pool.release(b);
	pool.release(c);
}

TEST_CASE("WAV flottant : aller-retour exact")
{
	std::vector<float> left { 0.0f, 0.5f, -1.0f, 1.0e-7f, 0.123456789f };
	std::vector<float> right { 1.0f, -0.5f, 0.25f, -1.0e-7f, 2.0f };
	const auto bytes = session::encodeWav({ left.data(), right.data() }, 5, 48000.0);

	session::WavData wav;
	std::string reason;
	REQUIRE(session::decodeWav(bytes, wav, reason));
	CHECK(wav.sampleRate == 48000.0);
	REQUIRE(wav.channels.size() == 2);
	CHECK(wav.channels[0] == left);
	CHECK(wav.channels[1] == right);

	auto broken = bytes;
	broken[20] = 1;	// PCM entier : refusé
	CHECK_FALSE(session::decodeWav(broken, wav, reason));
}

TEST_CASE("Session V1-2 : overdubs, undo, redo, sauvegarde et reouverture")
{
	TempFolder temp;
	Rig rig;
	playScenario(rig);

	auto before = rig.engine.trackSnapshot(0);
	REQUIRE(before.numLayers == 4);
	REQUIRE(before.numActiveLayers == 3);
	CHECK(rig.out(9 * bar + 1000) == 1.0f);
	CHECK(rig.out(9 * bar + 2000) == 0.5f);
	CHECK(rig.out(9 * bar + 3000) == 0.25f);
	CHECK(rig.out(9 * bar + 4000) == 0.0f);

	const auto folder = temp.path / "Morceau";
	const auto saved = saveRunning(rig, folder, { { "reglages.xml", "<BOOHcleur version=\"2\"/>" } });
	REQUIRE(saved.ok);
	CHECK(fs::exists(temp.path / "Morceau.boohcleur" / "session.json"));
	CHECK(fs::exists(temp.path / "Morceau.boohcleur" / "audio" / "boucle1-couche4.wav"));
	CHECK_FALSE(fs::exists(temp.path / "Morceau.boohcleur.tmp"));

	// Réouverture dans un moteur neuf.
	Rig other;
	session::Session loaded;
	REQUIRE(loadInto(other, session::withExtension(folder), loaded).ok);
	REQUIRE(loaded.files.size() == 1);
	CHECK(loaded.files[0].content == "<BOOHcleur version=\"2\"/>");

	const auto after = other.engine.trackSnapshot(0);
	CHECK(after.numLayers == 4);
	CHECK(after.numActiveLayers == 3);
	CHECK(after.loopLength == bar);
	CHECK(after.state == TrackState::Playing);
	CHECK(other.engine.trackSnapshot(1).state == TrackState::Empty);
	CHECK_FALSE(other.engine.transportSnapshot().playing);

	// Le transport repart à la mesure 1 : même son, au même endroit de la mesure.
	const SamplePos start = other.sample;
	other.at(start, CommandType::TransportStart);
	other.run(start + 2 * bar);
	CHECK(other.out(start + bar + 1000) == 1.0f);
	CHECK(other.out(start + bar + 2000) == 0.5f);
	CHECK(other.out(start + bar + 3000) == 0.25f);
	CHECK(other.out(start + bar + 4000) == 0.0f);

	// L'historique d'undo a survécu : le redo rend la quatrième couche.
	other.at(other.sample, CommandType::TrackRedo);
	other.run(other.sample + 2 * bar);
	CHECK(other.engine.trackSnapshot(0).numActiveLayers == 4);
	CHECK(other.out(start + 3 * bar + 4000) == 0.125f);
}

TEST_CASE("Session : sauvegarde pendant un overdub, couche en cours exclue")
{
	TempFolder temp;
	Rig rig;
	rig.input = [](std::int64_t i) { return i == 1000 ? 1.0f : (i == 2 * bar + 500 ? 0.5f : 0.0f); };
	rig.at(0, CommandType::TrackMain);
	rig.at(bar, CommandType::TrackMain);
	rig.at(2 * bar, CommandType::TrackMain);	// overdub en cours
	rig.run(2 * bar + bar / 2);
	REQUIRE(rig.engine.trackSnapshot(0).state == TrackState::Overdubbing);

	REQUIRE(saveRunning(rig, temp.path / "pendant").ok);

	session::Session loaded;
	REQUIRE(session::load(session::withExtension(temp.path / "pendant"), loaded).ok);
	REQUIRE(loaded.loops.size() == 1);
	CHECK(loaded.loops[0].layers.size() == 1);
	CHECK(loaded.loops[0].numActive == 1);
	CHECK(loaded.loops[0].layers[0].audio[0][1000] == 1.0f);

	// L'overdub, lui, a continué normalement.
	rig.at(rig.sample, CommandType::TrackMain);
	rig.run(5 * bar);
	CHECK(rig.engine.trackSnapshot(0).numLayers == 2);
}

TEST_CASE("Session : effacer pendant la sauvegarde ne corrompt pas l'audio ecrit")
{
	TempFolder temp;
	Rig rig;
	rig.input = [](std::int64_t i) { return i < bar ? 0.25f : 0.75f; };
	rig.at(0, CommandType::TrackMain);
	rig.at(bar, CommandType::TrackMain);
	rig.run(2 * bar);

	REQUIRE(rig.engine.beginSnapshot());
	rig.run(rig.sample + blockSize);
	const auto description = session::fromSnapshot(*rig.engine.snapshot());

	// Effacement puis nouvelle prise pendant l'écriture : sans l'épinglage,
	// la nouvelle prise réutiliserait les blocs encore à écrire.
	rig.at(rig.sample, CommandType::TrackClear);
	rig.at(rig.sample + bar / 2, CommandType::TrackMain, 0);
	rig.run(rig.sample + 3 * bar);

	REQUIRE(session::save(temp.path / "efface", description, session::snapshotReader(rig.engine)).ok);
	rig.engine.endSnapshot();
	rig.run(rig.sample + blockSize);

	session::Session loaded;
	REQUIRE(session::load(session::withExtension(temp.path / "efface"), loaded).ok);
	const auto& audio = loaded.loops.at(0).layers.at(0).audio[0];

	for (SamplePos i = 0; i < bar - 1000; i += 997)
		REQUIRE(audio[static_cast<std::size_t>(i)] == 0.25f);
}

TEST_CASE("Session : fichier abime ou manquant signale")
{
	TempFolder temp;
	Rig rig;
	rig.input = [](std::int64_t i) { return i == 1000 ? 1.0f : 0.0f; };
	rig.at(0, CommandType::TrackMain);
	rig.at(bar, CommandType::TrackMain);
	rig.run(2 * bar);
	REQUIRE(saveRunning(rig, temp.path / "abime").ok);

	const auto folder = temp.path / "abime.boohcleur";
	const auto wav = folder / "audio" / "boucle1-couche1.wav";

	{
		std::fstream file(wav, std::ios::in | std::ios::out | std::ios::binary);
		file.seekp(5000);
		file.put('\x7f');
	}

	session::Session loaded;
	auto result = session::load(folder, loaded);
	CHECK_FALSE(result.ok);
	CHECK(result.message.find("boucle1-couche1.wav") != std::string::npos);

	fs::remove(wav);
	result = session::load(folder, loaded);
	CHECK_FALSE(result.ok);
	CHECK(result.message.find("manquant") != std::string::npos);

	CHECK_FALSE(session::load(temp.path / "rien.boohcleur", loaded).ok);
}

TEST_CASE("Session : coupure pendant le remplacement, version precedente relue")
{
	TempFolder temp;
	Rig rig;
	rig.input = [](std::int64_t i) { return i == 1000 ? 1.0f : 0.0f; };
	rig.at(0, CommandType::TrackMain);
	rig.at(bar, CommandType::TrackMain);
	rig.run(2 * bar);

	const auto folder = temp.path / "coupure.boohcleur";
	REQUIRE(saveRunning(rig, folder).ok);
	REQUIRE(saveRunning(rig, folder).ok);	// remplacement complet : pas de .old qui traîne
	CHECK_FALSE(fs::exists(temp.path / "coupure.boohcleur.old"));

	// Coupure entre les deux renommages : l'ancienne version est en .old,
	// la nouvelle n'est pas encore en place.
	fs::path old = folder;
	old += ".old";
	fs::rename(folder, old);

	session::Session loaded;
	auto result = session::load(folder, loaded);
	REQUIRE(result.ok);
	CHECK(result.recovered);
	CHECK(loaded.loops.size() == 1);

	// Dossier présent mais incomplet (manifeste pas encore écrit).
	fs::create_directories(folder / "audio");
	result = session::load(folder, loaded);
	REQUIRE(result.ok);
	CHECK(result.recovered);
}

TEST_CASE("Session : frequence d'echantillonnage differente refusee clairement")
{
	session::Session description;
	description.sampleRate = 44100.0;
	EngineConfig config;
	RestoreRequest request;
	std::vector<std::vector<const float*>> pointers;
	std::string reason;
	CHECK_FALSE(session::toRestoreRequest(description, config, request, pointers, reason));
	CHECK(reason.find("44100") != std::string::npos);
}

TEST_CASE("Session : chargement refuse si la memoire manque, moteur laisse vide")
{
	TempFolder temp;
	Rig rig;
	rig.input = [](std::int64_t i) { return i == 1000 ? 1.0f : 0.0f; };
	rig.at(0, CommandType::TrackMain);
	rig.at(4 * bar, CommandType::TrackMain);	// quatre mesures, 8 s
	rig.run(5 * bar);
	REQUIRE(saveRunning(rig, temp.path / "longue").ok);

	EngineConfig small;
	small.sampleRate = sampleRate;
	small.maxBlockSize = blockSize;
	small.memoryBudgetBytes = std::size_t { 1 } << 20;	// 1 Mo : environ 2,7 s stéréo
	Engine engine;
	REQUIRE(engine.prepare(small));

	session::Session loaded;
	REQUIRE(session::load(session::withExtension(temp.path / "longue"), loaded).ok);
	RestoreRequest request;
	std::vector<std::vector<const float*>> pointers;
	std::string reason;
	REQUIRE(session::toRestoreRequest(loaded, engine.config(), request, pointers, reason));

	engine.suspend();
	CHECK(engine.restore(request) == RestoreResult::OutOfMemory);
	engine.resume();
	CHECK(engine.trackSnapshot(0).state == TrackState::Empty);
	CHECK(engine.memorySnapshot().usedBytes == 0);
}

TEST_CASE("Session V1-7 : dossier renomme relu, ecriture impossible signalee sans rien abimer")
{
	TempFolder temp;
	Rig rig;
	rig.input = [](std::int64_t i) { return i == 1000 ? 1.0f : 0.0f; };
	rig.at(0, CommandType::TrackMain);
	rig.at(bar, CommandType::TrackMain);
	rig.run(2 * bar);
	REQUIRE(saveRunning(rig, temp.path / "avant").ok);

	// Aucun chemin absolu dans la session : renommer le dossier ne casse rien.
	fs::rename(temp.path / "avant.boohcleur", temp.path / "apres.boohcleur");
	session::Session loaded;
	REQUIRE(session::load(temp.path / "apres.boohcleur", loaded).ok);
	CHECK(loaded.loops.at(0).layers.at(0).audio[0][1000] == 1.0f);

	// Écriture impossible (le dossier parent est un fichier) : message clair,
	// la session existante est intacte.
	{
		std::ofstream blocker(temp.path / "fichier");
		blocker << "x";
	}

	const auto failed = saveRunning(rig, temp.path / "fichier" / "dedans");
	CHECK_FALSE(failed.ok);
	CHECK_FALSE(failed.message.empty());
	CHECK(session::load(temp.path / "apres.boohcleur", loaded).ok);
}
