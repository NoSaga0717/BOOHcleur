#!/bin/sh
# Test de fumée : installe l'APK dans l'émulateur, la lance, vérifie qu'elle
# tourne encore après quelques secondes, puis touche le bouton principal
# (REC) et regarde dans le journal si la boucle 1 s'arme ou enregistre.
# Le journal (logcat) et deux captures d'écran sont gardés.
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

if ! adb shell pidof "$PKG" >/dev/null; then
	echo "ÉCHEC : l'application s'est arrêtée"
	exit 1
fi
echo "OK : l'application tourne toujours après 15 s"

# L'application écrit « BOOHcleur: etat ... principal=x,y » toutes les 2 s.
LAST=$(grep "BOOHcleur: etat" logcat.txt | tail -n 1)
echo "État avant REC : $LAST"
XY=$(echo "$LAST" | sed -n 's/.*principal=\([0-9]*\),\([0-9]*\).*/\1 \2/p')

if [ -z "$XY" ]; then
	echo "AVERTISSEMENT : pas de ligne d'état dans le journal, REC non testé"
	exit 0
fi

adb shell input tap $XY
sleep 6
adb exec-out screencap -p > smoke-rec.png
adb logcat -d > logcat-rec.txt
AFTER=$(grep "BOOHcleur: etat" logcat-rec.txt | tail -n 1)
echo "État après REC : $AFTER"

if ! adb shell pidof "$PKG" >/dev/null; then
	echo "ÉCHEC : l'application s'est arrêtée après REC"
	exit 1
fi

# Résultat indicatif : l'émulateur n'a pas toujours de micro, on n'échoue pas.
case "$AFTER" in
	*"blocs=0 "*) echo "AVERTISSEMENT : le moteur audio ne tourne pas (blocs=0)" ;;
esac
case "$AFTER" in
	*"b1=REC"*|*"b1=ARM"*|*"b1=PLAY"*|*"b1=OVERDUB"*) echo "OK : REC a changé l'état de la boucle 1" ;;
	*) echo "AVERTISSEMENT : REC n'a pas changé l'état de la boucle 1" ;;
esac
exit 0
