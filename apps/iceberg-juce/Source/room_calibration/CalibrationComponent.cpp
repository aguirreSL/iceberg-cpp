#include "CalibrationComponent.h"

#include "../Theme.h"

#include "calibration/signals.h"
#include "calibration/spectrum.h"
#include "io/wav_loader.h"

#include <cmath>
#include <random>

namespace iceberg::juce_app::room_calibration {

namespace cal = iceberg::calibration;

namespace {

constexpr double kFs = 48000.0;           // calibration_LSRoom's rate
constexpr double kMicSeconds = 10.0;      // calibration_LSRoom's nSecondsMic
constexpr double kRefHz = 1000.0;
constexpr double kTfPauseSeconds = 1.5;   // getTF's pause between loudspeakers
constexpr double kLevelPauseSeconds = 0.5;   // getLevel's

enum ExcitationId { kPink = 1, kSweep, kLtass };

// The app's colours by role (Theme.h), read when used so a theme switch
// reaches the next result; applyColours() re-colours the fixed labels.
const iceberg::juce_app::Palette& pal() { return iceberg::juce_app::theme(); }

juce::String fromStd(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

void styleHelp(juce::Label& l) {
    l.setFont(juce::FontOptions{12.0f});
    l.setColour(juce::Label::textColourId, pal().text_dim);
    l.setJustificationType(juce::Justification::topLeft);
    l.setMinimumHorizontalScale(1.0f);
}

void styleCaption(juce::Label& l) {
    l.setFont(juce::FontOptions{12.0f});
    l.setColour(juce::Label::textColourId, pal().text);
}

void styleNumber(juce::Slider& s, double lo, double hi, double step, double value) {
    s.setSliderStyle(juce::Slider::IncDecButtons);
    s.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 64, 22);
    s.setRange(lo, hi, step);
    s.setValue(value, juce::dontSendNotification);
}

void setResult(juce::Label& l, const juce::String& text, juce::Colour colour) {
    l.setText(text, juce::dontSendNotification);
    l.setColour(juce::Label::textColourId, colour);
}

}  // namespace

CalibrationComponent::CalibrationComponent(std::unique_ptr<juce::XmlElement> main_device_state) {
    // Inputs and outputs, starting on the main window's output device;
    // the input defaults to the system's, at 48 kHz.
    device_manager_.initialise(32, 32, main_device_state.get(), true);
    auto setup = device_manager_.getAudioDeviceSetup();
    if (setup.inputDeviceName.isEmpty()) {
        if (auto* type = device_manager_.getCurrentDeviceTypeObject()) {
            const auto names = type->getDeviceNames(true);
            const int def = type->getDefaultDeviceIndex(true);
            if (def >= 0 && def < names.size()) setup.inputDeviceName = names[def];
        }
    }
    setup.useDefaultInputChannels = true;
    setup.sampleRate = kFs;
    device_manager_.setAudioDeviceSetup(setup, true);
    device_manager_.addChangeListener(this);
    play_record_ = std::make_unique<DevicePlayRecord>(device_manager_);

    intro_.setText(
        "Measures every loudspeaker at the listening position and writes the calibration file "
        "the renderer loads. Place the measurement microphone where the listener's head will "
        "be, keep the room quiet and run the steps in order. Hover over any control for "
        "instructions.",
        juce::dontSendNotification);
    styleHelp(intro_);
    intro_.setFont(juce::FontOptions{13.0f});
    addAndMakeVisible(intro_);

    // Not yet run on real hardware.
    experimental_.setText(
        "Experimental: this measurement matches Iceberg MATLAB on simulated devices but has "
        "not been validated with real hardware. A calibration.json measured with Iceberg "
        "MATLAB is the tested path.",
        juce::dontSendNotification);
    styleHelp(experimental_);
    addAndMakeVisible(experimental_);

    for (auto* l : {&device_caption_, &mic_caption_, &ls_caption_, &prev_caption_,
                    &cal_level_caption_, &tf_avg_caption_, &tf_level_caption_, &target_caption_,
                    &tol_caption_, &lavg_caption_, &maxit_caption_, &exc_caption_,
                    &ltass_len_caption_}) {
        styleCaption(*l);
        addAndMakeVisible(*l);
    }
    for (auto* l : {&device_label_, &prev_label_, &speech_label_}) {
        styleHelp(*l);
        l->setJustificationType(juce::Justification::centredLeft);
        l->setMinimumHorizontalScale(0.6f);
        addAndMakeVisible(*l);
    }
    for (auto* l : {&mic_help_, &tf_help_, &level_help_, &mic_result_, &tf_result_,
                    &level_result_}) {
        styleHelp(*l);
        addAndMakeVisible(*l);
    }
    styleHelp(setup_problem_);
    setup_problem_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(setup_problem_);
    for (auto* g : {&mic_group_, &tf_group_, &level_group_}) {
        addAndMakeVisible(*g);
    }

    // ---- Setup ----------------------------------------------------------
    const juce::String device_tip =
        "The sound card used for the calibration: its outputs drive the loudspeakers (output 1 "
        "= loudspeaker 1) and one of its inputs records the measurement microphone. The "
        "calibration runs at 48 kHz.";
    device_caption_.setTooltip(device_tip);
    device_label_.setTooltip(device_tip);
    audio_setup_btn_.setTooltip(
        "Choose the sound card, its sample rate (48000 Hz) and which inputs and outputs are "
        "active. Use the same output device and channel order as the experiments.");
    audio_setup_btn_.onClick = [this] { onAudioSetup(); };
    addAndMakeVisible(audio_setup_btn_);

    const juce::String mic_tip =
        "The input the measurement microphone is plugged into. Set the preamp gain once (for "
        "example +30 dB, as in Iceberg's setup) and keep it from step 1 to the end: the "
        "microphone factor holds only for that gain.";
    mic_caption_.setTooltip(mic_tip);
    mic_menu_.setTooltip(mic_tip);
    addAndMakeVisible(mic_menu_);

    const juce::String ls_tip =
        "How many loudspeakers to calibrate, on outputs 1 to N. Loudspeaker k of the calibration "
        "file is output k, so keep the order the experiments' layout uses (default: 0, 90, 180, "
        "270 deg on outputs 1 to 4).";
    ls_caption_.setTooltip(ls_tip);
    ls_menu_.setTooltip(ls_tip);
    ls_menu_.onChange = [this] {
        if (static_cast<int>(filters_.size()) != loudspeakerCount()) {
            filters_.clear();
            checks_.clear();
            check_menu_.clear(juce::dontSendNotification);
            check_plot_.setCheck(std::nullopt, 0);
            level_factor_.resize(0);
            tf_result_.setText("", juce::dontSendNotification);
            level_result_.setText("", juce::dontSendNotification);
        }
        updateButtons();
    };
    addAndMakeVisible(ls_menu_);

    const juce::String prev_tip =
        "Optional: a calibration file from an earlier session in this room. Its microphone "
        "factor is used when step 1 is skipped, and its level factors are where step 3 starts "
        "(if it has the same number of loudspeakers); without it, step 3 starts from 1.0.";
    prev_caption_.setTooltip(prev_tip);
    prev_label_.setTooltip(prev_tip);
    prev_browse_.setTooltip(prev_tip);
    prev_clear_.setTooltip("Forget the previous calibration.");
    prev_label_.setText("(none)", juce::dontSendNotification);
    prev_browse_.onClick = [this] { onBrowsePrevious(); };
    prev_clear_.onClick = [this] { onClearPrevious(); };
    addAndMakeVisible(prev_browse_);
    addAndMakeVisible(prev_clear_);

    // ---- Step 1: microphone --------------------------------------------
    mic_help_.setText("Turns the microphone's digital level into sound pressure. Put the "
                      "calibrator on the microphone, switch it on, then record.",
                      juce::dontSendNotification);
    const juce::String cal_tip =
        "The level the calibrator produces at 1 kHz, as printed on it: for example 94 dB (1 Pa) "
        "or 114 dB (10 Pa), or the value with an adapter. 94 dB is taken as exactly 1 Pa, as in "
        "Iceberg.";
    cal_level_caption_.setTooltip(cal_tip);
    cal_level_.setTooltip(cal_tip);
    styleNumber(cal_level_, 80.0, 140.0, 0.1, 94.0);
    addAndMakeVisible(cal_level_);
    mic_run_.setTooltip(
        "Records 10 s from the microphone input and computes the factor; nothing is played. "
        "The recording is checked: the tone should be at 1 kHz, below full scale, well above the "
        "noise and steady. Problems are reported as warnings: you decide whether to record "
        "again.");
    mic_result_.setTooltip("The microphone factor (pascal per digital unit), what the tone "
                           "measured, and any warning about the recording.");
    mic_run_.onClick = [this] { runMicrophone(); };
    addAndMakeVisible(mic_run_);

    // ---- Step 2: loudspeaker response ----------------------------------
    tf_help_.setText("Plays a sweep through each loudspeaker in turn and derives its "
                     "equalisation (1/3 octave, 50 Hz to 20 kHz).",
                     juce::dontSendNotification);
    const juce::String tf_avg_tip =
        "Sweeps averaged per loudspeaker. More averages lower the noise; each sweep takes about "
        "0.7 s. Iceberg uses 3.";
    tf_avg_caption_.setTooltip(tf_avg_tip);
    tf_avg_.setTooltip(tf_avg_tip);
    styleNumber(tf_avg_, 1.0, 10.0, 1.0, 3.0);
    addAndMakeVisible(tf_avg_);
    const juce::String tf_level_tip =
        "Sweep level in dBFS. Iceberg plays it at -22 dBFS, as Iceberg MATLAB does. Raise it "
        "only if the room is noisy; a sweep that would clip is never played.";
    tf_level_caption_.setTooltip(tf_level_tip);
    tf_level_.setTooltip(tf_level_tip);
    styleNumber(tf_level_, -60.0, 0.0, 1.0, -22.0);
    addAndMakeVisible(tf_level_);
    tf_run_.setTooltip(
        "Set the amplifiers to the level the experiments use, then measure. Every loudspeaker "
        "(outputs 1 to N) gets its sweeps, 1.5 s apart, then one more sweep through its new "
        "equalisation as a check.");
    tf_result_.setTooltip("Per loudspeaker: the range of its equalisation across the bands, "
                          "and how far its response spreads over 100 Hz to 16 kHz before "
                          "the equalisation and through it (+- dB). The plot below shows "
                          "the two responses.");
    tf_run_.onClick = [this] { runResponse(); };
    addAndMakeVisible(tf_run_);

    // ---- Step 3: level --------------------------------------------------
    level_help_.setText("Plays a test signal through each loudspeaker, measures its level at "
                        "the microphone and corrects it until it reads the target.",
                        juce::dontSendNotification);
    const juce::String target_tip =
        "The level every loudspeaker must reach at the microphone, in dB SPL. Iceberg uses 70 dB.";
    target_caption_.setTooltip(target_tip);
    target_.setTooltip(target_tip);
    styleNumber(target_, 40.0, 95.0, 0.5, 70.0);
    const juce::String tol_tip = "How close to the target counts as reached. Iceberg uses 0.5 dB.";
    tol_caption_.setTooltip(tol_tip);
    tol_.setTooltip(tol_tip);
    styleNumber(tol_, 0.1, 3.0, 0.1, 0.5);
    const juce::String lavg_tip =
        "Measurements averaged before each correction. Iceberg uses 3.";
    lavg_caption_.setTooltip(lavg_tip);
    lavg_.setTooltip(lavg_tip);
    styleNumber(lavg_, 1.0, 10.0, 1.0, 3.0);
    const juce::String maxit_tip =
        "Corrections allowed per loudspeaker before the run stops. A loudspeaker that does not "
        "get there usually means the target is out of reach or the microphone does not pick it "
        "up. Iceberg uses 10.";
    maxit_caption_.setTooltip(maxit_tip);
    maxit_.setTooltip(maxit_tip);
    styleNumber(maxit_, 1.0, 30.0, 1.0, 10.0);
    for (auto* s : {&target_, &tol_, &lavg_, &maxit_}) addAndMakeVisible(*s);

    exc_menu_.addItem("Pink noise", kPink);
    exc_menu_.addItem("Sweep", kSweep);
    exc_menu_.addItem("LTASS (speech-shaped)", kLtass);
    exc_menu_.setSelectedId(kPink, juce::dontSendNotification);
    const juce::String exc_tip =
        "The test signal. Pink noise is Iceberg's default. The sweep repeats exactly. LTASS is "
        "noise with the long-term spectrum of a speech recording you choose (Iceberg ships no "
        "speech).";
    exc_caption_.setTooltip(exc_tip);
    exc_menu_.setTooltip(exc_tip);
    exc_menu_.onChange = [this] { updateButtons(); };
    addAndMakeVisible(exc_menu_);
    speech_btn_.setTooltip("A WAV of speech whose long-term spectrum shapes the LTASS noise. "
                           "Any sample rate: the noise is resampled to 48 kHz.");
    speech_btn_.onClick = [this] { onBrowseSpeech(); };
    addAndMakeVisible(speech_btn_);
    speech_label_.setText("(no speech file)", juce::dontSendNotification);
    const juce::String ltass_tip =
        "How much of the LTASS signal is played, in seconds (it is generated 3 s long).";
    ltass_len_caption_.setTooltip(ltass_tip);
    ltass_len_.setTooltip(ltass_tip);
    styleNumber(ltass_len_, 0.5, 2.9, 0.1, 2.5);
    addAndMakeVisible(ltass_len_);

    level_run_.setTooltip(
        "Aligns every loudspeaker in turn, 0.5 s apart. Needs step 2's equalisation and a "
        "microphone factor (step 1 or a previous calibration). A signal that would exceed "
        "digital full scale is never played.");
    level_result_.setTooltip("Each loudspeaker's level factor, the level it reached and how "
                             "many corrections it took.");
    level_run_.onClick = [this] { runLevel(); };
    addAndMakeVisible(level_run_);

    // ---- Log and actions ------------------------------------------------
    log_.setMultiLine(true, true);
    log_.setReadOnly(true);
    log_.setCaretVisible(false);
    log_.setScrollbarsShown(true);
    log_.setFont(juce::FontOptions{12.0f});
    log_.setTooltip("Everything the steps did, in order.");
    addAndMakeVisible(log_);
    check_menu_.setTooltip("Which loudspeaker the plot shows.");
    check_menu_.onChange = [this] { showCheck(); };
    addAndMakeVisible(check_menu_);
    check_plot_.setTooltip(
        "Step 2's check: the loudspeaker's response in 1/3-octave bands before its "
        "equalisation (grey) and measured again through it (blue), each about its mean over "
        "100 Hz to 16 kHz. The +- figures are half the distance between the highest and the "
        "lowest band in that range: the smaller after, the flatter the equalised loudspeaker.");
    addAndMakeVisible(check_plot_);
    cancel_btn_.setTooltip("Stops after the sweep or test signal now playing.");
    cancel_btn_.onClick = [this] { onCancel(); };
    addAndMakeVisible(cancel_btn_);
    save_btn_.setTooltip(
        "Writes the calibration file (JSON): the microphone factor and each loudspeaker's "
        "equalisation and level factor. Use it as a slot's Calibration, with 'Use full scale' "
        "off.");
    save_btn_.onClick = [this] { onSave(); };
    addAndMakeVisible(save_btn_);

    applyColours();
    setSize(1040, 830);
    refreshDevice();
    updateButtons();
    logLine("Calibration device: " + device_label_.getText() + ".");
}

CalibrationComponent::~CalibrationComponent() {
    play_record_->cancel();
    if (worker_.joinable()) worker_.join();
    device_manager_.removeChangeListener(this);
    play_record_.reset();
}

void CalibrationComponent::paint(juce::Graphics& g) {
    g.fillAll(pal().panel);
}

void CalibrationComponent::lookAndFeelChanged() {
    applyColours();
}

// The colours set on this window's fixed labels, groups and log. The result
// labels keep the colour of their last result.
void CalibrationComponent::applyColours() {
    for (auto* l : {&device_caption_, &mic_caption_, &ls_caption_, &prev_caption_,
                    &cal_level_caption_, &tf_avg_caption_, &tf_level_caption_, &target_caption_,
                    &tol_caption_, &lavg_caption_, &maxit_caption_, &exc_caption_,
                    &ltass_len_caption_}) {
        l->setColour(juce::Label::textColourId, pal().text);
    }
    for (auto* l : {&device_label_, &prev_label_, &speech_label_, &mic_help_, &tf_help_,
                    &level_help_}) {
        l->setColour(juce::Label::textColourId, pal().text_dim);
    }
    experimental_.setColour(juce::Label::textColourId, pal().warn);
    setup_problem_.setColour(juce::Label::textColourId, pal().error);
    for (auto* g : {&mic_group_, &tf_group_, &level_group_}) {
        g->setColour(juce::GroupComponent::textColourId, pal().text);
        g->setColour(juce::GroupComponent::outlineColourId, pal().outline);
    }
    log_.setColour(juce::TextEditor::backgroundColourId, pal().field);
    log_.setColour(juce::TextEditor::outlineColourId, pal().outline);
    log_.setColour(juce::TextEditor::textColourId, pal().text_dim);
    log_.applyColourToAllText(pal().text_dim);
    repaint();
}

void CalibrationComponent::resized() {
    auto area = getLocalBounds().reduced(16);
    intro_.setBounds(area.removeFromTop(40));
    area.removeFromTop(2);
    experimental_.setBounds(area.removeFromTop(32));
    area.removeFromTop(6);

    // Setup: two rows of two halves.
    const int half = (area.getWidth() - 24) / 2;
    auto row = area.removeFromTop(26);
    {
        auto left = row.removeFromLeft(half);
        device_caption_.setBounds(left.removeFromLeft(140));
        audio_setup_btn_.setBounds(left.removeFromRight(120));
        left.removeFromRight(8);
        device_label_.setBounds(left);
        row.removeFromLeft(24);
        prev_caption_.setBounds(row.removeFromLeft(140));
        prev_browse_.setBounds(row.removeFromRight(70));
        row.removeFromRight(6);
        prev_clear_.setBounds(row.removeFromRight(60));
        row.removeFromRight(8);
        prev_label_.setBounds(row);
    }
    area.removeFromTop(6);
    row = area.removeFromTop(26);
    {
        auto left = row.removeFromLeft(half);
        mic_caption_.setBounds(left.removeFromLeft(140));
        mic_menu_.setBounds(left.removeFromLeft(200));
        row.removeFromLeft(24);
        ls_caption_.setBounds(row.removeFromLeft(140));
        ls_menu_.setBounds(row.removeFromLeft(200));
    }
    area.removeFromTop(4);
    setup_problem_.setBounds(area.removeFromTop(18));
    area.removeFromTop(8);

    // Bottom: actions, then the log above them.
    auto actions = area.removeFromBottom(32);
    save_btn_.setBounds(actions.removeFromRight(180));
    actions.removeFromRight(8);
    cancel_btn_.setBounds(actions.removeFromRight(100));
    area.removeFromBottom(8);
    // The log on the left, step 2's check plot with its loudspeaker menu on
    // the right.
    auto bottom = area.removeFromBottom(170);
    auto plot = bottom.removeFromRight(bottom.getWidth() * 52 / 100);
    bottom.removeFromRight(12);
    log_.setBounds(bottom);
    check_menu_.setBounds(plot.removeFromTop(24).removeFromLeft(170));
    plot.removeFromTop(4);
    check_plot_.setBounds(plot);
    area.removeFromBottom(12);

    // The three steps, side by side.
    const int col = (area.getWidth() - 2 * 12) / 3;
    auto layoutGroup = [](juce::GroupComponent& group, juce::Rectangle<int> bounds,
                          juce::Label& help) {
        group.setBounds(bounds);
        auto inner = bounds.reduced(12);
        inner.removeFromTop(14);   // the group's title
        help.setBounds(inner.removeFromTop(46));
        inner.removeFromTop(6);
        return inner;
    };
    auto param = [](juce::Rectangle<int>& inner, juce::Label& caption, juce::Component& control) {
        auto r = inner.removeFromTop(26);
        caption.setBounds(r.removeFromLeft(r.getWidth() - 120));
        control.setBounds(r);
        inner.removeFromTop(4);
    };

    auto c1 = layoutGroup(mic_group_, area.removeFromLeft(col), mic_help_);
    area.removeFromLeft(12);
    param(c1, cal_level_caption_, cal_level_);
    mic_run_.setBounds(c1.removeFromTop(28));
    c1.removeFromTop(8);
    mic_result_.setBounds(c1);

    auto c2 = layoutGroup(tf_group_, area.removeFromLeft(col), tf_help_);
    area.removeFromLeft(12);
    param(c2, tf_avg_caption_, tf_avg_);
    param(c2, tf_level_caption_, tf_level_);
    tf_run_.setBounds(c2.removeFromTop(28));
    c2.removeFromTop(8);
    tf_result_.setBounds(c2);

    auto c3 = layoutGroup(level_group_, area, level_help_);
    param(c3, target_caption_, target_);
    param(c3, tol_caption_, tol_);
    param(c3, lavg_caption_, lavg_);
    param(c3, maxit_caption_, maxit_);
    param(c3, exc_caption_, exc_menu_);
    {
        auto r = c3.removeFromTop(26);
        speech_btn_.setBounds(r.removeFromLeft(110));
        r.removeFromLeft(8);
        speech_label_.setBounds(r);
        c3.removeFromTop(4);
    }
    param(c3, ltass_len_caption_, ltass_len_);
    level_run_.setBounds(c3.removeFromTop(28));
    c3.removeFromTop(8);
    level_result_.setBounds(c3);
}

void CalibrationComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    refreshDevice();
}

