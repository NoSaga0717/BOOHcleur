# Cahier des charges — Looper guitare multipiste avec Smart Quantize

**Version :** 1.0 — spécification initiale à transmettre à un LLM de développement  
**Statut :** cadrage fonctionnel et technique ; les points signalés « à arbitrer » ne doivent pas être inventés.  
**Produit :** application audio **autonome (Standalone)** et **plugin VST3**, partageant le même moteur. AU et Linux sont des extensions envisageables.

## 1. Objectif du projet

Développer un looper logiciel haut de gamme, orienté guitare, pilotable intégralement au MIDI (notamment au pied), capable d'enregistrer plusieurs pistes, de construire un morceau à sections et surtout de **corriger le timing des attaques à l'intérieur des enregistrements audio**. Le système doit rester utilisable sur scène : audio stable, commandes prévisibles, sauvegarde/restauration fiable.

**Différenciation fondamentale :** distinguer deux opérations indépendantes :
- **Loop Quantize** : aligne début, fin et transitions des boucles sur la grille musicale.
- **Smart Quantize (Audio Quantize)** : détecte les attaques des notes/accords à l'intérieur d'une prise audio et corrige leur position temporelle, en préservant autant que possible timbre, hauteur, sustain et naturel du jeu.

Ne jamais présenter une simple quantification des frontières de boucle comme une correction note par note.

## 2. Livrables et cibles

1. **Standalone** utilisable sans DAW, avec entrée/sortie audio, configuration de l'interface, MIDI, tempo et transport internes.
2. **VST3** utilisant le tempo, la signature et le transport fournis par le DAW lorsque disponibles, avec stratégie de repli explicite sinon.
3. **Moteur audio commun** aux deux cibles, séparé de l'interface et du code propre à chaque hôte.
4. **Projet sauvegardable** : données musicales, pistes, couches et sources audio originales, réglages, sections, affectations MIDI.
5. **Tests automatisés** du moteur et scénarios manuels de non-régression.
6. Documentation développeur (architecture, compilation, dépendances/licences, protocole MIDI, limites connues).

**À arbitrer avant développement ciblé :** systèmes d'exploitation prioritaires, cartes/contrôleurs MIDI à tester, format des projets, enveloppe CPU/RAM, politique des licences des bibliothèques tierces. Ne pas bloquer le moteur sur un modèle de contrôleur particulier.

## 3. Phasage et priorités

### V1 — Socle fiable (P0)
- Six pistes audio stéréo indépendantes au minimum ; accepter aussi les sources mono.
- Record, Play, Stop, Overdub, Mute, Solo, Clear par piste, avec états explicites.
- Overdub **non destructif** : chaque nouvelle prise conservée comme couche indépendante, sans fusion irréversible.
- Undo/Redo multiniveaux par piste, dans la limite des ressources configurées.
- Horloge musicale, tempo, signature, quantification des frontières de boucle et des changements d'état.
- Longueurs de pistes distinctes mais synchronisées (p. ex. 4/8/16 mesures).
- Mixeur : volume, panoramique, mute/solo, indication de niveau.
- MIDI Learn universel + affectations sauvegardées + commandes utilisables sans souris.
- Mode Standalone + VST3, traitement audio stable, gestion des interfaces audio/MIDI.
- Enregistrement et chargement de projets **avec leurs médias**, restauration des sessions.
- Crossfades anti-clics, comportement défini en cas d'erreurs, de disque plein, de déconnexion de périphérique, d'arrêt d'hôte.

### V2 — Smart Quantize (P0 de différenciation, après fiabilisation du moteur)
- Analyse des transitoires des prises audio enregistrées.
- Grille ajustable : temps, croches, doubles-croches, triolets ; prise en compte du tempo, de la signature et du swing.
- Force de correction 0–100 %, tolérance maximale en millisecondes, sensibilité de détection.
- Correction **non destructive**, réversible et modifiable ultérieurement pour chaque couche/prise.
- Édition/validation facultative des marqueurs de transitoires détectés.
- Déplacement temporel avec préservation de la hauteur ; crossfades et, lorsque nécessaire, time-stretch local.
- Profil d'analyse « accords/rythmique », « notes/riffs » ; profil « arpèges/sustain » expérimental si qualité suffisante.
- Déclenchement automatique d'une analyse après fermeture de prise, **sans bloquer le thread audio**.

