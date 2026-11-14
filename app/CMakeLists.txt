# Enveloppes JUCE : Standalone en J2, VST3 et AU en J6 (ADR 0001).
include(FetchContent)

FetchContent_Declare(JUCE
	GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
	GIT_TAG        8.0.15
	GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(BOOHcleur
	PRODUCT_NAME "BOOHcleur"
	COMPANY_NAME "BOOHcleur"
	BUNDLE_ID "fr.boohcleur.looper"
	PLUGIN_MANUFACTURER_CODE Booh
	PLUGIN_CODE Bhc1
	FORMATS Standalone
	IS_SYNTH FALSE
	NEEDS_MIDI_INPUT TRUE
	NEEDS_MIDI_OUTPUT FALSE
	MICROPHONE_PERMISSION_ENABLED TRUE
	MICROPHONE_PERMISSION_TEXT "BOOHcleur enregistre la guitare branchée sur la carte son."
	ICON_BIG "${PROJECT_SOURCE_DIR}/docs/images/logo.png"
	COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_add_binary_data(BOOHcleurAssets SOURCES "${PROJECT_SOURCE_DIR}/docs/images/logo.png")

target_sources(BOOHcleur PRIVATE
	Source/PluginProcessor.cpp
	Source/PluginEditor.cpp
	Source/LoopRing.cpp
)

target_compile_definitions(BOOHcleur PUBLIC
	JUCE_WEB_BROWSER=0
	JUCE_USE_CURL=0
	JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(BOOHcleur
	PRIVATE
		boohcleur::core
		boohcleur-warnings
		BOOHcleurAssets
		juce::juce_audio_utils
	PUBLIC
		juce::juce_recommended_config_flags
		juce::juce_recommended_lto_flags
		juce::juce_recommended_warning_flags
)

# Outil de capture d'écran de l'interface (développement, sans carte son).
option(BOOHCLEUR_BUILD_SCREENSHOT "Compiler l'outil de capture de l'interface" OFF)

if (BOOHCLEUR_BUILD_SCREENSHOT)
	juce_add_console_app(boohcleur-screenshot PRODUCT_NAME "boohcleur-screenshot")

	target_sources(boohcleur-screenshot PRIVATE
		tools/Screenshot.cpp
		Source/PluginProcessor.cpp
		Source/PluginEditor.cpp
		Source/LoopRing.cpp
	)

	target_compile_definitions(boohcleur-screenshot PRIVATE
		JUCE_WEB_BROWSER=0
		JUCE_USE_CURL=0
	)

	target_link_libraries(boohcleur-screenshot PRIVATE
		boohcleur::core
		BOOHcleurAssets
		juce::juce_audio_processors
		juce::juce_audio_utils
		juce::juce_recommended_config_flags
	)
endif()