void CalibrationComponent::refreshDevice() {
    auto* device = device_manager_.getCurrentAudioDevice();
    const int previous_mic = mic_menu_.getSelectedId();
    const int previous_ls = ls_menu_.getSelectedId();
    mic_menu_.clear(juce::dontSendNotification);
    ls_menu_.clear(juce::dontSendNotification);
    int active_outputs = 0;
    if (device == nullptr) {
        device_label_.setText("(no device open)", juce::dontSendNotification);
    } else {
        const auto setup = device_manager_.getAudioDeviceSetup();
        const auto ins = device->getActiveInputChannels();
        const auto outs = device->getActiveOutputChannels();
        active_outputs = outs.countNumberOfSetBits();
        device_label_.setText(
            setup.outputDeviceName + " / " +
                (setup.inputDeviceName.isEmpty() ? juce::String("no input") : setup.inputDeviceName) +
                ", " + juce::String(device->getCurrentSampleRate(), 0) + " Hz, " +
                juce::String(ins.countNumberOfSetBits()) + " in, " + juce::String(active_outputs) +
                " out",
            juce::dontSendNotification);
        const auto names = device->getInputChannelNames();
        for (int c = 0; c < names.size(); ++c) {
            if (!ins[c]) continue;
            // CoreAudio often names a channel by its number alone.
            const juce::String number = juce::String(c + 1);
            const juce::String name = names[c].trim();
            mic_menu_.addItem(name.isEmpty() || name == number ? "In " + number
                                                               : "In " + number + " (" + name + ")",
                              c + 1);
        }
    }
    for (int n = 4; n <= std::max(4, active_outputs); ++n) {
        ls_menu_.addItem(juce::String(n) + "  (outputs 1-" + juce::String(n) + ")", n);
    }
    if (mic_menu_.indexOfItemId(previous_mic) >= 0) {
        mic_menu_.setSelectedId(previous_mic, juce::dontSendNotification);
    } else if (mic_menu_.getNumItems() > 0) {
        mic_menu_.setSelectedItemIndex(0, juce::dontSendNotification);
    }
    ls_menu_.setSelectedId(ls_menu_.indexOfItemId(previous_ls) >= 0 ? previous_ls : 4,
                           juce::dontSendNotification);
    updateButtons();
}

