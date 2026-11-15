#include "SessionManager.h"
#include "PluginProcessor.h"

#include "boohcleur/core/Version.h"

#include <thread>

namespace
{
	std::filesystem::path toPath(const juce::File& file)
	{
		const auto text = file.getFullPathName().toStdString();	// UTF-8
		return std::filesystem::path(std::u8string(text.begin(), text.end()));
	}

	juce::String fromUtf8(const std::string& text)
	{
		return juce::String::fromUTF8(text.c_str(), static_cast<int>(text.size()));
	}

	juce::String fr(const char* text) { return juce::String::fromUTF8(text); }

	constexpr int autosaveIntervalMs = 30000;
}

// Le travail tourne sur un thread à part ; la fin est rapportée sur le thread
// de l'interface, si le gestionnaire existe encore.
struct SessionWorker final : juce::Thread
{
	SessionWorker(std::function<SessionManager::Outcome()> w, std::function<void(const SessionManager::Outcome&)> a,
		std::shared_ptr<std::atomic<bool>> alive)
		: juce::Thread("BOOHcleur sessions"), work(std::move(w)), after(std::move(a)), alive_(std::move(alive))
	{
	}

	void run() override
	{
		const auto outcome = work();
		auto done = after;
		auto alive = alive_;
		juce::MessageManager::callAsync([done, alive, outcome]
		{
			if (alive->load() && done)
				done(outcome);
		});
	}

	std::function<SessionManager::Outcome()> work;
	std::function<void(const SessionManager::Outcome&)> after;
	std::shared_ptr<std::atomic<bool>> alive_;
};

SessionManager::SessionManager(BoohcleurProcessor& processor)
	: processor_(processor)
{
}

SessionManager::~SessionManager()
{
	stopTimer();
	alive_->store(false);

	if (worker_ != nullptr)
		worker_->stopThread(-1);

	if (!autosave_)
		return;

	// Fermeture normale : le travail non enregistré est gardé et proposé au
	// prochain lancement ; sinon rien à récupérer.
	if (hasUnsavedChanges() && contentSignature().contains("+"))
	{
		if (saveNow(autosaveFolder(), processor_.stateXml().toStdString()).ok)
			runningMarker().create();
	}
	else
	{
		runningMarker().deleteFile();
	}
}

juce::File SessionManager::appFolder()
{
	return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("BOOHcleur");
}

juce::File SessionManager::autosaveFolder()
{
	return appFolder().getChildFile(juce::String("autosave") + boohcleur::session::extension);
}

juce::File SessionManager::runningMarker()
{
	return appFolder().getChildFile(fr("autosave-a-recuperer"));
}

juce::File SessionManager::defaultFolder()
{
	return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("BOOHcleur");
}

juce::String SessionManager::currentName() const
{
	return current_ == juce::File {} ? fr("Sans titre") : current_.getFileNameWithoutExtension();
}

juce::String SessionManager::contentSignature() const
{
	const auto& engine = processor_.engine();
	juce::String signature(engine.transportSnapshot().bpm, 3);

	for (int t = 0; t < engine.numTracks(); ++t)
	{
		const auto s = engine.trackSnapshot(t);

		if (s.numLayers > 0 && s.state != boohcleur::TrackState::Empty)
			signature << "+" << t << ":" << s.numLayers << "/" << s.numActiveLayers << "/" << static_cast<juce::int64>(s.loopLength);
	}

	return signature;
}

bool SessionManager::hasUnsavedChanges() const
{
	return processor_.isPrepared() && contentSignature() != savedSignature_;
}

void SessionManager::waitUntilIdle()
{
	const juce::ScopedLock lock(workLock_);
}

void SessionManager::run(std::function<Outcome()> work, std::function<void(const Outcome&)> after)
{
	if (worker_ != nullptr)
		worker_->stopThread(-1);	// déjà fini (busy_ faux) : simple ménage

	busy_.store(true);
	worker_ = std::make_unique<SessionWorker>(
		[this, work]
		{
			const juce::ScopedLock lock(workLock_);
			auto outcome = work();
			busy_.store(false);
			return outcome;
		},
		std::move(after), alive_);
	worker_->startThread();
}

SessionManager::Outcome SessionManager::saveNow(const juce::File& folder, const std::string& settings)
{
	auto& engine = processor_.engine();

	if (!processor_.isPrepared())
		return { false, fr("Le moteur audio n'est pas prêt : rien à enregistrer.") };

	if (!engine.beginSnapshot())
		return { false, fr("Une sauvegarde est déjà en cours.") };

	// Le thread audio fige le contenu au début de son prochain bloc.
	for (int i = 0; i < 60 && engine.snapshot() == nullptr; ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(5));

	// Audio arrêté (carte son fermée) : on fige directement, moteur suspendu.
	if (engine.snapshot() == nullptr)
	{
		engine.suspend();
		engine.captureSnapshotNow();
		engine.resume();
	}

	const auto* snapshot = engine.snapshot();

	if (snapshot == nullptr)
	{
		engine.endSnapshot();
		return { false, fr("Instantané du moteur impossible.") };
	}

	auto description = boohcleur::session::fromSnapshot(*snapshot);
	description.application = std::string("BOOHcleur ") + std::string(boohcleur::engineVersion());
	description.files.push_back({ "reglages.xml", settings });

	const auto result = boohcleur::session::save(toPath(folder), description, boohcleur::session::snapshotReader(engine));
	engine.endSnapshot();

	if (!result.ok)
		return { false, fromUtf8(result.message) };

	return { true, fr("Session enregistrée : ") + folder.getFileNameWithoutExtension() };
}

