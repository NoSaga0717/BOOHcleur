#pragma once

#include "boohcleur/core/MusicalTime.h"

namespace boohcleur
{
	// Divisions de la grille de quantification des frontières (Loop Quantize).
	// Ce n'est PAS la grille du Smart Quantize, qui corrige les attaques internes
	// d'une prise et aura ses propres réglages (swing, force, max shift).
	enum class GridDivision
	{
		Immediate,			// pas de quantification
		Bar,				// début de mesure
		Beat,				// temps (dépend du dénominateur de la signature)
		Eighth,				// croche
		Sixteenth,			// double-croche
		QuarterTriplet,		// triolet de noires
		EighthTriplet,		// triolet de croches
		SixteenthTriplet	// triolet de doubles-croches
	};

	// Longueur d'une division en ticks (0 pour Immediate).
	Tick divisionLength(GridDivision division, const TimeSignature& signature) noexcept;

	// La grille repart de chaque début de mesure : dans une mesure de 7/8 en
	// noires, les frontières sont 0, 960, 1920, 2880 puis la mesure suivante à 3360.
	// Le début de mesure est toujours une frontière.

	// Plus grande frontière <= tick.
	Tick boundaryAtOrBefore(Tick tick, GridDivision division, const TimeSignature& signature) noexcept;

	// Plus petite frontière > tick (strictement après).
	Tick boundaryAfter(Tick tick, GridDivision division, const TimeSignature& signature) noexcept;

	// Résultat de la règle de déclenchement d'une commande quantifiée.
	struct TriggerResolution
	{
		Tick target = 0;			// frontière où la commande s'applique
		bool retroactive = false;	// true : frontière déjà passée, recadrage en arrière
	};

	// Règle de déclenchement (cahier des charges §4) :
	// - appui exactement sur une frontière : appliqué à cette frontière ;
	// - appui au plus `lateToleranceTicks` après une frontière : recadré sur cette
	//   frontière passée (rétroactif, nécessite le tampon circulaire, ADR D6) ;
	// - sinon : planifié à la frontière suivante.
	// Avec Immediate, la commande s'applique au tick de l'appui.
	TriggerResolution resolveTrigger(Tick pressTick, GridDivision division, const TimeSignature& signature,
		Tick lateToleranceTicks) noexcept;
}
