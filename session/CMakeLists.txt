# Sessions (J5) : écriture et lecture des dossiers .boohcleur.
# Hors du moteur temps réel : C++20, bibliothèque standard et nlohmann/json
# (MIT, compatible GPLv3, ADR 0001 D12). Toujours sans JUCE, pour servir aussi
# à la tête (Pi 5).

include(FetchContent)

FetchContent_Declare(nlohmann_json
	URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
	URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(boohcleur-session
	src/Wav.cpp
	src/Session.cpp
)
add_library(boohcleur::session ALIAS boohcleur-session)

target_include_directories(boohcleur-session PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(boohcleur-session
	PUBLIC boohcleur::core
	PRIVATE nlohmann_json::nlohmann_json boohcleur-warnings
)
