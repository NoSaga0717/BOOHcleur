#pragma once

#include "boohcleur/core/SpscQueue.h"

#include <array>
#include <atomic>
#include <climits>
#include <cstdint>

namespace boohcleur
{
	// Pilotage au pied : sources MIDI, gestes (appui court, long, double),
	// affectations et MIDI Learn. Indépendant de toute marque de pédalier.
	//
	// Tout tourne sur le thread audio (les événements MIDI arrivent avec le bloc
	// audio, à l'échantillon près). L'interface envoie ses demandes par une file
	// et lit l'état publié (seqlock), sans jamais bloquer le thread audio.

	enum class MidiSourceKind : std::uint8_t { None, Note, ControlChange, ProgramChange };

	struct MidiSource
	{
		MidiSourceKind kind = MidiSourceKind::None;
		std::uint8_t channel = 0;	// 1 à 16
		std::uint8_t number = 0;	// note, numéro de CC ou de programme

		friend constexpr bool operator==(const MidiSource&, const MidiSource&) = default;
	};

	// Comment un message devient un appui.
	enum class PressMode : std::uint8_t
	{
		Momentary,		// Note On / Note Off, ou CC >= 64 / CC < 64 : appui et relâchement
		EveryMessage	// chaque message est un appui bref (Program Change, pédales « toggle »)
	};

	enum class Gesture : std::uint8_t { Short, Long, Double };

	// Actions assignables. Les actions de boucle visent une boucle précise ou,
	// avec track = selectedTrack, la boucle sélectionnée.
	enum class ControlAction : std::uint8_t
	{
		None,
		TrackMain,			// Rec / Play / Overdub
		TrackStop,
		TrackPlay,
		TrackUndo,
		TrackRedo,
		TrackClear,			// demande une confirmation : deux déclenchements en 2 s
		TransportToggle,
		MetronomeToggle,
		SelectNext,			// boucle sélectionnée suivante / précédente
		SelectPrevious,
		Count
	};

	inline constexpr std::uint8_t selectedTrack = 0xFF;

	struct Binding
	{
		MidiSource source;
		PressMode mode = PressMode::Momentary;
		Gesture gesture = Gesture::Short;
		ControlAction action = ControlAction::None;
		std::uint8_t track = selectedTrack;

		friend constexpr bool operator==(const Binding&, const Binding&) = default;
	};

	struct GestureSettings
	{
		double longPressMs = 500.0;
		double doubleTapMs = 300.0;
		double debounceMs = 30.0;
		double clearConfirmMs = 2000.0;
	};

	// Message MIDI brut, horodaté en échantillons de l'appareil.
	struct MidiEvent
	{
		std::uint8_t status = 0;
		std::uint8_t data1 = 0;
		std::uint8_t data2 = 0;
		std::int64_t device = 0;
	};

	// Action déclenchée. pressDevice est l'instant du PREMIER appui, même si le
	// geste n'est reconnu qu'après (appui long, attente d'un double appui) : la
	// quantification se fait donc sur le moment où le pied a appuyé.
	struct Trigger
	{
		ControlAction action = ControlAction::None;
		std::uint8_t track = 0;
		std::int64_t pressDevice = 0;
	};

	// Liste fixe d'actions produites pendant un bloc (aucune allocation).
	struct TriggerList
	{
		std::array<Trigger, 32> items {};
		int count = 0;

		void add(const Trigger& trigger) noexcept
		{
			if (count < static_cast<int>(items.size()))
				items[static_cast<std::size_t>(count++)] = trigger;
		}
	};

	// Demandes de l'interface vers le thread audio.
	struct ControlRequest
	{
		enum class Type : std::uint8_t { StartLearn, CancelLearn, AddBinding, RemoveBinding, SetMode, ClearAll, SetSettings };

		Type type = Type::CancelLearn;
		Binding binding {};			// StartLearn (action, geste, piste), AddBinding
		int index = -1;				// RemoveBinding, SetMode
		PressMode mode = PressMode::Momentary;
		GestureSettings settings {};
	};

	// État publié pour l'interface.
	struct ControlState
	{
		static constexpr int maxBindings = 64;

		std::array<Binding, maxBindings> bindings {};
		int numBindings = 0;
		bool learning = false;
		Binding learnTarget {};
		MidiSource lastSource {};		// dernier message reçu (affichage)
		std::uint8_t lastValue = 0;
		std::uint32_t messageCount = 0;
		bool clearArmed = false;
		GestureSettings settings {};
	};

	class MidiControl
	{
	public:
		static constexpr int maxBindings = ControlState::maxBindings;

		// Hors temps réel, audio arrêté (consomme aussi les demandes en attente).
		void prepare(double sampleRate) noexcept;

		// Thread de l'interface (un seul producteur).
		bool request(const ControlRequest& request) noexcept { return requests_.push(request); }
		ControlState snapshot() const noexcept;

		// Thread audio, dans l'ordre : demandes, messages du bloc, minuteries.
		void processRequests() noexcept;
		void onMidi(const MidiEvent& event, TriggerList& out) noexcept;
		void update(std::int64_t nowDevice, TriggerList& out) noexcept;
		void publish() noexcept;

		// Décodage d'un message (public pour les tests).
		struct Decoded
		{
			MidiSource source;
			bool press = false;
			bool release = false;
		};
		static Decoded decode(const MidiEvent& event, PressMode mode) noexcept;

	private:
		struct Button
		{
			MidiSource source;
			bool pressed = false;
			bool consumed = false;		// appui déjà utilisé (double, learn, court immédiat)
			bool longFired = false;
			bool waitingDouble = false;
			bool releasePending = false;	// relâchement à confirmer (anti-rebond)
			std::int64_t pressDevice = 0;
			std::int64_t firstPressDevice = 0;
			std::int64_t releaseDevice = INT64_MIN / 2;
			std::int64_t doubleDeadline = 0;
			std::int64_t pendingReleaseDevice = 0;
		};

		void handlePress(const MidiSource& source, std::int64_t t, TriggerList& out) noexcept;
		void handleRelease(const MidiSource& source, std::int64_t t, TriggerList& out) noexcept;
		void releaseNow(Button& button, std::int64_t t, TriggerList& out) noexcept;
		void addBinding(const Binding& binding) noexcept;
		void fire(const MidiSource& source, Gesture gesture, std::int64_t pressDevice, TriggerList& out) noexcept;
		bool hasGesture(const MidiSource& source, Gesture gesture) const noexcept;
		PressMode modeFor(const MidiSource& source) const noexcept;
		Button* button(const MidiSource& source) noexcept;
		void learn(const MidiSource& source, PressMode mode) noexcept;
		void removeBinding(int index) noexcept;
		std::int64_t ms(double milliseconds) const noexcept;

		double sampleRate_ = 48000.0;
		ControlState state_ {};
		std::array<Button, maxBindings> buttons_ {};
		int numButtons_ = 0;
		std::int64_t clearArmedUntil_ = INT64_MIN;
		std::int64_t now_ = 0;

		SpscQueue<ControlRequest, 128> requests_;

		// Seqlock : version impaire pendant l'écriture.
		mutable std::atomic<std::uint32_t> version_ { 0 };
		ControlState published_ {};
	};
}
