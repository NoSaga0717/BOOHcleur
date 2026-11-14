# Application Android de test

Une APK pour essayer le looper sur un téléphone, avec le micro intégré comme entrée. C'est un outil de test, pas une cible produit (voir l'[ADR 0002](../docs/adr/0002-application-android-de-test.md)) : même moteur et même enveloppe JUCE que le Standalone, interface tactile simplifiée, six boucles.

## Installer l'APK (hors Play Store)

1. Télécharger l'APK depuis le téléphone :
   - version de `main` : page **Releases** du dépôt, pré-version **android-test**, fichier `BOOHcleur-android.apk` ;
   - version d'une pull request : onglet **Actions**, exécution « Android » de la PR, artefact **BOOHcleur-Android** (un zip à ouvrir dans l'app Fichiers pour en sortir l'APK ; il faut être connecté à GitHub).
2. Ouvrir le fichier. Android demande d'autoriser l'installation d'applications inconnues pour le navigateur ou l'app Fichiers : accepter pour cette source.
3. Play Protect peut avertir d'une application inconnue : « Installer quand même ».
4. Au premier lancement, autoriser le micro.

Si l'installation d'une nouvelle version échoue (« application non installée », signature différente), désinstaller l'ancienne puis recommencer.

## Jouer

- Les **six boucles** sont en grille, chacune avec la forme d'onde de son audio dans l'anneau. Toucher un anneau agit sur cette boucle (REC, puis overdub, puis lecture) et la sélectionne ; toucher le nom la sélectionne seulement. Chaque boucle a son volume et son bouton **M** (muet).
- Les boutons du bas agissent sur la boucle sélectionnée : bouton principal, **STOP**, **UNDO / REDO**, **CLEAR** (deux appuis). **Démarrer / Arrêter** pilote le transport et le métronome.
- **Réglages** ouvre un panneau par-dessus les boucles : quantification (« Libre (immédiat) » pour une boucle de durée libre, sans s'aligner sur le métronome), nombre de boucles, métronome, écoute, latence, tempo, volume général.
- **Au casque** de préférence : sinon le haut-parleur repasse dans le micro, et chaque overdub réenregistre la boucle. **Écoute (casque)** renvoie le micro dans la sortie ; coupée par défaut pour éviter le larsen.
- **Latence** : préréglée sur la valeur annoncée par Android, souvent sous-estimée. Si les overdubs arrivent en retard, l'augmenter jusqu'à ce qu'ils tombent en place (jusqu'à 300 ms).
- L'app suit l'orientation du téléphone (portrait ou paysage) et garde l'écran allumé.

## Si REC ne fait rien

La ligne d'état en haut de l'écran dit ce qui bloque : micro refusé, moteur non préparé, mémoire insuffisante, ou audio arrêté (le système ne fait plus tourner le moteur). Sur Android, la réserve d'audio est de 128 Mo (environ 5 min 30 en stéréo à 48 kHz pour toutes les boucles), contre 512 Mo sur bureau.

Pour un diagnostic plus précis, l'app écrit son état toutes les 2 s dans le journal Android : `adb logcat | grep "BOOHcleur: etat"`. Le test dans l'émulateur (CI) touche le bouton principal et vérifie que la boucle 1 change d'état.

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
- Pas de pilotage MIDI ni de sessions sur cette version.
- Latence non calibrée (calibration automatique prévue en J7).
