#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "boohcleur/session/Session.h"

#include <functional>

class BoohcleurProcessor;

// Sessions côté application (J5) : enregistrer, ouvrir, sauvegarde
// automatique et récupération après plantage.
//
// L'écriture et la lecture se font sur un thread à part : le son continue
// pendant une sauvegarde (instantané du moteur, ADR 0001 D12). Une seule
// opération à la fois. Les rappels arrivent sur le thread de l'interface.
class SessionManager final : private juce::Timer
{
public:
	struct Outcome
	{
		bool ok = false;
		juce::String message;
	};

	using Callback = std::function<void(const Outcome&)>;

	explicit SessionManager(BoohcleurProcessor& processor);
	~SessionManager() override;

	// Session courante (vide : jamais enregistrée).
	juce::File currentFile() const { return current_; }
	juce::String currentName() const;
	bool isBusy() const noexcept { return busy_.load(); }

	// Changements depuis la dernière sauvegarde ou ouverture.
	bool hasUnsavedChanges() const;

	void save(const juce::File& folder, Callback done);
	void open(const juce::File& folder, Callback done);
	// Vide toutes les boucles et oublie la session courante.
	void clearAll(Callback done);

	// Sauvegarde automatique (application autonome) : toutes les 30 s si
	// quelque chose a changé, dans le dossier de l'application.
	void enableAutosave();
	// Une sauvegarde automatique d'une exécution qui ne s'est pas fermée
	// normalement (plantage, coupure) : à proposer au démarrage.
	bool hasRecovery() const;
	void recover(Callback done);
	void discardRecovery();

	// Appelé avant de recréer le moteur : attend la fin d'une opération en cours.
	void waitUntilIdle();

	static juce::File defaultFolder();

private:
	void timerCallback() override;
	juce::String contentSignature() const;
	void run(std::function<Outcome()> work, std::function<void(const Outcome&)> after);
	Outcome saveNow(const juce::File& folder, const std::string& settings);
	Outcome openNow(const juce::File& folder, juce::String& settings);

	static juce::File appFolder();
	static juce::File autosaveFolder();
	static juce::File runningMarker();

	BoohcleurProcessor& processor_;
	juce::File current_;
	juce::String savedSignature_;
	juce::String autosavedSignature_;
	bool autosave_ = false;
	bool recoveryAvailable_ = false;
	std::atomic<bool> busy_ { false };
	std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
	juce::CriticalSection workLock_;
	std::unique_ptr<juce::Thread> worker_;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionManager)
};
