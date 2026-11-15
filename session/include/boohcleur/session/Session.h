#pragma once

#include "boohcleur/core/Engine.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace boohcleur::session
{
	// Session sur disque (ADR 0001, D12) : un dossier « Nom.boohcleur » qui
	// contient tout, sans lien vers l'extérieur :
	//
	//   session.json           manifeste versionné (écrit en dernier)
	//   audio/boucleN-coucheM.wav   une couche par fichier, flottant 32 bits
	//   reglages.xml ...       fichiers de l'application (réglages, pédalier)
	//
	// Chaque fichier a son empreinte (FNV-1a 64 bits) dans le manifeste : un
	// fichier abîmé ou manquant est signalé à l'ouverture, avec son nom.
	//
	// Écriture atomique : tout est écrit dans « Nom.boohcleur.tmp », puis
	// l'ancienne version devient « Nom.boohcleur.old », la nouvelle prend sa
	// place et l'ancienne est effacée. Une coupure à n'importe quel moment
	// laisse une version complète, que load() retrouve.

	inline constexpr int formatVersion = 1;
	inline constexpr const char* extension = ".boohcleur";

	struct Layer
	{
		SamplePos startOffset = 0;
		SamplePos count = 0;
		std::vector<std::vector<float>> audio;	// [canal][échantillon], rempli par load()
	};

	struct Loop
	{
		int index = 0;				// boucle 0 à N-1
		bool playing = false;
		LoopSpan span;
		SamplePos loopLength = 0;
		int numActive = 0;
		std::vector<Layer> layers;
	};

	struct ExtraFile
	{
		std::string name;			// nom simple, sans dossier
		std::string content;
	};

	struct Session
	{
		double sampleRate = 48000.0;
		int numChannels = 2;
		double bpm = 120.0;
		TimeSignature signature {};
		std::vector<Loop> loops;	// boucles non vides seulement
		std::vector<ExtraFile> files;
		std::string application;	// qui l'a écrite (information)
	};

	struct Result
	{
		bool ok = true;
		bool recovered = false;		// lue depuis la version précédente (.old)
		std::string message;		// en français, à montrer tel quel

		static Result failure(std::string text) { return { false, false, std::move(text) }; }
	};

	// Audio d'une couche à écrire : (boucle, couche, canal, début, nombre, destination).
	using AudioReader = std::function<void(int loop, int layer, int channel, SamplePos start, int count, float* dest)>;

	// Écrit la session (sans son audio : lu par reader au fil de l'écriture).
	// session.loops[i].layers[j].audio est ignoré ; reader reçoit l'index de
	// boucle (Loop::index) et de couche.
	Result save(const std::filesystem::path& folder, const Session& session, const AudioReader& reader);

	// Lit une session complète, audio compris, et vérifie chaque fichier.
	Result load(const std::filesystem::path& folder, Session& out);

	// Ajoute l'extension si elle manque.
	std::filesystem::path withExtension(std::filesystem::path folder);

	// --- Passerelles avec le moteur ---

	// Description d'un instantané du moteur (sans audio).
	Session fromSnapshot(const ContentSnapshot& snapshot);

	// Lecture de l'audio d'un instantané, pour save().
	AudioReader snapshotReader(const Engine& engine);

	// Demande de chargement pour Engine::restore(). Les pointeurs visent
	// l'audio de session : elle doit vivre jusqu'à la fin de restore().
	// Rend false (et un message) si la session ne convient pas à ce moteur.
	bool toRestoreRequest(const Session& session, const EngineConfig& config, RestoreRequest& request,
		std::vector<std::vector<const float*>>& pointers, std::string& reason);

	std::string describe(RestoreResult result);
}
