#include "MainComponent.h"
#include "Theme.h"

#include <juce_gui_extra/juce_gui_extra.h>

class IcebergApplication : public juce::JUCEApplication {
public:
    IcebergApplication() = default;

    const juce::String getApplicationName() override {
        return JUCE_APPLICATION_NAME_STRING;
    }
    const juce::String getApplicationVersion() override {
        return JUCE_APPLICATION_VERSION_STRING;
    }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& /*commandLine*/) override {
        // The default look and feel, so dialogs and every window share the
        // theme. Set before the window, whose background it gives; the
        // saved appearance is applied by MainComponent.
        iceberg::juce_app::apply_theme(juce::Desktop::getInstance().isDarkModeActive(), lnf_);
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);
        main_window_ = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override {
        main_window_ = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override {
        if (!main_window_) { quit(); return; }
        if (auto* mc = dynamic_cast<MainComponent*>(
                main_window_->getContentComponent())) {
            mc->canQuit([](bool proceed) {
                if (proceed) juce::JUCEApplication::quit();
            });
            return;
        }
        quit();
    }

    void anotherInstanceStarted(const juce::String& /*commandLine*/) override {
        // No multi-instance state to merge yet.
    }

    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(juce::String name)
            : juce::DocumentWindow(
                  std::move(name),
                  juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                      juce::ResizableWindow::backgroundColourId),
                  DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    juce::LookAndFeel_V4 lnf_;
    std::unique_ptr<MainWindow> main_window_;
};

START_JUCE_APPLICATION(IcebergApplication)
