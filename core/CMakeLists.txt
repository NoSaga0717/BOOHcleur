add_library(boohcleur-core
	src/Version.cpp
	src/MusicalTime.cpp
	src/TempoMap.cpp
	src/Grid.cpp
	src/TransportClock.cpp
	src/InputRingBuffer.cpp
	src/SamplePool.cpp
	src/LoopTrack.cpp
	src/MidiControl.cpp
	src/Engine.cpp
)
add_library(boohcleur::core ALIAS boohcleur-core)

target_include_directories(boohcleur-core PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_definitions(boohcleur-core PRIVATE
	BOOHCLEUR_VERSION_STRING="${PROJECT_VERSION}"
)

target_link_libraries(boohcleur-core PRIVATE boohcleur-warnings)
