#pragma once

#include "DevicePlayRecord.h"
#include "ResponsePlot.h"

#include "calibration/measure.h"
#include "io/calibration_loader.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace iceberg::juce_app::room_calibration {

/// Room calibration: Iceberg v1.0.0's
/// `calibration_LSRoom` as a window. Three steps in its order -- the
/// microphone factor from a calibrator, each loudspeaker's response and
/// equalisation (getTF), each loudspeaker's level (getLevel) -- then the
/// calibration file the renderer loads. Every control carries a tooltip
/// that instructs.
///
/// Owns its own `AudioDeviceManager` (inputs and outputs, 48 kHz),
/// started on the main window's output device, so the main player keeps
/// its output-only setup. Measurements run on one worker thread; the UI
/// only reads results on the message thread.
class CalibrationComponent : public juce::Component, private juce::ChangeListener {
public:
    /// `main_device_state`: the main window's device setup, so the
    /// calibration opens the same output device (may be null).
    explicit CalibrationComponent(std::unique_ptr<juce::XmlElement> main_device_state);
    ~CalibrationComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    void applyColours();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    void refreshDevice();
    void onAudioSetup();
    void onBrowsePrevious();
    void onClearPrevious();
    void onBrowseSpeech();
    void runMicrophone();
    void runResponse();
    void showCheck();   // the plot shows the loudspeaker check_menu_ picks
    void runLevel();
    void onCancel();
    void onSave();

    /// Runs `job` on the worker; `job` returns the lines to log on success
    /// and the result to apply (on the message thread).
    void startWorker(const juce::String& what, std::function<std::function<void()>()> job);
    iceberg::calibration::PlayRecord deviceIo(double pause_seconds);
    void logLine(const juce::String& line);
    void updateButtons();
    int loudspeakerCount() const;
    int micChannel() const;
    /// Why the device cannot run a step now, or empty: no device, no
    /// active microphone input, outputs 1..N not all active.
    juce::String inputProblem() const;
    juce::String outputProblem() const;

    juce::AudioDeviceManager device_manager_;
    std::unique_ptr<DevicePlayRecord> play_record_;
    juce::TooltipWindow tooltip_window_ {this, 400};
    std::unique_ptr<juce::FileChooser> chooser_;
    std::thread worker_;
    std::atomic<bool> running_ {false};

    // Results, message thread only.
    std::optional<double> i_factor_;
    juce::String i_factor_source_;
    std::vector<iceberg::calibration::BandSpectrum> filters_;
    std::vector<iceberg::calibration::ResponseCheck> checks_;   // step 2, per loudspeaker
    Eigen::VectorXd level_factor_;
    std::optional<iceberg::io::CalibrationData> previous_;
    juce::String speech_path_;

    juce::Label intro_;
    juce::Label experimental_;

    juce::Label      device_caption_ {"dev_cap", "Audio device"};
    juce::Label      device_label_;
    juce::TextButton audio_setup_btn_ {"Audio setup..."};
    juce::Label      mic_caption_ {"mic_cap", "Microphone input"};
    juce::ComboBox   mic_menu_;
    juce::Label      ls_caption_ {"ls_cap", "Loudspeakers"};
    juce::ComboBox   ls_menu_;
    juce::Label      prev_caption_ {"prev_cap", "Previous calibration"};
    juce::Label      prev_label_;
    juce::TextButton prev_browse_ {"Browse"};
    juce::TextButton prev_clear_ {"Clear"};
    juce::Label      setup_problem_;

    juce::GroupComponent mic_group_ {"mic_group", "1  Microphone"};
    juce::Label      mic_help_;
    juce::Label      cal_level_caption_ {"cal_cap", "Calibrator level (dB SPL)"};
    juce::Slider     cal_level_;
    juce::TextButton mic_run_ {"Record 10 s"};
    juce::Label      mic_result_;

    juce::GroupComponent tf_group_ {"tf_group", "2  Loudspeaker response"};
    juce::Label      tf_help_;
    juce::Label      tf_avg_caption_ {"tf_avg_cap", "Averages"};
    juce::Slider     tf_avg_;
    juce::Label      tf_level_caption_ {"tf_lvl_cap", "Sweep level (dBFS)"};
    juce::Slider     tf_level_;
    juce::TextButton tf_run_ {"Measure"};
    juce::Label      tf_result_;

    juce::GroupComponent level_group_ {"level_group", "3  Level"};
    juce::Label      level_help_;
    juce::Label      target_caption_ {"target_cap", "Target (dB SPL)"};
    juce::Slider     target_;
    juce::Label      tol_caption_ {"tol_cap", "Tolerance (dB)"};
    juce::Slider     tol_;
    juce::Label      lavg_caption_ {"lavg_cap", "Averages"};
    juce::Slider     lavg_;
    juce::Label      maxit_caption_ {"maxit_cap", "Max corrections"};
    juce::Slider     maxit_;
    juce::Label      exc_caption_ {"exc_cap", "Test signal"};
    juce::ComboBox   exc_menu_;
    juce::TextButton speech_btn_ {"Speech file..."};
    juce::Label      speech_label_;
    juce::Label      ltass_len_caption_ {"ltass_cap", "LTASS length (s)"};
    juce::Slider     ltass_len_;
    juce::TextButton level_run_ {"Align levels"};
    juce::Label      level_result_;

    juce::TextEditor log_;
    juce::ComboBox   check_menu_;
    ResponsePlot     check_plot_;
    juce::TextButton cancel_btn_ {"Cancel"};
    juce::TextButton save_btn_ {"Save calibration..."};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CalibrationComponent)
};

/// The window around `CalibrationComponent`. `on_close` runs when the
/// close button is pressed; the owner deletes the window (asynchronously).
class CalibrationWindow : public juce::DocumentWindow {
public:
    CalibrationWindow(std::unique_ptr<juce::XmlElement> main_device_state,
                      std::function<void()> on_close);
    void closeButtonPressed() override;

private:
    std::function<void()> on_close_;
};

}  // namespace iceberg::juce_app::room_calibration
