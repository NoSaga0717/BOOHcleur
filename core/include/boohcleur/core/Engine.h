#pragma once

#include "boohcleur/core/Grid.h"
#include "boohcleur/core/InputRingBuffer.h"
#include "boohcleur/core/LoopTrack.h"
#include "boohcleur/core/SpscQueue.h"
#include "boohcleur/core/TransportClock.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace boohcleur
{
	struct EngineConfig
	{
		double sampleRate = 48000.0;
		int maxBlockSize = 512;
		int numChannels = 2;
		int numTracks = 1;				// boucles créées (ADR D10 : 6 par défaut à partir de J4)
		int maxTracks = 6;				// plafond logiciel de la cible
		double maxLoopSeconds = 60.0;	// provisoire, budget mémoire à chiffrer en J4
		int maxLayersPerTrack = 8;
		double ringSeconds = 2.0;		// tampon circulaire d'entrée
		double fadeMilliseconds = 5.0;
		double bpm = 120.0;
		TimeSignature signature {};
	};

	enum class CommandType : std::uint8_t
	{
		TrackMain,		// bouton principal : Rec -> Play -> Overdub -> Play...
		TrackStop,
		TrackPlay,
		TrackUndo,
		TrackRedo,
		TrackClear,
		TransportStart,
		TransportStop,
		SetTempo		// value = BPM ; refusé si le transport tourne ou si une piste n'est pas vide
	};

	struct Command
	{
		CommandType type = CommandType::TrackMain;
		int track = 0;
		double value = 0.0;
	};

	// Action planifiée sur une piste, en attente de sa frontière.
	enum class PendingAction : std::uint8_t
	{
		None,
		StartRecording,
		CloseRecording,
		CloseAndStop,
		Play
	};

	struct TrackSnapshot
	{
		TrackState state = TrackState::Empty;
		PendingAction pending = PendingAction::None;
		int numLayers = 0;
		int numActiveLayers = 0;
		SamplePos loopLength = 0;		// échantillons, 0 si vide
		SamplePos position = 0;			// position dans la boucle
		SamplePos recorded = 0;			// pendant la première prise : durée enregistrée
		Tick pendingTarget = 0;
	};

	struct TransportSnapshot
	{
		bool playing = false;
		Tick tick = 0;
		double bpm = 120.0;
		TimeSignature signature {};
	};

	// Moteur du looper, commun au Standalone, aux plugins et (plus tard) à la tête.
	//
	// Threads :
	// - prepare() : hors temps réel, alloue tout ;
	// - process() : thread audio, aucune allocation ni verrou ;
	// - post() : un seul thread producteur (interface) ; le MIDI aura sa file en J3 ;
	// - snapshot*() et réglages : n'importe quel thread (atomiques).
	class Engine
	{
	public:
		Engine();
		~Engine();

		bool prepare(const EngineConfig& config);
		const EngineConfig& config() const noexcept { return config_; }

		// in : numIn pointeurs (1 = mono, dupliqué) ; out : numOut pointeurs.
		void process(const float* const* in, int numIn, float* const* out, int numOut, int numFrames) noexcept;

		// Rend false si la file de commandes est pleine.
		bool post(const Command& command) noexcept;

		TrackSnapshot trackSnapshot(int track) const noexcept;
		TransportSnapshot transportSnapshot() const noexcept;
		int numTracks() const noexcept { return static_cast<int>(tracks_.size()); }

		// Réglages modifiables pendant le jeu.
		void setLatencySamples(int samples) noexcept { latencySamples_.store(samples); }
		int latencySamples() const noexcept { return latencySamples_.load(); }
		void setLateToleranceMilliseconds(float ms) noexcept { lateToleranceMs_.store(ms); }
		float lateToleranceMilliseconds() const noexcept { return lateToleranceMs_.load(); }
		void setQuantizeDivision(GridDivision division) noexcept { division_.store(static_cast<int>(division)); }
		GridDivision quantizeDivision() const noexcept { return static_cast<GridDivision>(division_.load()); }
		void setMetronomeEnabled(bool enabled) noexcept { metronome_.store(enabled); }
		bool metronomeEnabled() const noexcept { return metronome_.load(); }
		void setMonitorEnabled(bool enabled) noexcept { monitor_.store(enabled); }
		bool monitorEnabled() const noexcept { return monitor_.load(); }

	private:
		struct Pending
		{
			PendingAction action = PendingAction::None;
			Tick target = 0;
		};

		struct SharedTrackState
		{
			std::atomic<int> state { 0 };
			std::atomic<int> pending { 0 };
			std::atomic<int> numLayers { 0 };
			std::atomic<int> numActive { 0 };
			std::atomic<std::int64_t> loopLength { 0 };
			std::atomic<std::int64_t> position { 0 };
			std::atomic<std::int64_t> recorded { 0 };
			std::atomic<std::int64_t> pendingTarget { 0 };
		};

		void handleCommand(const Command& command, SamplePos blockStart) noexcept;
		void handleTrackMain(int index, Tick pressTick, SamplePos blockStart) noexcept;
		void schedule(int index, PendingAction action, Tick pressTick, SamplePos blockStart) noexcept;
		void execute(int index, const Pending& pending, SamplePos at) noexcept;
		std::int64_t musicalToDevice() const noexcept;
		void renderMetronome(SamplePos blockStart, int numFrames, float* const* out, int numOut) noexcept;
		void publish(SamplePos blockStart) noexcept;

		EngineConfig config_;
		TempoMap tempoMap_;
		TransportClock clock_;
		InputRingBuffer ring_;
		std::vector<LoopTrack> tracks_;
		std::vector<Pending> pending_;
		std::unique_ptr<SharedTrackState[]> shared_;
		std::vector<std::vector<float>> scratch_;

		SpscQueue<Command, 256> commands_;

		std::int64_t deviceSample_ = 0;			// compteur de l'appareil, ne s'arrête jamais
		std::int64_t musicalOrigin_ = 0;		// index d'appareil de la position musicale 0
		SamplePos transportStopAt_ = -1;		// arrêt différé après les fondus

		// Métronome
		int clickRemaining_ = 0;
		int clickLength_ = 0;
		double clickPhase_ = 0.0;
		double clickIncrement_ = 0.0;

		std::atomic<int> latencySamples_ { 0 };
		std::atomic<float> lateToleranceMs_ { 80.0f };
		std::atomic<int> division_ { static_cast<int>(GridDivision::Bar) };
		std::atomic<bool> metronome_ { true };
		std::atomic<bool> monitor_ { false };

		std::atomic<bool> transportPlaying_ { false };
		std::atomic<std::int64_t> transportTick_ { 0 };
		std::atomic<double> bpm_ { 120.0 };
		std::atomic<int> numerator_ { 4 };
		std::atomic<int> denominator_ { 4 };
	};
}