### V3 — Arrangement et scène (P1)
- Sections musicales (intro, A, B, refrain, pont, outro) et changements quantifiés.
- Snapshots d'état (mixage, états de pistes, sections) rappelables au MIDI.
- Auto Record / seuil de détection, pré-enregistrement circulaire, count-in, tap tempo.
- Enregistrement ciblé/remplacement d'une prise, export WAV multipiste original et corrigé.
- Variation du tempo sans variation de hauteur (time-stretch), reverse, half-speed.
- Synchronisation MIDI Clock avancée, retour MIDI pour LED/contrôleurs compatibles.
- Routage d'entrées/sorties audio avancé et traitement brut/traité configurable.

### V4 — Extensions (P2, hors périmètre initial)
- Batterie interne, patterns, effets guitare, intégration d'hôtes de plugins tiers.
- Auto-tempo à partir d'une première prise libre, après confirmation utilisateur.
- Quantification adaptative/groove qui tente de préserver les décalages expressifs.
- Automations et contrôle via pédale d'expression ; AU/Linux si retenus.

## 4. Modèle de données et comportements

**Hiérarchie recommandée :** Projet → Sections → Pistes → Couches/Prises → Sources audio et métadonnées. Tempo/signature/grille/transport appartiennent au projet ; une section définit quels contenus de pistes sont actifs et leur organisation. Les fichiers audio sources restent immuables ; les éditions sont enregistrées comme paramètres/marqueurs, non comme remplacements destructifs.

**États d'une piste** (à formaliser en machine à états) : Empty, Armed, Recording, Playing, Overdubbing, Stopped, Muted ; les attributs « Muted » et « Solo » peuvent être orthogonaux selon l'implémentation. Transitions invalides ignorées ou signalées, jamais indéfinies.

**Enregistrement :**
- Premier enregistrement : fixe la longueur de la boucle selon les réglages (tempo/mesures), ou propose un mode libre avec validation ultérieure.
- Les nouveaux overdubs créent des couches alignées à la timeline de la piste et indépendamment annulables.
- Les commandes MIDI de Record, Stop et changement de section peuvent être immédiates ou planifiées à la prochaine subdivision/mesure, selon le paramétrage.
- Les longueurs de boucles différentes doivent pouvoir coexister sans dérive et sans changement implicite du tempo global.
- Gérer explicitement une action reçue juste **après** une frontière musicale : recadrage sur la dernière frontière dans une fenêtre de tolérance configurable, sinon planification à la suivante. Documenter les conséquences audio (recadrage de la fin) et éviter les clics.

**Mixage/lecture :** monitoring live indépendant de la version quantifiée en cours d'analyse, gestion des niveaux et prévention de l'écrêtage à la somme des couches. Aucun changement audio brutal à l'activation d'une correction ; appliquer une transition sûre à la frontière de boucle ou un fondu court.

## 5. Smart Quantize — spécification de référence

### 5.1 Pipeline
1. Enregistrer intégralement la **prise audio originale** avec horodatage sample-accurate.
2. Détecter les attaques candidates (enveloppe + variation spectrale, éventuellement détection multibande).
3. Estimer la confiance de chaque détection ; offrir sensibilité, suppression/ajout manuel de marqueurs et garde-fous contre les faux positifs.
4. Associer chaque attaque à une position cible de la grille, avec swing et fenêtre maximale de déplacement.
5. Appliquer une correction temporelle non destructive : repositionnement de segments et, si nécessaire, étirement local pour préserver transitions et sustain.
6. Prévisualiser et relire le rendu sans délai perceptible ajouté au **monitoring live** ; conserver l'original et les paramètres pour réédition.

