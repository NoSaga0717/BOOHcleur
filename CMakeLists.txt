cmake_minimum_required(VERSION 3.22)

project(BOOHcleur VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BOOHCLEUR_BUILD_TESTS "Compiler les tests" ON)
option(BOOHCLEUR_BUILD_APP "Compiler l'application JUCE (Standalone)" OFF)
option(BOOHCLEUR_BUILD_MOBILE "Compiler l'application mobile de test (Android via Gradle, aperçu sur bureau)" OFF)

# Avertissements stricts pour notre code seulement (pas pour les sources de JUCE).
add_library(boohcleur-warnings INTERFACE)

if (MSVC)
	target_compile_options(boohcleur-warnings INTERFACE /W4 /permissive-)
else()
	target_compile_options(boohcleur-warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Sources en UTF-8 (commentaires et textes en français).
if (MSVC)
	add_compile_options(/utf-8)
endif()

# Moteur commun : C++20 et bibliothèque standard uniquement (ADR 0001).
# Aucune dépendance à JUCE ni à l'OS dans ce module.
add_subdirectory(core)

if (BOOHCLEUR_BUILD_APP)
	add_subdirectory(app)
endif()

# Construite par Gradle pour Android (voir android/README.md).
if (BOOHCLEUR_BUILD_MOBILE)
	add_subdirectory(android/native)
endif()

if (BOOHCLEUR_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
