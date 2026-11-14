#pragma once

#include "boohcleur/core/InputRingBuffer.h"
#include "boohcleur/core/SamplePool.h"
#include "boohcleur/core/TransportClock.h"

#include <array>
#include <cstdint>
#include <vector>

namespace boohcleur
{
	// États d'une piste (ADR 0001, machine à états « Piste »).
	enum class TrackState : std::uint8_t
	{
		Empty,			// rien d'enregistré
		Armed,			// enregistrement demandé, attend sa frontière
		Recording,		// première prise en cours (fixe la longueur)
		Playing,
		Overdubbing,	// lecture + nouvelle couche en cours
		Stopped
	};

	struct TrackConfig
	{
		int numChannels = 2;			// doit être celui de la réserve
		SamplePos maxLoopSamples = 0;	// longueur maximale d'une boucle
		int maxLayers = 8;				// couches par piste (undo compris)
		int fadeSamples = 240;			// fondus anti-clic
	};

	// Une piste : une boucle, ses couches non destructives et leur capture.
	//
	// Toutes les méthodes sauf prepare() sont temps réel : aucune allocation.
	// Le son des couches vit dans une réserve partagée (SamplePool) : une couche
	// ne prend que la mémoire de ce qu'elle a enregistré. Réserve vide : la
	// suite de la couche reste muette, rien ne casse.
	//
	// Couches :
	// - la couche 0 est la première prise, sur toute la longueur de la boucle ;
	// - chaque overdub crée une couche ; un overdub qui dure plus d'un tour en
	//   crée une par tour, pour pouvoir annuler tour par tour ;
	// - l'undo désactive la dernière couche active, le redo la réactive ; un
	//   nouvel overdub après un undo supprime les couches annulées.
	//
	// Capture : le son est relu dans le tampon circulaire avec un décalage
	// (latence aller-retour), donc toujours un peu après le temps musical. Une
	// prise se termine donc quelques millisecondes après sa frontière musicale.
	class LoopTrack
	{
	public:
		void prepare(const TrackConfig& config, SamplePool& pool);

		TrackState state() const noexcept { return state_; }
		bool hasContent() const noexcept { return numLayers_ > 0 && hasSpan_; }
		const LoopSpan& span() const noexcept { return span_; }
		SamplePos loopLength() const noexcept { return loopLength_; }
		int numLayers() const noexcept { return numLayers_; }
		int numActiveLayers() const noexcept { return numActive_; }
		Tick recordStartTick() const noexcept { return recordStartTick_; }
		bool isCapturing() const noexcept;

		// --- Transitions (rendent false si refusées dans l'état courant) ---

		// Empty ou Armed -> Armed (attente de frontière).
		bool arm() noexcept;
		// Armed -> Empty.
		bool disarm() noexcept;

		// Empty ou Armed -> Recording. startTick peut être dans le passé
		// (pression tardive) : le tampon circulaire fournit le son manquant.
		// musicalToDevice : index d'appareil = position musicale + musicalToDevice
		// (inclut la latence aller-retour, figée pour toute la prise).
		bool beginRecording(const TempoMap& tempoMap, Tick startTick, std::int64_t musicalToDevice,
			const InputRingBuffer& ring) noexcept;

		// Recording -> Playing (ou Stopped si thenStop). endTick > startTick.
		bool closeRecording(const TempoMap& tempoMap, Tick endTick, bool thenStop) noexcept;

		// Playing -> Overdubbing, à la position musicale `at`.
		bool beginOverdub(const TempoMap& tempoMap, SamplePos at, std::int64_t musicalToDevice) noexcept;

		// Overdubbing -> Playing, à la position musicale `at`.
		bool endOverdub(SamplePos at) noexcept;

		// Stopped -> Playing.
		bool play() noexcept;

		// Playing ou Overdubbing -> Stopped (fondu de sortie).
		bool stop(SamplePos at) noexcept;

		// Recording -> Empty (abandon de la première prise).
		bool cancelRecording() noexcept;

		bool undo(SamplePos at) noexcept;
		bool redo() noexcept;

		// Tout état non vide -> Empty, après un fondu de sortie.
		bool clear() noexcept;

		// --- Traitement ---

		// Ajoute la lecture de la piste à out[c][0..numFrames), pour les positions
		// musicales [blockStart, blockStart + numFrames).
		void render(const TempoMap& tempoMap, SamplePos blockStart, int numFrames, float* const* out) noexcept;

		// Recopie dans les couches tout ce qui est disponible dans le tampon.
		void capture(const TempoMap& tempoMap, const InputRingBuffer& ring) noexcept;

	private:
		struct Layer
		{
			std::vector<int> chunks;				// blocs de la réserve, -1 si pas encore pris
			SamplePos startOffset = 0;				// position dans la boucle du premier échantillon
			SamplePos count = 0;					// échantillons valides
			float gain = 0.0f;						// gain lissé courant
		};

		enum class CaptureKind : std::uint8_t { None, First, Overdub };

		struct Capture
		{
			CaptureKind kind = CaptureKind::None;
			int layer = -1;
			SamplePos next = 0;			// prochaine position musicale à capturer
			SamplePos start = 0;		// position musicale du premier échantillon
			SamplePos end = INT64_MAX;	// fin musicale (exclue), hors fondu
			std::int64_t musicalToDevice = 0;
		};

		static constexpr int maxCaptures = 2;

		Capture* freeCapture() noexcept;
		void captureFirst(Capture& capture, const InputRingBuffer& ring) noexcept;
		void captureOverdub(Capture& capture, const TempoMap& tempoMap, const InputRingBuffer& ring) noexcept;
		void finishFirstLayer(const Capture& capture) noexcept;
		void resetContent() noexcept;
		void releaseLayer(Layer& layer) noexcept;
		bool ensure(Layer& layer, SamplePos index) noexcept;
		float read(const Layer& layer, int c, SamplePos index) const noexcept;
		float& sample(Layer& layer, int c, SamplePos index) noexcept;

		TrackConfig config_;
		SamplePool* pool_ = nullptr;
		std::vector<Layer> layers_;
		std::vector<std::vector<float>> preRoll_;	// son juste avant le début de la prise
		std::array<Capture, maxCaptures> captures_ {};

		TrackState state_ = TrackState::Empty;
		LoopSpan span_;
		bool hasSpan_ = false;
		SamplePos loopLength_ = 0;
		Tick recordStartTick_ = 0;
		int numLayers_ = 0;
		int numActive_ = 0;
		float trackGain_ = 0.0f;
		float trackTarget_ = 0.0f;
		bool clearing_ = false;
	};
}