**Règle théorique :** `t_corrigé = t_original + force × (t_cible - t_original)`, où `force ∈ [0,1]`, et le déplacement est borné par `max_shift_ms`. Ne pas appliquer cette formule directement aux échantillons : elle sert au calcul de positions-cibles des transitoires.

### 5.2 Paramètres utilisateur
- Quantification audio : OFF / ON (par piste et idéalement par couche).
- Force : 0–100 % (défaut **proposé** 75 %, à valider).
- Grille : 1/4, 1/8, 1/16 et variantes triolets ; swing 0–100 % selon une convention explicitée dans l'UI.
- Max Shift : valeur en ms ; éviter de déplacer une attaque vers un temps manifestement erroné.
- Sensibilité des transitoires / seuil de confiance.
- Mode de traitement : rythmique/accords, riffs/notes isolées, expérimental arpèges/sustain.
- A/B original vs corrigé et réinitialisation de la correction.

### 5.3 Limites et gestion des cas difficiles
- Sur une guitare polyphonique, un accord ou une résonance n'est pas une série de notes facilement séparables. Ne pas promettre la quantification indépendante de chaque note d'un accord enregistré en audio mixé.
- Bends, legato, arpèges liés, reverb/delay imprimés dans la prise : analyse plus ambiguë et artefacts possibles. Prévoir marquage manuel et bascule vers signal original.
- Ne pas confondre correction de timing audio et correction de hauteur/pitch.
- Par défaut, quantifier **après** une prise plutôt que manipuler irréversiblement le signal pendant son enregistrement ; l'analyse peut démarrer en parallèle si elle reste hors thread audio.
- Quand le traitement n'est pas prêt, continuer la lecture de la version précédente/originale ; activer le nouveau rendu seulement à un instant musical sûr.

## 6. MIDI et contrôle au pied

**Entrées :** MIDI Note On/Off, CC, Program Change ; sélection du canal et du port, filtrage configurable et MIDI Learn pour chaque action. Distinguer appui, relâchement et maintien ; détecter appui long/double appui à partir des événements MIDI **sans** dépendre d'une marque précise de pédalier. Définir un comportement anti-rebond et une résolution de conflits d'assignation.

**Actions assignables :** Record/Play/Overdub, Stop, Undo/Redo, Clear (avec garde contre les suppressions accidentelles), piste précédente/suivante, mute/solo, section précédente/suivante/nommée, Tap Tempo, métronome, Play/Stop global, quantification on/off, Snapshots, volume/pan et intensité de quantification.

**Modes de déclenchement :** immédiat, fin de temps, fin de mesure, fin de boucle ; commandes futures affichées comme « en attente ». Prévoir le feedback MIDI LED/état comme fonctionnalité P1, sur port de sortie configurable.

**Exemple de mapping (suggestion, jamais figé) :**

| Bouton | Court | Long |
|---|---|---|
| 1 | Rec / Play / Overdub piste active | Undo |
| 2 | Stop piste active | Clear avec confirmation |
| 3 | Piste précédente | Mute piste |
| 4 | Piste suivante | Solo piste |
| 5 | Section précédente | Retour première section |
| 6 | Section suivante à la prochaine mesure | Accès section nommée |
| 7 | Tap tempo | Métronome |
| 8 | Play / Stop global | Stop immédiat |

## 7. Tempo, synchronisation et sections

- **Standalone** : horloge interne maître, BPM et signature définissables, count-in et métronome.
- **VST3** : suivre l'horloge et le transport DAW quand disponibles. Définir précisément le comportement lors d'un seek, du bouclage du DAW, d'un changement de BPM/signature ou d'une interruption.
- **MIDI Clock** : émission/réception, priorité maître/esclave explicite, gestion jitter, perte d'horloge, start/stop/continue et reprise ; module P1 si les bases V1 sont stabilisées.
- Sections : lancer section B en fin de mesure sans désynchroniser les pistes, et conserver la possibilité de revenir en section A.
- Par défaut, les événements sont rattachés à une timeline musicale absolue (mesures/temps/ticks) et convertis en échantillons pour l'audio.

