// APK de test de BOOHcleur : moteur et enveloppe JUCE du Standalone, interface
// tactile, entrée micro du téléphone (Oboe / AAudio). Voir android/README.md.

plugins {
	id("com.android.application")
}

val repoRoot: File = rootProject.projectDir.parentFile
val juceVersion: String = providers.gradleProperty("juceVersion").get()

// JUCE est cloné ici une fois pour toutes : Gradle a besoin de ses sources Java
// avant même que CMake ne tourne. -PjuceDir=... pour une copie locale.
val juceDir: File = providers.gradleProperty("juceDir").orNull?.let { file(it) }
	?: rootProject.layout.projectDirectory.dir("build/JUCE-$juceVersion").asFile

if (!juceDir.resolve("CMakeLists.txt").exists()) {
	logger.lifecycle("Clonage de JUCE $juceVersion dans $juceDir")
	providers.exec {
		commandLine("git", "clone", "--quiet", "--depth", "1", "--branch", juceVersion,
			"https://github.com/juce-framework/JUCE.git", juceDir.absolutePath)
	}.result.get().assertNormalExitValue()
}

val abiList: List<String> = (providers.gradleProperty("abis").orNull ?: "arm64-v8a")
	.split(",").map { it.trim() }.filter { it.isNotEmpty() }

android {
	namespace = "fr.boohcleur.looper"
	compileSdk = 35
	providers.gradleProperty("ndkVersion").orNull?.let { ndkVersion = it }

	defaultConfig {
		applicationId = "fr.boohcleur.looper"
		minSdk = 26	// AAudio (Android 8)
		targetSdk = 35
		versionCode = 1
		versionName = "0.0.1-test"

		ndk {
			abiFilters += abiList
		}

		externalNativeBuild {
			cmake {
				// Options reprises de l'exportateur Android de JUCE (Projucer).
				arguments += listOf(
					"-DBOOHCLEUR_BUILD_MOBILE=ON",
					"-DBOOHCLEUR_BUILD_TESTS=OFF",
					"-DBOOHCLEUR_JUCE_DIR=${juceDir.absolutePath}",
					"-DANDROID_STL=c++_static",
					"-DANDROID_CPP_FEATURES=exceptions rtti",
					"-DANDROID_ARM_MODE=arm",
					"-DANDROID_ARM_NEON=TRUE",
					// Liaison faible des API récentes du NDK (polices Android 29+),
					// JUCE vérifie leur présence à l'exécution.
					"-DANDROID_WEAK_API_DEFS=ON",
				)
				cFlags += "-fsigned-char"
				cppFlags += "-fsigned-char"
			}
		}
	}

	buildTypes {
		// Le C++ est optimisé même en debug : un moteur audio en -O0 décroche.
		debug {
			externalNativeBuild {
				cmake {
					arguments += "-DCMAKE_BUILD_TYPE=Release"
				}
			}
		}
		release {
			isMinifyEnabled = false
			// APK de test installée hors Play Store : signée avec la clé de debug.
			signingConfig = signingConfigs.getByName("debug")
		}
	}

	externalNativeBuild {
		cmake {
			path = repoRoot.resolve("CMakeLists.txt")
		}
	}

	sourceSets {
		getByName("main") {
			// Classes Java de JUCE (application, activité) : mêmes dossiers que
			// ceux que l'exportateur Android du Projucer ajoute au projet.
			java.srcDirs(
				juceDir.resolve("modules/juce_core/native/javacore/init"),
				juceDir.resolve("modules/juce_core/native/javacore/app"),
				juceDir.resolve("modules/juce_gui_basics/native/javaopt/app"),
			)
		}
	}

	compileOptions {
		sourceCompatibility = JavaVersion.VERSION_17
		targetCompatibility = JavaVersion.VERSION_17
	}

	packaging {
		jniLibs {
			useLegacyPackaging = false
		}
	}
}
