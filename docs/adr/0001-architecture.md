# ADR 0001 : architecture générale, licence et jalons

- **Statut** : Accepté
- **Date** : 2026-11-13
- **Contexte** : cahier des charges (`docs/cahier-des-charges.md`) et intégration future à la Tête NAM (fork `NoSaga0717/stompbox`).

## Contexte

Le cahier des charges vise un Standalone et un VST3 bâtis sur JUCE. Formats retenus : Standalone, VST3 et AU (Logic, macOS). Le projet doit aussi pouvoir s'intégrer à la Tête NAM, dont le moteur a d'autres contraintes :

| Contrainte de la tête | Source |
| --- | --- |
| Moteur stompbox-jack : C++20, sans JUCE, sans interface graphique, sous JACK, sur Pi 5 (Linux aarch64) | `stompbox/tete-nam/README.md` |
| Chaîne d'effets mono : `StompBox::compute(int count, float* input, float* output)` | `stompbox/stompbox-core/StompBox.h` |
| Transport limité au BPM (`SetBPM`), sans signature ni position | `stompbox/stompbox-core/StompBox.h` |
| MIDI lu en Python (`web.py`), commandes texte en TCP vers le moteur | `tete-nam/software/midi/README.md` |
| Interface web 800×480 (écran tactile, navigateur, appli Android) | `tete-nam/software/README.md` |
| Licence GPLv3 | `stompbox/LICENSE.md` |
| Deux NAM A2 et le rack prennent environ 25 à 35 % du DSP mesuré | `tete-nam/software/rack-effets/README.md` |

## Décisions

### D0. Priorité : le looper complet d'abord, la tête ensuite
Le produit principal est un looper complet en Standalone et en plugin. Le portage sur la Tête NAM vient après, éventuellement en version allégée (« lite »). Les choix ci-dessous servent d'abord le Standalone et le plugin ; ils gardent seulement le portage possible, sans le développer.

### D1. Licence : GPLv3
Le projet est publié sous GPLv3, ce qui le rend compatible avec stompbox. Conséquences : JUCE est utilisable sous AGPLv3, et des bibliothèques GPL (aubio, Rubber Band) restent possibles si elles apportent un gain mesuré.

### D1b. Langage : C++20 avec JUCE
Rust a été envisagé (sécurité mémoire, Cargo, compilation croisée simple). C++ est retenu parce que l'AU et une interface riche sont mûrs dans JUCE, alors que nih-plug ne fait pas d'AU. L'écosystème voisin (Stompbox, NAM, Signalsmith Stretch) est aussi en C++.

### D2. Moteur commun sans JUCE
C'est ce qui rend le portage possible plus tard : la tête n'a pas JUCE. La version lite réutilisera le même moteur avec moins de fonctions activées (moins de pistes, mono, sans Smart Quantize si le CPU ne suit pas), plutôt qu'un code à part.
`boohcleur-core` ne dépend que du C++20 et de la bibliothèque standard. JUCE ne sert qu'aux enveloppes Standalone, VST3, AU et à l'interface de bureau. Le même moteur pourra être appelé par un adaptateur stompbox ou un client JACK.

### D3. Modules

| Module | Dépend de | Rôle | Jalon |
| --- | --- | --- | --- |
| `core` | STL | Horloge musicale, pistes, couches, ordonnanceur de commandes, mixeur, tampon de pré-enregistrement, fondus. Seul code exécuté sur le thread audio. | J0 à J3 |
| `analysis` | core | Détection d'attaques, association à la grille, carte de déformation (warp map). Calcul pur et déterministe. | V2 |
| `render` | analysis, Signalsmith Stretch (MIT) | Rendu corrigé d'une couche à partir de la source et de la warp map. | V2 |
| `control` | core | MIDI Learn, gestes (appui court, long, double), actions, protocole de commandes et d'état commun aux interfaces. | J3 (`MidiControl`, logé dans `core` tant qu'il tient en un fichier), J5 |
| `session` | core, dr_libs, JSON | Manifeste versionné, WAV float32, écriture atomique, migration, récupération après crash. | J6 |
| `juce` | tous + JUCE | AudioProcessor, Standalone, VST3, AU, interface de bureau. | J2, J6 |
| `stompbox` | core, control | Adaptateur pour la tête (version lite). | Après V1 |

### D4. Threads
- **Audio (temps réel)** : `process()` seulement. Aucune allocation, aucun verrou bloquant, aucun accès disque. Tampons pris dans un pool pré-alloué selon un budget mémoire.
- **Commandes vers l'audio** : file SPSC sans verrou, chaque commande horodatée en échantillons.
- **Événements depuis l'audio** (états, prise fermée, niveaux) : file SPSC lue par le thread de contrôle.
- **Workers** (analyse, rendu) : priorité basse. Ils publient un résultat immuable par échange atomique de pointeur. L'audio ne bascule qu'à une frontière sûre, avec un fondu.
- **Disque** : thread dédié. La prise en cours est écrite en continu, puis les sauvegardes.
- **Libération mémoire** : jamais sur le thread audio. Les objets retirés passent par une file de libération vidée hors temps réel.

