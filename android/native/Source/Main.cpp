// Application mobile de test : le même moteur et la même enveloppe que le
// Standalone, une interface tactile et l'entrée micro du téléphone.

#include <juce_audio_utils/juce_audio_utils.h>

#include "MobileComponent.h"
#include "PluginProcessor.h"

#include "boohcleur/core/Version.h"

namespace
{
	class MainWindow final : public juce::DocumentWindow
	{
	public:
		explicit MainWindow(std::unique_ptr<juce::Component> content)
			: DocumentWindow("BOOHcleur", boohcleur::ui::colours::background, DocumentWindow::allButtons)
		{
			setUsingNativeTitleBar(true);
			setContentOwned(content.release(), true);

		#if JUCE_ANDROID || JUCE_IOS
			setFullScreen(true);
		#else
			// Prévisualisation sur bureau : format d'un téléphone en paysage.
			setResizable(true, false);
			centreWithSize(800, 380);
		#endif

			setVisible(true);
		}

		void closeButtonPressed() override
		{
			juce::JUCEApplication::getInstance()->systemRequestedQuit();
		}
	};
}

class BoohcleurMobileApp final : public juce::JUCEApplication
{
public:
	const juce::String getApplicationName() override { return "BOOHcleur"; }
	const juce::String getApplicationVersion() override
	{
		return juce::String(std::string(boohcleur::engineVersion()));
	}
	bool moreThanOneInstanceAllowed() override { return false; }

	void initialise(const juce::String&) override
	{
		processor_ = std::make_unique<BoohcleurProcessor>();
		window_ = std::make_unique<MainWindow>(std::make_unique<MobileComponent>(*processor_, deviceManager_));

		// Garder l'écran allumé pendant qu'on joue.
		juce::Desktop::getInstance().setScreenSaverEnabled(false);

		// Android demande l'accès au micro à l'exécution. Sans lui, on ouvre
		// quand même la sortie : les boucles restent vides mais l'app tourne.
		juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio,
			[this](bool granted) { openAudio(granted); });
	}

	void shutdown() override
	{
		deviceManager_.removeAudioCallback(&player_);
		player_.setProcessor(nullptr);
		deviceManager_.closeAudioDevice();
		window_ = nullptr;
		processor_ = nullptr;
	}

	void systemRequestedQuit() override { quit(); }

private:
	void openAudio(bool micGranted)
	{
		const auto error = deviceManager_.initialiseWithDefaultDevices(micGranted ? 1 : 0, 2);

		player_.setProcessor(processor_.get());
		deviceManager_.addAudioCallback(&player_);

		if (auto* content = dynamic_cast<MobileComponent*>(window_->getContentComponent()))
			content->audioOpened(micGranted, error);
	}

	juce::AudioDeviceManager deviceManager_;
	juce::AudioProcessorPlayer player_;
	std::unique_ptr<BoohcleurProcessor> processor_;
	std::unique_ptr<MainWindow> window_;
};

START_JUCE_APPLICATION(BoohcleurMobileApp)