## 8. Routage et intégration guitare/NAM

- Entrée audio mono/stéréo, sortie stéréo, niveaux indépendants et monitoring à faible latence.
- Choix de source **dry** (guitare non traitée) ou **wet** (signal ayant déjà traversé un ampli/effet) ; conserver une architecture permettant le double chemin si l'interface et l'hôte le permettent.
- Le looper **n'a pas à embarquer NAM dès la V1**. Il doit pouvoir être placé avant ou après une chaîne d'effets dans un DAW, et accueillir ultérieurement un routage interne dry/wet.
- Définir explicitement quel signal est enregistré et quel signal est entendu ; ne pas supposer qu'un plugin VST3 peut accéder librement à toutes les entrées physiques de l'interface.

## 9. Architecture logicielle et exigences non fonctionnelles

**Approche suggérée :** JUCE/C++ pour moteur commun, Standalone et VST3 ; bibliothèque de time-stretch telle que Signalsmith Stretch (vérifier adaptation technique/licence). Séparer au minimum : `AudioEngine`, `TransportClock`, `LoopTrack`, `TakeLayer`, `QuantizeEngine`, `MidiRouter`, `SessionStore`, `UI`. Les noms sont indicatifs.

**Temps réel :**
- Callback audio déterministe ; pas d'accès disque, de blocage, de mutex potentiellement bloquant, ni d'allocation mémoire non bornée sur le thread audio.
- Traitements lourds (analyse de transitoires, warping/rendu, sauvegarde) en tâches hors thread audio ; transfert de résultats atomique/sûr aux frontières audio.
- Horodatage MIDI/transport précis ; conversions temps musical ↔ échantillons testables.
- Fondu aux raccords, gestion des sauts de transport et de l'overdub sans clics ; pas de dérive cumulée entre pistes.
- Contrôle de la charge CPU/RAM/disque ; messages clairs en cas d'indisponibilité de périphérique ou d'échec d'export.

**Moteur et données :** échantillonnage 44,1/48 kHz au minimum ; tester 96 kHz si raisonnable. Formats 32-bit float en interne, sans promettre une qualité supérieure de la chaîne entière. Le système doit résister aux buffers 64/128/256/512 échantillons si le matériel et l'hôte le permettent. Les métriques CPU et latence seront mesurées sur **une machine de référence à choisir**, et non inventées.

**Interface :** écran lisible en répétition/scène : états colorés distincts avec texte/icônes, timeline/mesures, pistes, section, statut de quantification, transport, commandes en attente, niveaux et warnings. Fonctionnement intégral au MIDI pour l'usage courant ; configuration avancée au clavier/souris autorisée.

**Sauvegarde :** projet portable avec manifeste versionné, audio source, couches et métadonnées des transitoires, réglages, état de chaque piste, sections et mappings. Écriture atomique et récupération après crash autant que possible. Versionner le format et prévoir migration. Éviter les liens absolus fragiles vers des fichiers extérieurs ; vérifier intégrité et médias manquants à l'ouverture.

## 10. Critères d'acceptation et tests

### V1 — Tests fonctionnels obligatoires
1. Créer 6 pistes stéréo, enregistrer 4 mesures sur piste 1, 8 sur piste 2, 16 sur piste 3 ; vérifier un bouclage synchronisé et stable.
2. Ajouter 3 overdubs sur une piste, annuler 2 couches, rétablir 1, sauvegarder puis rouvrir : le projet reste cohérent.
3. Enregistrer/arrêter une boucle via CC ou Note MIDI ; tester pressions légèrement avant/après frontière musicale et vérifier la règle de quantification documentée.
4. Pistes mute/solo/volume, changement de piste active au pied : aucune confusion de piste ni artefact audible aux transitions.
5. Simuler Stop/Play, déplacement de lecture, changement de tempo et de signature côté DAW ; vérifier la politique de resynchronisation.
6. Tester au moins 44,1 et 48 kHz et buffers 64/128/256 échantillons sur la machine de référence ; mesurer xruns, CPU, RAM, latence et dérive. **Aucun dropout imputable au looper durant le scénario nominal convenu.**
7. Couper une source audio, renommer un dossier projet, interdire l'écriture disque : afficher une erreur exploitable sans corrompre les médias existants.

