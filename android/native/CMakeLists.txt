# Application mobile de test (Android), construite par Gradle (dossier android/).
#
# Le module JUCE est fourni par Gradle, qui clone la même version que app/
# (variable BOOHCLEUR_JUCE_DIR). Sur bureau, la même cible compile une fenêtre
# de prévisualisation de l'interface tactile, utile pour développer sans téléphone.

if (NOT TARGET juce::juce_core)
	if (BOOHCLEUR_JUCE_DIR)
		add_subdirectory("${BOOHCLEUR_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
	else()
		include(FetchContent)
		FetchContent_Declare(JUCE
			GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
			GIT_TAG        8.0.15
			GIT_SHALLOW    TRUE
		)
		FetchContent_MakeAvailable(JUCE)
	endif()
endif()

# Java.java (JUCE) charge « libjuce_jni.so » : le nom de la cible est imposé.
juce_add_gui_app(juce_jni
	PRODUCT_NAME "BOOHcleur"
	COMPANY_NAME "BOOHcleur"
	BUNDLE_ID "fr.boohcleur.looper"
	VERSION "${PROJECT_VERSION}"
	MICROPHONE_PERMISSION_ENABLED TRUE
)

if (NOT TARGET BOOHcleurAssets)
	juce_add_binary_data(BOOHcleurAssets SOURCES "${PROJECT_SOURCE_DIR}/docs/images/logo.png")
endif()

# On réutilise l'enveloppe et les widgets du Standalone (app/Source) sans la
# modifier : seule la fenêtre change. Le glob suit les fichiers ajoutés à app/.
file(GLOB BOOHCLEUR_APP_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/app/Source/*.cpp")

target_sources(juce_jni PRIVATE
	Source/Main.cpp
	Source/MobileComponent.cpp
	${BOOHCLEUR_APP_SOURCES}
)

target_include_directories(juce_jni PRIVATE "${PROJECT_SOURCE_DIR}/app/Source")

target_compile_definitions(juce_jni PRIVATE
	JUCE_WEB_BROWSER=0
	JUCE_USE_CURL=0
)

target_link_libraries(juce_jni
	PRIVATE
		boohcleur::core
		BOOHcleurAssets
		juce::juce_audio_utils
		juce::juce_recommended_config_flags
)