juce::String CalibrationComponent::inputProblem() const {
    auto* device = device_manager_.getCurrentAudioDevice();
    if (device == nullptr) return "No audio device is open: choose one in Audio setup.";
    if (device->getActiveInputChannels().isZero()) {
        return "The device has no active input for the microphone: enable one in Audio setup.";
    }
    return {};
}

juce::String CalibrationComponent::outputProblem() const {
    auto* device = device_manager_.getCurrentAudioDevice();
    if (device == nullptr) return "No audio device is open: choose one in Audio setup.";
    const auto outs = device->getActiveOutputChannels();
    const int n = loudspeakerCount();
    for (int c = 0; c < n; ++c) {
        if (!outs[c]) {
            return "The device has " + juce::String(outs.countNumberOfSetBits()) +
                   " active outputs; " + juce::String(n) + " loudspeakers need outputs 1-" +
                   juce::String(n) + ". Enable them in Audio setup or choose another device.";
        }
    }
    return {};
}

void CalibrationComponent::onAudioSetup() {
    auto* selector = new juce::AudioDeviceSelectorComponent(
        device_manager_, /*minIn*/ 1, /*maxIn*/ 32, /*minOut*/ 1, /*maxOut*/ 32,
        /*showMidiIn*/ false, /*showMidiOut*/ false, /*showChannels*/ true,
        /*hideAdvanced*/ false);
    selector->setSize(560, 460);
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(selector);
    opts.dialogTitle = "Calibration audio setup";
    opts.componentToCentreAround = this;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

int CalibrationComponent::loudspeakerCount() const {
    return std::max(4, ls_menu_.getSelectedId());
}

int CalibrationComponent::micChannel() const {
    return std::max(0, mic_menu_.getSelectedId() - 1);
}

void CalibrationComponent::logLine(const juce::String& line) {
    log_.moveCaretToEnd();
    log_.insertTextAtCaret(line + "\n");
}

void CalibrationComponent::updateButtons() {
    const bool busy = running_.load();
    for (auto* c : std::initializer_list<juce::Component*>{
             &audio_setup_btn_, &mic_menu_, &ls_menu_, &prev_browse_, &prev_clear_, &cal_level_,
             &mic_run_, &tf_avg_, &tf_level_, &tf_run_, &target_, &tol_, &lavg_, &maxit_,
             &exc_menu_, &speech_btn_, &ltass_len_, &level_run_}) {
        c->setEnabled(!busy);
    }
    prev_clear_.setEnabled(!busy && previous_.has_value());
    cancel_btn_.setEnabled(busy);

    const bool ltass = exc_menu_.getSelectedId() == kLtass;
    speech_btn_.setVisible(ltass);
    speech_label_.setVisible(ltass);
    ltass_len_caption_.setVisible(ltass);
    ltass_len_.setVisible(ltass);

    const juce::String problem =
        inputProblem().isNotEmpty() ? inputProblem() : outputProblem();
    setup_problem_.setText(problem, juce::dontSendNotification);

    const int n_ls = loudspeakerCount();
    const bool have_filters = static_cast<int>(filters_.size()) == n_ls;
    const bool have_levels = level_factor_.size() == n_ls;
    save_btn_.setEnabled(!busy && i_factor_.has_value() && have_filters && have_levels);
}

iceberg::calibration::PlayRecord CalibrationComponent::deviceIo(double pause_seconds) {
    // MATLAB pauses between loudspeakers (getTF 1.5 s, getLevel 0.5 s) so
    // the room decays: pause whenever the loudspeaker carrying the page
    // changes.
    DevicePlayRecord* device = play_record_.get();
    auto last = std::make_shared<int>(-1);
    return [device, last, pause_seconds](const Eigen::MatrixXd& page,
                                         const std::vector<int>& channels, int rec,
                                         double fs) {
        int carrying = -1;
        for (Eigen::Index k = 0; k < page.cols() && k < static_cast<Eigen::Index>(channels.size());
             ++k) {
            if (!page.col(k).isZero(0.0)) {
                carrying = channels[static_cast<size_t>(k)];
                break;
            }
        }
        if (*last >= 0 && carrying >= 0 && carrying != *last && pause_seconds > 0.0) {
            juce::Thread::sleep(static_cast<int>(pause_seconds * 1000.0));
        }
        if (carrying >= 0) *last = carrying;
        return device->playRecord(page, channels, rec, fs);
    };
}

void CalibrationComponent::startWorker(const juce::String& what,
                                       std::function<std::function<void()>()> job) {
    if (running_.load()) return;
    if (worker_.joinable()) worker_.join();
    running_.store(true);
    play_record_->resetCancel();
    updateButtons();
    logLine(what + "...");

    juce::Component::SafePointer<CalibrationComponent> safe(this);
    worker_ = std::thread([safe, what, job] {
        std::function<void()> apply;
        juce::String error;
        try {
            apply = job();
        } catch (const Cancelled&) {
            error = "cancelled.";
        } catch (const std::exception& e) {
            error = fromStd(e.what());
        }
        juce::MessageManager::callAsync([safe, what, apply, error] {
            if (safe == nullptr) return;
            safe->running_.store(false);
            if (apply) {
                apply();
            } else {
                safe->logLine(what + " stopped: " + error);
            }
            safe->updateButtons();
        });
    });
}

void CalibrationComponent::runMicrophone() {
    if (const auto problem = inputProblem(); problem.isNotEmpty()) {
        setResult(mic_result_, problem, pal().error);
        return;
    }
    const int mic = micChannel();
    const double cal_db = cal_level_.getValue();
    const auto io = deviceIo(0.0);
    juce::Component::SafePointer<CalibrationComponent> safe(this);
    startWorker("Step 1, microphone: recording 10 s", [safe, io, mic, cal_db] {
        const auto n = static_cast<Eigen::Index>(kMicSeconds * kFs);
        const Eigen::VectorXd rec = io(Eigen::MatrixXd(n, 0), {}, mic, kFs);
        const auto check = cal::check_calibrator_recording(rec, kFs, kRefHz);
        const auto m = cal::mic_calibration_factor(rec, kFs, kRefHz);
        if (!(m.v1k > 0.0)) {
            throw std::runtime_error("the recording is silent: check the microphone input");
        }
        const double factor = cal::mic_factor_for_calibrator(m.v1k, cal_db);
        return std::function<void()>([safe, check, factor, cal_db] {
            if (safe == nullptr) return;
            safe->i_factor_ = factor;
            safe->i_factor_source_ = "step 1";
            juce::String text = "Factor " + juce::String(factor, 4) + " Pa per unit (calibrator " +
                                juce::String(cal_db, 1) + " dB SPL; tone " +
                                juce::String(check.rms_dbfs, 1) + " dBFS RMS at " +
                                juce::String(check.strongest_hz, 1) + " Hz).";
            for (const auto& w : check.warnings) text += "\nWarning: " + fromStd(w);
            setResult(safe->mic_result_, text, check.warnings.empty() ? pal().good : pal().warn);
            safe->logLine("Step 1: microphone factor " + juce::String(factor, 6) + ".");
            for (const auto& w : check.warnings) safe->logLine("  Warning: " + fromStd(w));
        });
    });
}

void CalibrationComponent::runResponse() {
    if (const auto problem = inputProblem().isNotEmpty() ? inputProblem() : outputProblem();
        problem.isNotEmpty()) {
        setResult(tf_result_, problem, pal().error);
        return;
    }
    cal::TfConfig config;
    config.n_loudspeakers = loudspeakerCount();
    config.input_channel = micChannel();
    config.fs = kFs;
    config.averages = static_cast<int>(tf_avg_.getValue());
    config.output_amplification_db = tf_level_.getValue();
    const auto io = deviceIo(kTfPauseSeconds);
    juce::Component::SafePointer<CalibrationComponent> safe(this);
    const cal::Progress progress = [safe](const std::string& line) {
        juce::MessageManager::callAsync([safe, line] {
            if (safe != nullptr) safe->logLine("  " + fromStd(line));
        });
    };
    startWorker("Step 2, loudspeaker response", [safe, config, io, progress] {
        const cal::TfResult tf = cal::get_tf(config, io, progress);
        std::vector<cal::ResponseCheck> checks;
        for (size_t k = 0; k < tf.rir.size(); ++k) {
            checks.push_back(cal::response_check(tf.rir[k], tf.rir_adjusted[k], config.fs));
        }
        return std::function<void()>([safe, filters = tf.filters, checks] {
            if (safe == nullptr) return;
            safe->filters_ = filters;
            safe->checks_ = checks;
            safe->level_factor_.resize(0);
            safe->level_result_.setText("", juce::dontSendNotification);
            juce::String text;
            for (size_t k = 0; k < filters.size(); ++k) {
                const auto& f = filters[k].freq;
                const double lo = 20.0 * std::log10(f.minCoeff());
                const double hi = 20.0 * std::log10(f.maxCoeff());
                text += "LS " + juce::String(static_cast<int>(k) + 1) + ": EQ " +
                        juce::String(lo, 1) + " to " + juce::String(hi, 1) + " dB, +-" +
                        juce::String(checks[k].before_spread_db, 1) + " -> +-" +
                        juce::String(checks[k].after_spread_db, 1) + " dB\n";
            }
            safe->check_menu_.clear(juce::dontSendNotification);
            for (size_t k = 0; k < checks.size(); ++k) {
                safe->check_menu_.addItem("Loudspeaker " + juce::String(static_cast<int>(k) + 1),
                                          static_cast<int>(k) + 1);
            }
            safe->check_menu_.setSelectedId(1, juce::dontSendNotification);
            safe->showCheck();
            setResult(safe->tf_result_, text.trimEnd(), pal().good);
            safe->logLine("Step 2: equalisation measured for " +
                          juce::String(static_cast<int>(filters.size())) + " loudspeakers.");
        });
    });
}

void CalibrationComponent::showCheck() {
    const int k = check_menu_.getSelectedId() - 1;
    if (k < 0 || k >= static_cast<int>(checks_.size())) {
        check_plot_.setCheck(std::nullopt, 0);
        return;
    }
    check_plot_.setCheck(checks_[static_cast<size_t>(k)], k + 1);
}

void CalibrationComponent::runLevel() {
    if (const auto problem = inputProblem().isNotEmpty() ? inputProblem() : outputProblem();
        problem.isNotEmpty()) {
        setResult(level_result_, problem, pal().error);
        return;
    }
    const int n_ls = loudspeakerCount();
    if (static_cast<int>(filters_.size()) != n_ls) {
        setResult(level_result_,
                  "Run step 2 first: the levels need each loudspeaker's equalisation.", pal().error);
        return;
    }
    if (!i_factor_) {
        setResult(level_result_,
                  "Needs a microphone factor: run step 1 or load a previous calibration.", pal().error);
        return;
    }
    const int excitation = exc_menu_.getSelectedId();
    if (excitation == kLtass && speech_path_.isEmpty()) {
        setResult(level_result_, "Choose a speech file for the LTASS signal.", pal().error);
        return;
    }

    cal::LevelConfig config;
    config.level = target_.getValue();
    config.tolerance_db = tol_.getValue();
    config.n_average = static_cast<int>(lavg_.getValue());
    config.max_iterations = static_cast<int>(maxit_.getValue());
    config.n_loudspeakers = n_ls;
    config.input_channel = micChannel();
    config.fs = kFs;

    Eigen::VectorXd initial = Eigen::VectorXd::Ones(n_ls);
    if (previous_ && static_cast<int>(previous_->loudspeakers.size()) == n_ls) {
        initial = previous_->new_level_factor;
        logLine("Step 3 starts from the previous calibration's level factors.");
    } else {
        logLine("Step 3 starts from level factor 1.0.");
    }
    const double i_factor = *i_factor_;
    const auto filters = filters_;
    const std::string speech = speech_path_.toStdString();
    const double ltass_seconds = ltass_len_.getValue();
    const auto io = deviceIo(kLevelPauseSeconds);
    juce::Component::SafePointer<CalibrationComponent> safe(this);
    const cal::Progress progress = [safe](const std::string& line) {
        juce::MessageManager::callAsync([safe, line] {
            if (safe != nullptr) safe->logLine("  " + fromStd(line));
        });
    };
    startWorker("Step 3, level", [safe, config, i_factor, filters, initial, excitation, speech,
                                  ltass_seconds, io, progress] {
        std::mt19937_64 rng(std::random_device{}());
        std::normal_distribution<double> randn(0.0, 1.0);
        const auto white = [&](Eigen::Index n) {
            Eigen::VectorXd w(n);
            for (Eigen::Index i = 0; i < n; ++i) w(i) = randn(rng);
            return w;
        };
        Eigen::VectorXd signal;
        if (excitation == kSweep) {
            signal = cal::level_excitation_sweep(config.fs);
        } else if (excitation == kLtass) {
            const auto wav = iceberg::io::load_wav(speech);
            const Eigen::VectorXd mono = wav.samples.cast<double>().rowwise().mean();
            const auto n_white = static_cast<Eigen::Index>(std::round(3.0 * wav.sample_rate));
            signal = cal::level_excitation_ltass(mono, wav.sample_rate, config.fs, white(n_white));
            signal = signal.head(std::min<Eigen::Index>(
                signal.size(), static_cast<Eigen::Index>(std::round(ltass_seconds * config.fs))));
        } else {
            signal = cal::level_excitation_pink(white(Eigen::Index{1} << 16), config.fs,
                                                config.lower_freq, config.upper_freq);
        }
        const cal::LevelResult out =
            cal::get_level(config, i_factor, filters, initial, signal, io, progress);
        return std::function<void()>([safe, out] {
            if (safe == nullptr) return;
            safe->level_factor_ = out.level_factor;
            juce::String text;
            for (Eigen::Index k = 0; k < out.level_factor.size(); ++k) {
                text += "LS " + juce::String(static_cast<int>(k) + 1) + ": factor " +
                        juce::String(out.level_factor(k), 4) + ", " + juce::String(out.spl(k), 1) +
                        " dB SPL (" + juce::String(out.corrections[static_cast<size_t>(k)]) +
                        " corrections)\n";
            }
            setResult(safe->level_result_, text.trimEnd(), pal().good);
            safe->logLine("Step 3: every loudspeaker aligned. Save the calibration.");
        });
    });
}

void CalibrationComponent::onCancel() {
    play_record_->cancel();
}

void CalibrationComponent::onBrowsePrevious() {
    chooser_ = std::make_unique<juce::FileChooser>("Previous calibration (.json)", juce::File{},
                                                   "*.json");
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            const auto file = fc.getResult();
            if (file == juce::File{}) return;
            try {
                previous_ = iceberg::io::load_calibration(file.getFullPathName().toStdString());
            } catch (const std::exception& e) {
                logLine("Previous calibration not loaded: " + fromStd(e.what()));
                return;
            }
            prev_label_.setText(file.getFileName() + "  (" +
                                    juce::String(static_cast<int>(previous_->loudspeakers.size())) +
                                    " loudspeakers)",
                                juce::dontSendNotification);
            if (i_factor_source_ != "step 1") {
                i_factor_ = previous_->i_factor;
                i_factor_source_ = "previous";
                setResult(mic_result_,
                          "Factor " + juce::String(previous_->i_factor, 4) +
                              " Pa per unit, from the previous calibration.",
                          pal().text_dim);
            }
            logLine("Previous calibration: " + file.getFullPathName());
            updateButtons();
        });
}

