#include <catch2/catch_test_macros.hpp>

#include "boohcleur/core/Version.h"

// Noms de tests en ASCII : ctest les passe en ligne de commande, et Windows
// ne les transmet pas correctement en UTF-8.
TEST_CASE("La version du moteur est celle de CMake")
{
	REQUIRE(boohcleur::engineVersion() == "0.0.1");
}