SessionManager::Outcome SessionManager::openNow(const juce::File& folder, juce::String& settings)
{
	auto& engine = processor_.engine();

	if (!processor_.isPrepared())
		return { false, fr("Le moteur audio n'est pas prêt : ouvrir la carte son puis recommencer.") };

	boohcleur::session::Session loaded;
	const auto result = boohcleur::session::load(toPath(folder), loaded);

	if (!result.ok)
		return { false, fromUtf8(result.message) };

	boohcleur::RestoreRequest request;
	std::vector<std::vector<const float*>> pointers;
	std::string reason;

	if (!boohcleur::session::toRestoreRequest(loaded, engine.config(), request, pointers, reason))
		return { false, fromUtf8(reason) };

	engine.suspend();
	const auto restored = engine.restore(request);
	engine.resume();

	if (restored != boohcleur::RestoreResult::Ok)
		return { false, fromUtf8(boohcleur::session::describe(restored)) };

	for (const auto& file : loaded.files)
		if (file.name == "reglages.xml")
			settings = fromUtf8(file.content);

	auto message = fr("Session ouverte : ") + folder.getFileNameWithoutExtension();

	if (result.recovered)
		message = fromUtf8(result.message);

	return { true, message };
}

void SessionManager::save(const juce::File& requested, Callback done)
{
	if (busy_.load())
	{
		if (done) done({ false, fr("Une opération de session est déjà en cours.") });
		return;
	}

	const auto folder = juce::File(requested.getFullPathName().endsWith(boohcleur::session::extension)
		? requested.getFullPathName() : requested.getFullPathName() + boohcleur::session::extension);
	const auto settings = processor_.stateXml().toStdString();
	folder.getParentDirectory().createDirectory();

	run([this, folder, settings] { return saveNow(folder, settings); },
		[this, folder, done](const Outcome& outcome)
		{
			if (outcome.ok)
			{
				current_ = folder;
				savedSignature_ = contentSignature();
				runningMarker().deleteFile();
			}

			if (done) done(outcome);
		});
}

void SessionManager::open(const juce::File& requested, Callback done)
{
	if (busy_.load())
	{
		if (done) done({ false, fr("Une opération de session est déjà en cours.") });
		return;
	}

	// Un clic sur session.json ouvre son dossier.
	const auto folder = requested.getFileName() == "session.json" ? requested.getParentDirectory() : requested;
	auto settings = std::make_shared<juce::String>();

	run([this, folder, settings] { return openNow(folder, *settings); },
		[this, folder, settings, done](const Outcome& outcome)
		{
			if (outcome.ok)
			{
				if (settings->isNotEmpty())
					processor_.applyStateXml(*settings, true);

				current_ = folder;
				savedSignature_ = contentSignature();
				autosavedSignature_ = savedSignature_;
				runningMarker().deleteFile();
			}

			if (done) done(outcome);
		});
}

void SessionManager::clearAll(Callback done)
{
	if (busy_.load())
	{
		if (done) done({ false, fr("Une opération de session est déjà en cours.") });
		return;
	}

	run([this]() -> Outcome
		{
			auto& engine = processor_.engine();

			if (!processor_.isPrepared())
				return { false, fr("Le moteur audio n'est pas prêt.") };

			boohcleur::RestoreRequest empty;
			empty.bpm = engine.transportSnapshot().bpm;
			empty.signature = engine.transportSnapshot().signature;
			engine.suspend();
			const auto result = engine.restore(empty);
			engine.resume();
			return { result == boohcleur::RestoreResult::Ok, fr("Nouvelle session") };
		},
		[this, done](const Outcome& outcome)
		{
			current_ = juce::File {};
			savedSignature_ = contentSignature();
			runningMarker().deleteFile();

			if (done) done(outcome);
		});
}

void SessionManager::enableAutosave()
{
	if (autosave_)
		return;

	autosave_ = true;
	appFolder().createDirectory();
	recoveryAvailable_ = runningMarker().existsAsFile() && autosaveFolder().getChildFile("session.json").existsAsFile();
	startTimer(autosaveIntervalMs);
}

bool SessionManager::hasRecovery() const
{
	return recoveryAvailable_;
}

void SessionManager::recover(Callback done)
{
	recoveryAvailable_ = false;

	open(autosaveFolder(), [this, done](const Outcome& outcome)
	{
		if (outcome.ok)
		{
			// Rien n'est encore enregistré par l'utilisateur : session sans titre, à enregistrer.
			current_ = juce::File {};
			savedSignature_ = {};
			runningMarker().create();
		}

		if (done) done({ outcome.ok, outcome.ok ? fr("Session récupérée (sauvegarde automatique) : pensez à l'enregistrer.") : outcome.message });
	});
}

void SessionManager::discardRecovery()
{
	recoveryAvailable_ = false;
	runningMarker().deleteFile();
}

void SessionManager::timerCallback()
{
	if (busy_.load() || recoveryAvailable_ || !processor_.isPrepared())
		return;

	const auto signature = contentSignature();

	if (signature == autosavedSignature_)
		return;

	if (!hasUnsavedChanges() || !signature.contains("+"))
	{
		autosavedSignature_ = signature;
		runningMarker().deleteFile();
		return;
	}

	const auto settings = processor_.stateXml().toStdString();
	const auto folder = autosaveFolder();

	run([this, folder, settings] { return saveNow(folder, settings); },
		[this, signature](const Outcome& outcome)
		{
			if (outcome.ok)
			{
				autosavedSignature_ = signature;
				runningMarker().create();
			}
		});
}
