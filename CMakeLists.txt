cmake_minimum_required(VERSION 3.22)

project(BOOHcleur VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BOOHCLEUR_BUILD_TESTS "Compiler les tests" ON)

if (MSVC)
	add_compile_options(/W4 /permissive-)
else()
	add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Moteur commun : C++20 et bibliothèque standard uniquement (ADR 0001).
# Aucune dépendance à JUCE ni à l'OS dans ce module.
add_subdirectory(core)

if (BOOHCLEUR_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
