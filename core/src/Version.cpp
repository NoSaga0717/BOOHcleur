#include "boohcleur/core/Version.h"

namespace boohcleur
{
	std::string_view engineVersion() noexcept
	{
		return BOOHCLEUR_VERSION_STRING;
	}
}