### D5. Temps
Le temps musical (mesures, temps, ticks) est la référence. La phase de chaque piste est calculée depuis une horloge musicale globale, jamais par cumul propre à la piste, pour qu'aucune dérive ne s'accumule entre boucles de longueurs différentes.

### D6. Infrastructure V1 imposée par le cahier
- **Tampon d'entrée circulaire toujours actif** : indispensable pour la règle « recadrer sur la dernière frontière » et pour ne pas couper la première attaque. Ce n'est pas la fonction « pré-enregistrement » de la V3, seulement son infrastructure.
- **Compensation de latence aller-retour** : décalage d'enregistrement réglable, calibration en boucle physique dans le Standalone.
- **Horodatage des gestes MIDI** : une commande porte l'heure du premier appui, même si l'appui long ou le double appui n'est reconnu qu'après.

### D7. Mode libre de la première boucle : désactivé en V1
En V1, la première boucle a une longueur définie par le tempo et le nombre de mesures. Le mode libre reviendra avec l'auto-tempo (V4) ou par un nouvel ADR.

### D8. Mode d'intégration à la tête : décidé au moment du portage
Deux options : effet compilé dans stompbox-jack, ou processus JACK séparé. Le choix se fera au portage, sur prototype Pi 5. D2 garde les deux possibles. D'ici là, la CI compile et teste le moteur en Linux aarch64 à chaque PR, ce qui détecte tôt une incompatibilité.

### D9. Interface : canevas libre de widgets
Inspiration : les loopers tactiles à canevas (type Loopy Pro), pour leurs principes et non pour leur apparence.
- Canevas sur plusieurs pages, avec des widgets placés et redimensionnés librement : boucle en anneau (progression, état, commande en attente), bouton, fader, niveau, tempo.
- Chaque widget est relié à une action de la même table que le MIDI (module `control`). Un bouton à l'écran et un switch au pied produisent la même commande, avec la même quantification.
- La disposition est une donnée JSON stockée dans le projet, pas du code. L'interface web de la tête pourra la relire.
- Pensée pour le tactile et la scène : grandes cibles, états lisibles de loin (couleur plus texte ou icône).

### D10. Nombre de boucles : libre, 6 par défaut, plafond réglable
Le moteur gère N boucles. Un projet neuf en propose 6. Un plafond logiciel (`maxLoops`) fixé par la cible limite N : la version lite de la tête pourra le bloquer à 4 selon la charge mesurée. Aucune structure ne suppose un nombre fixe, pour pouvoir faire évoluer ces valeurs.

### D11. Mémoire des boucles (J4)

Toutes les boucles puisent dans une réserve unique (`SamplePool`) réservée au démarrage et découpée en blocs de 16 384 images (0,34 s à 48 kHz). Une couche ne prend que les blocs de ce qu'elle a enregistré et les rend à l'effacement ou quand un nouvel overdub abandonne des couches annulées. Aucune allocation sur le thread audio. Réserve vide : la suite de la prise reste muette et l'interface l'annonce, rien ne casse.

Valeurs du bureau : 512 Mo (environ 23 min de son stéréo à 48 kHz, toutes boucles et couches confondues), boucle de 5 min au plus, 16 couches par boucle. Sans réserve partagée, six boucles de 60 s à 8 couches demandaient 1,1 Go réservé d'avance. Sur la tête, le budget sera fixé au portage (T1) ; une réserve plus petite suffit, le code ne change pas.

## Modèle de données (cible)

