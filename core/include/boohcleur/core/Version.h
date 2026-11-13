#pragma once

#include <string_view>

namespace boohcleur
{
	// Version du moteur, lue depuis CMake (project VERSION).
	std::string_view engineVersion() noexcept;
}