### V2 — Tests Smart Quantize obligatoires
1. Générer une prise test de 4 mesures à 120 BPM avec transitoires nets placés à ±20/50/80 ms d'une grille connue. À 100 % et dans la fenêtre autorisée, vérifier les positions-cibles calculées ; à 50 %, vérifier un déplacement de moitié, à 0 % aucun déplacement.
2. Désactiver la quantification : retrouver **bit pour bit le fichier source original**, lequel n'a pas été modifié.
3. Ajouter deux overdubs avec réglages de correction différents : chaque couche se corrige indépendamment.
4. Tester accords tenus, silences, legato, arpèges, bends, réverb imprimée : documenter faux positifs et artefacts, sans exiger une perfection impossible.
5. Pendant une analyse lourde, la lecture et le monitoring doivent continuer normalement. Le nouveau rendu n'entre en lecture qu'à un instant sûr.
6. A/B original/corrigé, sauvegarde/rechargement et recalcul du rendu : résultat stable et commandes réversibles.

Les tolérances chiffrées de fidélité audio, seuils CPU et niveaux de latence seront définis après un prototype mesuré. La réussite ne peut pas être déduite du seul fait que des notes s'alignent visuellement.

## 11. Hors périmètre initial

- Quantification temps réel du signal **pendant qu'on joue** avec prédiction des notes futures.
- Séparation polyphonique parfaite de notes au sein d'un accord audio.
- Correction de hauteur, remplacement automatique d'erreurs harmoniques, génération musicale par IA.
- Hébergement de VST tiers, éditeur de batterie avancé, collaboration cloud, app mobile, compatibilité avec un pédalier unique.

## 12. Consignes à l'autre LLM développeur

1. **Commencer par proposer** l'architecture, le modèle de données, les machines à états et un plan d'implémentation incrémental. Ne pas coder immédiatement toute la V1–V4.
2. Relever les arbitrages réellement bloquants (OS, distribution, machine de référence, première interface audio, modèle de tempo maître, conventions de quantification) et poser des questions ciblées ; proposer une valeur par défaut clairement identifiée si un arbitrage n'est pas bloquant.
3. Respecter la séparation audio temps réel / tâches asynchrones / interface. Fournir pour chaque module contrats d'API et tests.
4. **Ne jamais substituer** Loop Quantize à Smart Quantize, ni prétendre quantifier indépendamment toutes les notes d'un signal polyphonique.
5. Utiliser des bibliothèques existantes quand elles réduisent le risque, mais contrôler licences, maintenance et contraintes techniques avant intégration.
6. Livrer par jalons compilables et testables, en priorisant la fiabilité audio plutôt que l'accumulation de fonctions.
7. À chaque jalon, rendre : code, commandes de build, tests, décisions techniques, problèmes connus et prochaines étapes ; éviter les assertions non testées du type « zéro latence ».
8. Conserver un document de décisions (ADR) et ce cahier des charges comme référence ; toute modification de périmètre doit être signalée avant implémentation.

## 13. Définition de « terminé »

Un jalon n'est terminé que si : la compilation fonctionne sur la plateforme cible retenue ; les tests pertinents passent ; les comportements MIDI et audio sont reproductibles ; la sauvegarde/restauration ne perd aucune prise ; la documentation explique les limites ; et aucun défaut connu bloquant pour l'usage en répétition/scène ne subsiste. La V2 est réussie uniquement si elle corrige réellement les **attaques internes** d'une prise audio, avec une méthode non destructive et des artefacts évalués.
