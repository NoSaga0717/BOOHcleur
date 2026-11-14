include(FetchContent)

FetchContent_Declare(Catch2
	GIT_REPOSITORY https://github.com/catchorg/Catch2.git
	GIT_TAG        v3.7.1
	GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(boohcleur-tests
	VersionTests.cpp
	MusicalTimeTests.cpp
	TempoMapTests.cpp
	GridTests.cpp
	TransportClockTests.cpp
	InputRingBufferTests.cpp
	EngineTests.cpp
	MidiControlTests.cpp
)
target_link_libraries(boohcleur-tests PRIVATE boohcleur::core Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(boohcleur-tests)

target_link_libraries(boohcleur-tests PRIVATE boohcleur-warnings)
