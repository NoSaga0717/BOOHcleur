# Application Android de test

Une APK pour essayer le looper sur un téléphone, avec le micro intégré comme entrée. C'est un outil de test, pas une cible produit (voir l'[ADR 0002](../docs/adr/0002-application-android-de-test.md)) : même moteur et même enveloppe JUCE que le Standalone, interface tactile simplifiée, une boucle.

## Installer l'APK (hors Play Store)

1. Télécharger l'APK depuis le téléphone :
   - version de `main` : page **Releases** du dépôt, pré-version **android-test**, fichier `BOOHcleur-android.apk` ;
   - version d'une pull request : onglet **Actions**, exécution « Android » de la PR, artefact **BOOHcleur-Android** (un zip à ouvrir dans l'app Fichiers pour en sortir l'APK ; il faut être connecté à GitHub).
2. Ouvrir le fichier. Android demande d'autoriser l'installation d'applications inconnues pour le navigateur ou l'app Fichiers : accepter pour cette source.
3. Play Protect peut avertir d'une application inconnue : « Installer quand même ».
4. Au premier lancement, autoriser le micro.

Si l'installation d'une nouvelle version échoue (« application non installée », signature différente), désinstaller l'ancienne puis recommencer.

## Jouer

- **REC** démarre la prise (sur la mesure suivante en quantification « Mesure »), le même bouton la ferme puis enchaîne overdub et lecture. **STOP**, **UNDO / REDO**, **CLEAR** comme sur le Standalone. **Démarrer / Arrêter** pilote le transport et le métronome.
- **Au casque** de préférence : sinon le haut-parleur repasse dans le micro, et chaque overdub réenregistre la boucle. **Écoute (casque)** renvoie le micro dans la sortie ; coupée par défaut pour éviter le larsen.
- **Latence** : préréglée sur la valeur annoncée par Android, souvent sous-estimée. Si les overdubs arrivent en retard, l'augmenter jusqu'à ce qu'ils tombent en place (jusqu'à 300 ms).
- L'app suit l'orientation du téléphone (portrait ou paysage) et garde l'écran allumé.

## Compiler

Avec Android Studio : ouvrir le dossier `android/`. En ligne de commande (SDK Android et NDK installés, `ANDROID_HOME` défini) :

```bash
cd android
./gradlew assembleRelease
# APK : app/build/outputs/apk/release/app-release.apk
```

Au premier build, Gradle clone JUCE dans `android/build/` (même version que `app/`). CMake compile `android/native` à partir du `CMakeLists.txt` racine (option `BOOHCLEUR_BUILD_MOBILE`) ; seule l'architecture `arm64-v8a` est compilée par défaut (`-Pabis=arm64-v8a,armeabi-v7a` pour les vieux téléphones 32 bits).

Aperçu de l'interface sur bureau, sans téléphone :

```bash
cmake -S . -B build-mobile -DBOOHCLEUR_BUILD_MOBILE=ON -DBOOHCLEUR_BUILD_TESTS=OFF
cmake --build build-mobile -j
```

## Limites connues

- Entrée : micro du téléphone seulement pour l'instant. Une interface audio USB (class compliant) devrait apparaître comme périphérique Android, mais ce n'est pas testé et l'app ne propose pas encore de choisir le périphérique.
- Pas de pilotage MIDI sur cette version.
- Latence non calibrée (calibration automatique prévue en J7).
