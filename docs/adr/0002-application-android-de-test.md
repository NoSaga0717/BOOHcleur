# ADR 0002 : application Android de test

- **Statut** : Accepté
- **Date** : 2026-11-14
- **Contexte** : Alex veut essayer le looper en déplacement, sur son téléphone, avec le micro intégré.

## Contexte

Le cahier des charges range l'« app mobile » hors périmètre de la V1. La demande ici est un outil de test, pas un produit : vérifier le comportement du moteur (prise, overdub, undo, quantification) sans ordinateur. Le moteur `boohcleur-core` étant en C++20 pur (ADR 0001, D2), il se compile tel quel pour Android.

## Décisions

### D1. Une APK de test, hors périmètre produit
L'application Android n'ajoute aucune fonction au moteur et ne modifie pas le Standalone. Elle n'est pas publiée sur un store : APK signée avec une clé de debug, installée à la main. Si une vraie version mobile devient un objectif, elle fera l'objet d'un autre ADR.

### D2. JUCE pour Android, construit par Gradle
- Le code natif (`android/native`) est une cible `juce_add_gui_app` nommée `juce_jni` (nom imposé par la classe Java de JUCE), compilée depuis le `CMakeLists.txt` racine avec l'option `BOOHCLEUR_BUILD_MOBILE`. Elle réutilise `app/Source` (processeur, widgets) sans le modifier.
- Le projet Gradle (`android/`) reprend ce que génère l'exportateur Android du Projucer : classes Java `com.rmsl.juce` de JUCE, options CMake, manifeste. L'API CMake de JUCE ne produit pas d'APK elle-même.
- Audio : Oboe (AAudio, Android 8 minimum), fourni avec JUCE. Permission `RECORD_AUDIO` demandée au lancement ; refusée, l'app tourne sans entrée.
- Le C++ est compilé en Release même pour le build debug : un moteur audio en `-O0` décroche.

### D3. Interface tactile minimale
Un écran : boucle en anneau, bouton principal Rec / Play / Overdub, stop, undo / redo, effacer, transport, quantification, métronome, écoute de l'entrée, latence et tempo. Mise en page portrait ou paysage. Le canevas libre (ADR 0001, D9) reste prévu en J5b et remplacera cet écran le moment venu.

### D4. Distribution par la CI
Le workflow `Android` construit l'APK à chaque PR (artefact) et, sur `main`, met à jour une pré-version GitHub `android-test` pour un téléchargement direct depuis le téléphone. La clé de debug est conservée en cache CI pour que les mises à jour s'installent par-dessus.

## Licences ajoutées

| Bibliothèque | Licence | Usage |
| --- | --- | --- |
| Oboe (fourni avec JUCE) | Apache 2.0 | Audio Android (AAudio / OpenSL ES) |
| Android Gradle Plugin, Gradle | Apache 2.0 | Outils de build, non distribués dans l'APK |

## Conséquences

- Une cible de plus à faire compiler : la CI Android casse si `app/Source` utilise une API de bureau sans garde.
- La latence d'un téléphone (micro et haut-parleur) est bien plus élevée que celle d'une carte son : réglage manuel jusqu'à 300 ms en attendant la calibration (J7).
