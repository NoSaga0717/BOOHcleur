#include <catch2/catch_test_macros.hpp>

#include "boohcleur/core/Version.h"

TEST_CASE("La version du moteur est celle déclarée dans CMake")
{
	REQUIRE(boohcleur::engineVersion() == "0.0.1");
}
