#!/bin/sh
# Test de fumée : installe l'APK dans l'émulateur, la lance et vérifie qu'elle
# tourne encore après quelques secondes. Le journal (logcat) est gardé.
set -u
APK="$1"
PKG=fr.boohcleur.looper

adb logcat -c
adb install -r -g "$APK"
adb shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1
sleep 15
adb exec-out screencap -p > smoke-screen.png
adb logcat -d > logcat.txt

echo "----- journal de l'application et plantages -----"
grep -E "AndroidRuntime|DEBUG|libc|JUCE|juce|$PKG|FATAL|Abort|signal" logcat.txt | tail -n 200

if adb shell pidof "$PKG" >/dev/null; then
	echo "OK : l'application tourne toujours après 15 s"
else
	echo "ÉCHEC : l'application s'est arrêtée"
	exit 1
fi