void CalibrationComponent::onClearPrevious() {
    previous_.reset();
    prev_label_.setText("(none)", juce::dontSendNotification);
    if (i_factor_source_ == "previous") {
        i_factor_.reset();
        i_factor_source_.clear();
        mic_result_.setText("", juce::dontSendNotification);
    }
    updateButtons();
}

void CalibrationComponent::onBrowseSpeech() {
    chooser_ = std::make_unique<juce::FileChooser>("Speech for LTASS (.wav)", juce::File{}, "*.wav");
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            const auto file = fc.getResult();
            if (file == juce::File{}) return;
            speech_path_ = file.getFullPathName();
            speech_label_.setText(file.getFileName(), juce::dontSendNotification);
        });
}

void CalibrationComponent::onSave() {
    chooser_ = std::make_unique<juce::FileChooser>(
        "Save calibration", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                .getChildFile("calibration.json"),
        "*.json");
    chooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc) {
            const auto file = fc.getResult();
            if (file == juce::File{} || !i_factor_) return;
            try {
                iceberg::io::save_calibration(
                    file.getFullPathName().toStdString(),
                    cal::to_calibration_data(*i_factor_, filters_, level_factor_));
                logLine("Saved " + file.getFullPathName());
            } catch (const std::exception& e) {
                logLine("Not saved: " + fromStd(e.what()));
            }
        });
}

CalibrationWindow::CalibrationWindow(std::unique_ptr<juce::XmlElement> main_device_state,
                                     std::function<void()> on_close)
    : juce::DocumentWindow("Room calibration", pal().panel, juce::DocumentWindow::closeButton),
      on_close_(std::move(on_close)) {
    setUsingNativeTitleBar(true);
    setContentOwned(new CalibrationComponent(std::move(main_device_state)), true);
    setResizable(false, false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void CalibrationWindow::closeButtonPressed() {
    if (on_close_) on_close_();
}

}  // namespace iceberg::juce_app::room_calibration
