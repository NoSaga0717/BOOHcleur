<p align="center"><img src="docs/images/logo.png" alt="BOOHcleur" width="420"></p>

# BOOHcleur

Looper guitare multipiste avec **Smart Quantize** : il corrige le placement des attaques à l'intérieur d'une prise audio, sans jamais modifier l'enregistrement original. Il est prévu en application autonome (Standalone) et en plugins VST3 et AU, entièrement pilotable au MIDI, et à porter ensuite, éventuellement en version allégée, sur la [Tête NAM](https://github.com/NoSaga0717/tete-nam) (moteur [stompbox](https://github.com/NoSaga0717/stompbox) sur Raspberry Pi 5).

## Où on en est

J1 fait : horloge musicale, conversions temps musical ↔ échantillons, grille des frontières et règle de déclenchement (`core/`). Pas encore de traitement audio. L'architecture est dans [`docs/adr/0001-architecture.md`](docs/adr/0001-architecture.md). Prochain jalon : J2, une piste jouable dans un Standalone minimal.

| Document | Contenu |
| --- | --- |
| [`docs/cahier-des-charges.md`](docs/cahier-des-charges.md) | Cahier des charges de référence (V1 à V4) |
| [`docs/adr/`](docs/adr/README.md) | Décisions d'architecture (ADR), une par fichier |
| [`AGENTS.md`](AGENTS.md) | Règles pour les IA qui travaillent sur le dépôt |

## Compiler et tester

Il faut CMake 3.22 ou plus récent et un compilateur C++20 (GCC 11+, Clang 14+, MSVC 2022).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Les tests utilisent Catch2, téléchargé automatiquement par CMake au premier configure.

## Organisation

- `core/` : `boohcleur-core`, le moteur commun. C++20 et bibliothèque standard uniquement, sans JUCE ni dépendance à l'OS.
- `tests/` : tests automatisés.
- Les autres modules (analyse, rendu, contrôle, sessions, enveloppes JUCE, adaptateur stompbox) seront créés à leur jalon, voir l'ADR 0001.

## Licence

GPLv3, voir [`LICENSE`](LICENSE).