- **Project** : version du format, fréquence d'échantillonnage, tempo et signature (constants en V1, structure prête pour une tempo map), grille, pistes, sections (une section implicite en V1), mappings MIDI, réglages.
- **Track** : état, ancre musicale, longueur en mesures et en échantillons, mix (gain, pan, mute, solo), couches, pile undo/redo, réglages de quantification par défaut.
- **Layer** : source audio, décalage dans la boucle, gain, actif ou annulé (l'undo désactive, il ne supprime pas), réglages Smart Quantize propres.
- **AudioSource** : WAV float32 immuable, empreinte d'intégrité.
- **TransientAnalysis** : paramètres et version de l'algorithme, marqueurs (position, confiance, auto ou manuel, supprimé).
- **WarpMap** : paires (position source, position cible), dérivées de l'analyse.
- **RenderCache** : rendu jetable, recalculable, jamais la référence.

## Machines à états (cible)

**Piste** (Mute et Solo sont des attributs orthogonaux) :

| État | Commande | Suivant |
| --- | --- | --- |
| Empty | Rec | Armed |
| Armed | frontière atteinte | Recording |
| Armed | Stop | Empty |
| Recording | Rec ou Play | Playing (fermeture de la prise) |
| Playing | Rec | Overdubbing (nouvelle couche) |
| Overdubbing | Rec ou Play | Playing |
| Playing, Overdubbing | Stop | Stopped |
| Stopped | Play | Playing |
| tout état non vide | Clear confirmé | Empty |

Toute autre combinaison est rejetée avec un événement « commande refusée ». Une piste a au plus une commande en attente.

**Correction d'une couche** : Off, Analyzing, Analyzed, Rendering, Ready (attend une frontière), Active ; Failed ramène à la lecture de l'original. Pendant un nouveau rendu, l'ancien continue de jouer.

**Geste MIDI** : Idle, Pressed, puis appui court, double ou long selon les seuils. L'horodatage transmis est celui du premier appui.

**Transport** : Internal (Standalone) ou HostFollowing (VST3, AU), avec une politique documentée pour play, stop, seek, bouclage, changement de BPM ou de signature.

**Sauvegarde** : Idle, Snapshot, Writing (fichiers temporaires), Commit (renommage atomique), Idle ou Error.

## Jalons

Principe : avoir un looper jouable à la guitare dès J2, puis au pied dès J3, pour juger le comportement réel le plus tôt possible. Les fonctions s'ajoutent ensuite autour.

| Jalon | Contenu | Validation |
| --- | --- | --- |
| J0 | Dépôt, CMake, CI, ADR | Compilation et tests verts sur la CI |
| J1 | Horloge musicale, conversions temps ↔ échantillons | Aucune dérive sur 1 h simulée à 44,1, 48 et 96 kHz |
| J2 | Une piste jouable : rec, play, overdub, undo, tampon circulaire, ordonnanceur et tolérance, fondus, **dans un Standalone JUCE minimal** (périphériques, décalage de latence réglable à la main) | Banc hors ligne à clics synthétiques sans discontinuité ; essai à la guitare |
| J3 | MIDI de base : learn, gestes (court, long, double), mappings | Test V1-3, rejeu de MIDI horodaté ; essai au pédalier |
| J4 | N boucles (6 par défaut, plafond réglable), longueurs différentes, mixeur, niveaux, pool mémoire | Test V1-1, budget RAM respecté, plafond respecté |
| J5 | Sessions : sauvegarde, chargement, récupération, erreurs | Tests V1-2 et V1-7 |
| J5b | Éditeur de canevas (D9) : pages, widgets, liaison aux actions, disposition sauvegardée | Disposition rechargée à l'identique ; chaque widget déclenche la même commande que son équivalent MIDI |
| J6 | Plugins VST3 et AU, transport hôte | pluginval, auval, test V1-5 |
| J7 | Calibration de latence en boucle physique, campagne V1 sur la machine de référence | Test V1-6, mesures publiées |
| Q1 à Q5 | V2 : détecteur, mapper, rendu, intégration asynchrone, édition des marqueurs | Tests V2-1 à V2-6 |
| T1 | Portage lite sur la tête : prototype Pi 5, choix D8, périmètre de la version lite | Compile sur le Pi, aucun xrun avec 2 NAM actifs ; décision D8 |

## Arbitrages encore ouverts (valeur proposée entre parenthèses)

1. OS prioritaires (Windows et macOS pour le bureau, Linux aarch64 pour le moteur).
2. Mono ou stéréo dans la tête (mono dans la tête, stéréo sur le bureau ; le moteur gère les deux).
3. Signal enregistré dans la tête : avant ou après NAM (après la chaîne).
4. ~~Budget RAM, longueur maximale de boucle, profondeur d'undo~~ : tranché en J4, voir D11.
5. Changement de BPM côté DAW en V1 (tempo du projet verrouillé, avec avertissement).
6. Fenêtre de tolérance après une frontière et durée du tampon circulaire (à mesurer).
7. Max Shift : borner ou ignorer l'attaque hors fenêtre (ignorer).
8. Convention de swing.
9. Écrêtage de la somme : limiteur doux ou marge fixe.
10. État du plugin (VST3, AU) : dossier projet référencé (chemin relatif quand c'est possible).
11. Format de projet : dossier JSON + WAV float32.
12. Machine de référence et première interface audio.
13. Chemin du MIDI dans la tête : `web.py` ou JACK (JACK, pour l'horodatage).

## Licences des dépendances prévues

| Bibliothèque | Licence | Usage |
| --- | --- | --- |
| JUCE 8 | AGPLv3 ou commercial | Enveloppes (Standalone, VST3, AU) et interface de bureau |
| VST3 SDK 3.8 | MIT | Format VST3 (via JUCE) |
| Signalsmith Stretch | MIT | Rendu Smart Quantize |
| dr_libs | domaine public ou MIT-0 | Lecture et écriture WAV |
| Catch2 | BSL-1.0 | Tests |
