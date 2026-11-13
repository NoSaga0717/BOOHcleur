# Consignes pour les agents IA (Claude, Codex, autres)

## Langue
Écrire en français : documentation, commentaires, messages de commit.

## Références
- Cahier des charges : `docs/cahier-des-charges.md`. Toute modification de périmètre se signale avant d'être implémentée.
- Décisions : `docs/adr/`. Ne pas contredire un ADR accepté sans en écrire un nouveau qui le remplace.
- Ne pas inventer de choix techniques non validés : les arbitrages ouverts sont listés dans l'ADR 0001.

## Règles d'architecture
- `core/` ne dépend que du C++20 et de la bibliothèque standard. Jamais de JUCE ni d'API système dans `core/`.
- Thread audio : aucune allocation, aucun verrou bloquant, aucun accès disque, aucune exception. Tampons pré-alloués, files sans verrou.
- Ne jamais confondre Loop Quantize (frontières de boucle) et Smart Quantize (attaques internes d'une prise).
- Les fichiers audio sources sont immuables. Toute correction est un paramètre ou un marqueur, jamais un remplacement.

## Licences
Le projet est en GPLv3. Toute bibliothèque ajoutée doit être compatible GPLv3 et listée avec sa licence dans l'ADR qui l'introduit.

## Travail
- Livrer par jalons compilables et testés. Chaque jalon donne : code, commandes de build, tests, décisions, problèmes connus.
- Passer par une branche et une pull request, jamais de push direct sur `main`.
- Pas d'affirmation non mesurée (« zéro latence », « sans artefact »).
- Noms de tests Catch2 en ASCII (sans accents) : sinon ctest ne les retrouve pas sous Windows.
