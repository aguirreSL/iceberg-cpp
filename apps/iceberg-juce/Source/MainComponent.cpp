#include "MainComponent.h"

#include "Theme.h"
#include "audio/SignalFile.h"

#include "experiment/experiment_loader.h"
#include "experiment/slot_config.h"
#include "io/wav_loader.h"
#include "io/wav_writer.h"

namespace {
constexpr double kBytesPerGB = 1024.0 * 1024.0 * 1024.0;

constexpr const char* kPropRecentFiles  = "recent_files";
constexpr const char* kPropDeviceState  = "audio_device_state";
constexpr const char* kPropMemBudgetGB  = "memory_budget_gb";
constexpr const char* kPropAppearance  = "appearance";   // system, light or dark
}  // namespace

MainComponent::MainComponent() {
    setSize(1100, 800);

    // ApplicationProperties: ~/Library/Application Support/Iceberg/
    // Iceberg.settings on macOS, equivalent on other platforms.
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "Iceberg";
    opts.filenameSuffix      = "settings";
    opts.folderName          = "Iceberg";
    opts.osxLibrarySubFolder = "Application Support";
    opts.commonToAllUsers    = false;
    props_.setStorageParameters(opts);

    auto* user = props_.getUserSettings();
    recent_files_.setMaxNumberOfItems(5);
    recent_files_.restoreFromString(user->getValue(kPropRecentFiles));

    // Restore audio device state BEFORE AudioPlayer's default
    // initialise has a chance to run -- but member init order is
    // fixed (player_ ctor runs before this body). Patch up here:
    // re-initialise from saved XML if available. JUCE handles a
    // re-init cleanly.
    if (const auto xml_str = user->getValue(kPropDeviceState);
        xml_str.isNotEmpty()) {
        if (auto xml = juce::XmlDocument::parse(xml_str)) {
            player_.getDeviceManager().initialise(0, 32, xml.get(), true);
        }
    }

    // Restore memory budget.
    const double budget =
        user->getDoubleValue(kPropMemBudgetGB, 3.0);
    orchestrator_.setMemoryBudgetGB(budget);

    // The theme: the operating system's unless one was chosen in View.
    const juce::String appearance = user->getValue(kPropAppearance, "system");
    appearance_ = appearance == "light" ? Appearance::Light
                : appearance == "dark"  ? Appearance::Dark
                                        : Appearance::System;
    juce::Desktop::getInstance().addDarkModeSettingListener(this);

    // The menus: in the macOS menu bar, and on Windows and Linux in a bar at
    // the top of the window (setMacMainMenu exists only on macOS).
    menu_model_ = std::make_unique<MainMenu>(*this);
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(menu_model_.get());
   #else
    menu_bar_ = std::make_unique<juce::MenuBarComponent>(menu_model_.get());
    addAndMakeVisible(*menu_bar_);
   #endif

    addAndMakeVisible(list_panel_);
    addAndMakeVisible(props_panel_);
    addAndMakeVisible(status_bar_);
    addAndMakeVisible(render_all_btn_);
    addAndMakeVisible(memory_label_);
    addAndMakeVisible(play_seq_btn_);
    addAndMakeVisible(stop_btn_);
    addAndMakeVisible(next_btn_);
    addAndMakeVisible(wait_toggle_);
    addAndMakeVisible(wait_caption_);
    addAndMakeVisible(wait_slider_);
    addAndMakeVisible(prep_btn_);
    addAndMakeVisible(live_btn_);
    live_btn_.setClickingTogglesState(true);
    live_btn_.setTooltip(
        "Live mode: Play live prepares the slot and plays it through the live engine; "
        "dragging the source on the pad (or the angle slider) moves it while it plays. "
        "Held still, it sounds as the rendered slot; a moving source has no MATLAB "
        "reference. Needs one output per loudspeaker, or Prep mode for a stereo fold-down.");

    // Prep-mode button is a click-toggle so the visual state is
    // unambiguous even at a glance. ON state uses an accent colour
    // so the user sees immediately they're in stereo preview.
    prep_btn_.setClickingTogglesState(true);

    memory_label_.setFont(juce::FontOptions{12.0f});
    memory_label_.setJustificationType(juce::Justification::centredRight);

    wait_caption_.setFont(juce::FontOptions{12.0f});
    applyColours();
    wait_slider_.setSliderStyle(juce::Slider::LinearHorizontal);
    wait_slider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    wait_slider_.setRange(0.0, 10.0, 0.1);
    wait_slider_.setValue(0.0, juce::dontSendNotification);
    wait_slider_.setTextValueSuffix(" s");

    // Default startup: 1 empty slot so the user sees the editor wired up.
    experiment_.name = "Untitled Experiment";
    experiment_.slots.push_back(makeDefaultSlot(1));
    current_slot_index_ = 0;

    orchestrator_.setExperiment(&experiment_);
    player_.setExperiment(&experiment_);

    list_panel_.setExperiment(&experiment_);
    list_panel_.setOrchestrator(&orchestrator_);
    list_panel_.onSelectionChanged = [this](int idx) { onSlotSelected(idx); };
    list_panel_.onAddSlot          = [this]         { onAddSlot(); };
    list_panel_.onRemoveSlot       = [this](int i)  { onRemoveSlot(i); };
    list_panel_.onDuplicateSlot    = [this](int i)  { onDuplicateSlot(i); };
    list_panel_.onReorder          = [this](int f, int t) { onReorder(f, t); };
    list_panel_.onRenameSlot       = [this](int i)  { onRenameSlot(i); };

    props_panel_.onSlotEdited      = [this] { onSlotEdited(); };
    props_panel_.onBrowseRequested = [this](auto field) { onBrowseField(field); };
    props_panel_.onRenderThisSlot  = [this] { onRenderThisSlot(); };
    props_panel_.onClearBuffer     = [this] { onClearBuffer(); };
    props_panel_.onPlayThisSlot    = [this] { onPlayThisSlot(); };
    props_panel_.onRenderTrajectory = [this] { onRenderTrajectory(); };
    props_panel_.onPlayTrajectory   = [this] { onPlayTrajectory(); };

    orchestrator_.onSlotStateChanged =
        [this](int idx, iceberg::juce_app::SlotRenderState st) {
            onRenderStateChanged(idx, st);
        };
    orchestrator_.onError = [this](const std::string& msg) {
        status_bar_.setError(juce::String(msg));
    };

    player_.onSlotStarted = [this](int idx) {
        list_panel_.setCurrentlyPlayingIndex(idx);
        updatePlaybackButtons();
        updatePlaybackStatus();
    };
    player_.onSlotFinished = [this](int /*idx*/) {
        list_panel_.setCurrentlyPlayingIndex(-1);
        updatePlaybackButtons();
        updatePlaybackStatus();
    };
    player_.onSequenceFinished = [this] {
        list_panel_.setCurrentlyPlayingIndex(-1);
        status_bar_.setPlaybackStatus("Sequence finished.");
        updatePlaybackButtons();
    };
    player_.onWaitingForNext = [this] {
        status_bar_.setPlaybackStatus("Press Next to continue.");
        updatePlaybackButtons();
    };
    player_.onError = [this](const std::string& msg) {
        status_bar_.setError(juce::String(msg));
        list_panel_.setCurrentlyPlayingIndex(-1);
        updatePlaybackButtons();
    };

    render_all_btn_.onClick = [this] { onRenderAllStaleToggle(); };
    play_seq_btn_  .onClick = [this] { onPlaySequence(); };
    stop_btn_      .onClick = [this] { onStop(); };
    next_btn_      .onClick = [this] { onNext(); };
    wait_toggle_.onClick      = [this] { onWaitToggleChanged(); };
    wait_slider_.onValueChange = [this] { onWaitSliderChanged(); };
    prep_btn_.onClick         = [this] { onPrepToggleChanged(); };
    live_btn_.onClick         = [this] { onLiveToggleChanged(); };

    wait_toggle_.setToggleState(experiment_.play_options.wait_between_slots,
                                juce::dontSendNotification);
    prep_btn_   .setToggleState(experiment_.play_options.preparation_mode,
                                juce::dontSendNotification);
    refreshPrepButtonAppearance();

    list_panel_.setSelectedIndex(0);
    selectSlot(0);
    updateStatusCounts();
    updateMemoryLabel();
    updateRenderAllButton();
    updatePlaybackButtons();
    updatePlaybackStatus();

    // Polls AudioPlayer position for "Playing Slot N (M:SS / M:SS)".
    startTimer(200);

    // Window title reflects "Untitled" + clean.
    updateWindowTitle();
    applyAppearance();
}

MainComponent::~MainComponent() {
    juce::Desktop::getInstance().removeDarkModeSettingListener(this);
    calibration_window_.reset();
    if (live_worker_.joinable()) live_worker_.join();
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr);
   #endif
}

// ============ Appearance ===============================================

// The theme of the saved appearance (System follows the OS), applied to the
// default look and feel and sent to every window: this one, the room
// calibration window and any dialog open.
void MainComponent::applyAppearance() {
    const bool dark = appearance_ == Appearance::System
                          ? juce::Desktop::getInstance().isDarkModeActive()
                          : appearance_ == Appearance::Dark;
    if (auto* lnf = dynamic_cast<juce::LookAndFeel_V4*>(
            &juce::LookAndFeel::getDefaultLookAndFeel())) {
        iceberg::juce_app::apply_theme(dark, *lnf);
    }
    sendLookAndFeelChange();
    juce::Desktop& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i) {
        juce::Component* window = desktop.getComponent(i);
        if (window == nullptr) continue;
        if (auto* resizable = dynamic_cast<juce::ResizableWindow*>(window)) {
            resizable->setBackgroundColour(iceberg::juce_app::theme().window);
        }
        window->sendLookAndFeelChange();
    }
}

void MainComponent::setAppearance(Appearance a) {
    appearance_ = a;
    props_.getUserSettings()->setValue(
        kPropAppearance, a == Appearance::Light ? "light"
                         : a == Appearance::Dark ? "dark"
                                                 : "system");
    props_.saveIfNeeded();
    if (menu_model_) menu_model_->menuItemsChanged();
    applyAppearance();
}

void MainComponent::darkModeSettingChanged() {
    if (appearance_ == Appearance::System) applyAppearance();
}

void MainComponent::lookAndFeelChanged() {
    applyColours();
}

// The colours this component sets on its own widgets; the rest come from
// the look and feel's scheme.
void MainComponent::applyColours() {
    const iceberg::juce_app::Palette& p = iceberg::juce_app::theme();
    for (juce::TextButton* b : {&live_btn_, &prep_btn_}) {
        b->setColour(juce::TextButton::buttonColourId, p.button_off);
        b->setColour(juce::TextButton::textColourOnId, p.on_text);
    }
    live_btn_.setColour(juce::TextButton::buttonOnColourId, p.live_on);
    prep_btn_.setColour(juce::TextButton::buttonOnColourId, p.prep_on);
    memory_label_.setColour(juce::Label::textColourId, p.text_dim);
    wait_caption_.setColour(juce::Label::textColourId, p.text_dim);
    repaint();
}

void MainComponent::timerCallback() {
    updatePlaybackStatus();
    refreshMenuIfChanged();
    updateSlotFields();
}

// The status bar's fields for the selected slot, refreshed by the timer
// like the menu: the IR file (its header, read once per path), the render
// (output and the split time it used) and the device's output latency.
void MainComponent::updateSlotFields() {
    StatusBar::SlotFields f;
    if (juce::AudioIODevice* device = player_.getDeviceManager().getCurrentAudioDevice()) {
        const double fs = device->getCurrentSampleRate();
        // What the device reports plus one buffer, the delay a sample
        // written by the player waits before it leaves the interface.
        if (fs > 0.0) {
            f.latency_ms = 1000.0 *
                (device->getOutputLatencyInSamples() + device->getCurrentBufferSizeSamples()) /
                fs;
        }
    }
    if (current_slot_index_ >= 0 &&
        current_slot_index_ < static_cast<int>(experiment_.slots.size())) {
        const auto& s = experiment_.slots[static_cast<size_t>(current_slot_index_)];
        const juce::String ir_path = juce::String::fromUTF8(s.ir_path.c_str());
        if (ir_path != ir_info_path_) {
            ir_info_path_ = ir_path;
            ir_info_channels_ = 0;
            ir_info_seconds_ = -1.0;
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            if (ir_path.isNotEmpty()) {
                if (std::unique_ptr<juce::AudioFormatReader> reader{
                        formats.createReaderFor(juce::File(ir_path))}) {
                    ir_info_channels_ = static_cast<int>(reader->numChannels);
                    ir_info_seconds_ =
                        static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
                }
            }
        }
        f.ir_channels = ir_info_channels_;
        f.ir_seconds = ir_info_seconds_;
        if (s.output_buffer && s.cached_metrics) {
            f.out_channels = static_cast<int>(s.output_buffer->samples.cols());
            f.out_seconds = static_cast<double>(s.output_buffer->samples.rows()) /
                            s.output_buffer->sample_rate;
            f.out_peak = s.cached_metrics->peak_abs;
            f.ctime_s = s.cached_metrics->c_time;
        }
    }
    status_bar_.setSlotFields(f);
}

// The macOS menu bar keeps the enabled states of its last getMenuForIndex()
// until menuItemsChanged(). Renders finish on worker threads and edits come
// from many places, so the timer watches the states instead of every call
// site remembering to rebuild. Mirrors the conditions in getMenuForIndex().
void MainComponent::refreshMenuIfChanged() {
    const int state = (experiment_.slots.empty() ? 0 : 1) |
                      (canExportWav(false) ? 2 : 0) |
                      (canExportWav(true) ? 4 : 0) |
                      (orchestrator_.getStaleCount() > 0 ? 8 : 0) |
                      (orchestrator_.getCurrentBufferRamBytes() > 0 ? 16 : 0);
    if (state == menu_state_) return;
    menu_state_ = state;
    if (menu_model_) menu_model_->menuItemsChanged();
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(iceberg::juce_app::theme().window);
}

void MainComponent::resized() {
    auto area = getLocalBounds();
    if (menu_bar_) {
        menu_bar_->setBounds(area.removeFromTop(getLookAndFeel().getDefaultMenuBarHeight()));
    }

    constexpr int kStatusHeight = 28;
    status_bar_.setBounds(area.removeFromBottom(kStatusHeight));

    // Top action bar (32 px): Render All Stale + memory label.
    constexpr int kTopBar = 32;
    auto top = area.removeFromTop(kTopBar).reduced(8, 4);
    render_all_btn_.setBounds(top.removeFromLeft(220));
    top.removeFromLeft(8);
    memory_label_.setBounds(top);

    // Playback bar (32 px): Play sequence / Stop / Next + Wait toggle + slider.
    auto pb = area.removeFromTop(kTopBar).reduced(8, 4);
    play_seq_btn_.setBounds(pb.removeFromLeft(120));
    pb.removeFromLeft(6);
    stop_btn_    .setBounds(pb.removeFromLeft(70));
    pb.removeFromLeft(6);
    next_btn_    .setBounds(pb.removeFromLeft(70));
    pb.removeFromLeft(16);
    wait_toggle_ .setBounds(pb.removeFromLeft(170));
    pb.removeFromLeft(6);
    wait_caption_.setBounds(pb.removeFromLeft(48));
    // Prep button on the right edge so it's visually separated.
    prep_btn_    .setBounds(pb.removeFromRight(150));
    pb.removeFromRight(8);
    live_btn_    .setBounds(pb.removeFromRight(100));
    pb.removeFromRight(8);
    wait_slider_ .setBounds(pb);

    constexpr int kListWidth = 320;
    list_panel_ .setBounds(area.removeFromLeft(kListWidth));
    props_panel_.setBounds(area);
}

iceberg::experiment::Slot MainComponent::makeDefaultSlot(int slot_number) {
    iceberg::experiment::Slot s;
    s.name             = "Slot " + std::to_string(slot_number);
    s.source_angle_deg = 0.0;
    // Default slot uses full-scale mode (cal_path nullopt), so the
    // level field is interpreted in dBFS. -20 dBFS is a safe headroom
    // default; user adjusts via the slider.
    s.level_db         = -20.0;
    s.is_rendered      = false;
    s.is_stale         = true;
    return s;
}

void MainComponent::selectSlot(int idx) {
    current_slot_index_ = idx;
    if (idx >= 0 && idx < static_cast<int>(experiment_.slots.size())) {
        props_panel_.setSlot(&experiment_.slots[static_cast<size_t>(idx)]);
    } else {
        props_panel_.setSlot(nullptr);
    }
    list_panel_.setSelectedIndex(idx);
}

void MainComponent::onSlotSelected(int idx) {
    if (idx == current_slot_index_) return;
    selectSlot(idx);
}

void MainComponent::onAddSlot() {
    const int n = static_cast<int>(experiment_.slots.size());
    experiment_.slots.push_back(makeDefaultSlot(n + 1));
    orchestrator_.resyncSlotCount();
    list_panel_.refresh();
    selectSlot(n);
    updateStatusCounts();
    updateRenderAllButton();
    markDirty();
}

void MainComponent::onRemoveSlot(int idx) {
    if (idx < 0 || idx >= static_cast<int>(experiment_.slots.size())) return;

    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon,
        "Remove slot",
        "Remove '" + juce::String(experiment_.slots[static_cast<size_t>(idx)].name) +
            "'? This cannot be undone.",
        "Remove", "Cancel", this,
        juce::ModalCallbackFunction::create([this, idx](int result) {
            if (result != 1) return;
            experiment_.slots.erase(experiment_.slots.begin() + idx);
            orchestrator_.resyncSlotCount();
            const int n = static_cast<int>(experiment_.slots.size());
            const int new_sel = (n == 0) ? -1 : std::min(idx, n - 1);
            list_panel_.refresh();
            selectSlot(new_sel);
            updateStatusCounts();
            updateMemoryLabel();
            updateRenderAllButton();
            markDirty();
        }));
}

void MainComponent::onDuplicateSlot(int idx) {
    if (idx < 0 || idx >= static_cast<int>(experiment_.slots.size())) return;

    // Deep-copy the slot via the implicit Slot copy constructor.
    // PRESERVED: ir_path, layout_path, cal_path, signal_path,
    // source_angle_deg, level_db, the default LS order and the
    // v1.0.0 options. The intent of "Duplicate" is
    // "prepare a quick variation" - typically the user changes one
    // param (angle / level) and re-renders. Clearing paths would
    // force a full re-Browse and defeat the workflow.
    // RESET: only the runtime / output fields so the copy behaves
    // as a fresh slot ready to be rendered.
    iceberg::experiment::Slot copy =
        experiment_.slots[static_cast<size_t>(idx)];
    copy.name = copy.name + " (copy)";
    copy.is_rendered = false;
    copy.is_stale    = true;
    copy.output_buffer.reset();
    copy.error_message.reset();
    copy.render_notices.clear();
    copy.trajectory_buffer.reset();
    copy.cached_metrics.reset();

    experiment_.slots.insert(experiment_.slots.begin() + idx + 1,
                             std::move(copy));
    orchestrator_.resyncSlotCount();
    list_panel_.refresh();
    selectSlot(idx + 1);
    updateStatusCounts();
    updateRenderAllButton();
    markDirty();
}

void MainComponent::onReorder(int from, int to) {
    const int n = static_cast<int>(experiment_.slots.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;

    auto moved = std::move(experiment_.slots[static_cast<size_t>(from)]);
    experiment_.slots.erase(experiment_.slots.begin() + from);
    const int insert_pos = std::min(to,
        static_cast<int>(experiment_.slots.size()));
    experiment_.slots.insert(experiment_.slots.begin() + insert_pos,
                             std::move(moved));

    orchestrator_.resyncSlotCount();
    const int new_sel = insert_pos;
    list_panel_.refresh();
    selectSlot(new_sel);
    updateStatusCounts();
    markDirty();
}

void MainComponent::onRenameSlot(int idx) {
    if (idx < 0 || idx >= static_cast<int>(experiment_.slots.size())) return;

    auto* aw = new juce::AlertWindow(
        "Rename slot",
        "Enter a new name for this slot:",
        juce::AlertWindow::QuestionIcon);
    aw->addTextEditor(
        "name",
        juce::String(experiment_.slots[static_cast<size_t>(idx)].name),
        juce::String());
    aw->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    aw->enterModalState(true,
        juce::ModalCallbackFunction::create(
            [this, idx, aw](int result) {
                if (result == 1) {
                    const auto new_name =
                        aw->getTextEditorContents("name").trim();
                    if (new_name.isNotEmpty() &&
                        idx < static_cast<int>(experiment_.slots.size())) {
                        experiment_.slots[static_cast<size_t>(idx)].name =
                            new_name.toStdString();
                        list_panel_.refresh();
                        markDirty();
                    }
                }
                delete aw;
            }), false);
}

void MainComponent::onSlotEdited() {
    // While the edited slot plays live, its angle drives the engine.
    if (player_.isLive() && player_.currentSlotIndex() == current_slot_index_ &&
        current_slot_index_ >= 0 &&
        current_slot_index_ < static_cast<int>(experiment_.slots.size())) {
        player_.setLiveAngle(
            experiment_.slots[static_cast<size_t>(current_slot_index_)].source_angle_deg);
    }
    if (current_slot_index_ >= 0 &&
        current_slot_index_ < static_cast<int>(experiment_.slots.size())) {
        // Slot is now stale; resync the orchestrator's mirror so the
        // badge flips immediately.
        orchestrator_.resyncSlotCount();
    }
    list_panel_.refresh();
    updateStatusCounts();
    updateRenderAllButton();
    updatePlaybackButtons();
    props_panel_.refreshFromSlot();
    markDirty();
}

void MainComponent::onBrowseField(SlotPropertiesPanel::BrowseField field) {
    if (current_slot_index_ < 0 ||
        current_slot_index_ >= static_cast<int>(experiment_.slots.size())) return;
    using F = SlotPropertiesPanel::BrowseField;

    juce::String title;
    juce::String filter;
    switch (field) {
        case F::IR:          title = "Select IR (.wav, B-Format)";      filter = "*.wav"; break;
        case F::Layout:      title = "Select layout (.json)";           filter = "*.json"; break;
        case F::Calibration: title = "Select calibration (.json)";      filter = "*.json"; break;
        case F::Signal:
            title = "Select signal (mono)";
            filter = iceberg::juce_app::audio::signal_file_wildcard();
            break;
    }

    active_chooser_ = std::make_unique<juce::FileChooser>(
        title, juce::File{}, filter);
    const auto flags = juce::FileBrowserComponent::openMode |
                       juce::FileBrowserComponent::canSelectFiles;
    const int idx_capture = current_slot_index_;
    active_chooser_->launchAsync(flags,
        [this, field, idx_capture](const juce::FileChooser& fc) {
            const auto file = fc.getResult();
            if (file == juce::File{}) return;
            if (idx_capture < 0 ||
                idx_capture >= static_cast<int>(experiment_.slots.size())) return;
            const std::string p = file.getFullPathName().toStdString();

            // Open IR / Signal once, fail gracefully on read error. Only
            // the IR fixes the experiment's rate (a render comes out at
            // its IR's rate; a signal at another rate is resampled to
            // it), so only an IR browse is checked across slots.
            int signal_n_ch = 1;  // populated when field == Signal
            if (field == F::IR || field == F::Signal) {
                double new_fs = 0.0;
                try {
                    const auto sig = field == F::Signal
                                         ? iceberg::juce_app::audio::load_signal_file(p)
                                         : iceberg::io::load_wav(p);
                    new_fs = sig.sample_rate;
                    signal_n_ch = static_cast<int>(sig.samples.cols());
                } catch (const std::exception& e) {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        "Cannot read file",
                        juce::String(e.what()));
                    return;
                }

                const auto exp_fs =
                    iceberg::juce_app::RenderOrchestrator::
                        getExperimentSampleRate(experiment_, idx_capture);
                if (field == F::IR && exp_fs &&
                    std::abs(*exp_fs - new_fs) > 0.5) {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        "Sample rate mismatch",
                        "The experiment's IRs are at " +
                            juce::String(*exp_fs, 0) +
                            " Hz; the selected IR is at " +
                            juce::String(new_fs, 0) +
                            " Hz. All slots play on one device, so "
                            "their IRs must share a rate.");
                    return;
                }
            }

            // Signal must be mono;
            // if user picked a multi-channel file, ask before
            // accepting (only channel 0 is used downstream).
            auto applyPath = [this, field, idx_capture, p]() {
                if (idx_capture < 0 ||
                    idx_capture >= static_cast<int>(experiment_.slots.size()))
                    return;
                auto& slot = experiment_.slots[
                    static_cast<size_t>(idx_capture)];
                switch (field) {
                    case F::IR:          slot.ir_path = p;     break;
                    case F::Layout:      slot.layout_path = p; break;
                    case F::Calibration: slot.cal_path = p;    break;
                    case F::Signal:      slot.signal_path = p; break;
                }
                slot.is_stale = true;
                onSlotEdited();
                selectSlot(idx_capture);
            };

            if (field == F::Signal && signal_n_ch > 1) {
                juce::AlertWindow::showOkCancelBox(
                    juce::AlertWindow::QuestionIcon,
                    "Signal is multi-channel",
                    "Signal has " + juce::String(signal_n_ch) +
                        " channels. Only channel 0 will be used. "
                        "Continue?",
                    "Use channel 0", "Cancel", this,
                    juce::ModalCallbackFunction::create(
                        [applyPath](int result) {
                            if (result == 1) applyPath();
                        }));
                return;
            }

            applyPath();
        });
}

void MainComponent::onRenderThisSlot() {
    if (current_slot_index_ < 0) return;

    // SPL mode requires a calibration JSON. Modal warning
    // before render kick-off so the user can either Browse a cal or
    // switch back to full scale. Avoids a silent error in the
    // status bar for the single-slot interactive path. Batch
    // (Render All Stale) emits the orchestrator's runtime_error
    // through the status bar.
    const auto& slot =
        experiment_.slots[static_cast<size_t>(current_slot_index_)];
    if (!slot.use_full_scale && !slot.cal_path.has_value()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Calibration required",
            "'Use full scale' is OFF for this slot, but no "
            "calibration JSON has been selected. Browse a cal file "
            "or switch back to full scale mode.");
        return;
    }

    status_bar_.setError("");
    orchestrator_.renderSlot(current_slot_index_);
    updateRenderAllButton();
}

void MainComponent::onClearBuffer() {
    if (current_slot_index_ < 0) return;
    orchestrator_.clearSlotBuffer(current_slot_index_);
    list_panel_.refresh();
    props_panel_.refreshFromSlot();
    updateStatusCounts();
    updateMemoryLabel();
    updateRenderAllButton();
    updatePlaybackButtons();
    markDirty();
}

void MainComponent::onRenderAllStaleToggle() {
    if (orchestrator_.isAnythingRendering()) {
        orchestrator_.cancelAll();
    } else {
        // Pre-flight RAM warning: sum of estimated bytes across
        // stale slots.
        size_t projected = orchestrator_.getCurrentBufferRamBytes();
        for (size_t i = 0; i < experiment_.slots.size(); ++i) {
            if (experiment_.slots[i].is_stale)
                projected += orchestrator_.estimateRenderBytes(static_cast<int>(i));
        }
        const double projected_gb = static_cast<double>(projected) / kBytesPerGB;
        if (projected_gb > orchestrator_.getMemoryBudgetGB()) {
            juce::AlertWindow::showOkCancelBox(
                juce::AlertWindow::QuestionIcon,
                "Memory budget warning",
                "This batch is projected to use " +
                    juce::String(projected_gb, 2) + " GB of RAM, exceeding the " +
                    juce::String(orchestrator_.getMemoryBudgetGB(), 1) +
                    " GB budget. Continue?",
                "Render", "Cancel", this,
                juce::ModalCallbackFunction::create([this](int result) {
                    if (result != 1) return;
                    orchestrator_.renderAllStale();
                    updateRenderAllButton();
                }));
            return;
        }
        orchestrator_.renderAllStale();
    }
    updateRenderAllButton();
}

void MainComponent::onRenderStateChanged(
    int slot_index, iceberg::juce_app::SlotRenderState new_state) {
    // Point at the notices iceberg() returned; the slot panel
    // lists them.
    if (new_state == iceberg::juce_app::SlotRenderState::Rendered &&
        slot_index >= 0 &&
        slot_index < static_cast<int>(experiment_.slots.size())) {
        const auto& s = experiment_.slots[static_cast<size_t>(slot_index)];
        if (!s.render_notices.empty()) {
            status_bar_.setMessage(
                juce::String::fromUTF8(s.name.c_str()) + ": " +
                juce::String(static_cast<int>(s.render_notices.size())) +
                " render notice(s), see Last render in the slot panel.");
        }
    }
    list_panel_.refresh();
    updateStatusCounts();
    updateMemoryLabel();
    updateRenderAllButton();
    updatePlaybackButtons();
    props_panel_.refreshFromSlot();
}

void MainComponent::updateStatusCounts() {
    int ready = 0;
    int stale = 0;
    for (const auto& s : experiment_.slots) {
        if (s.is_rendered && !s.is_stale) ++ready;
        if (s.is_stale)                   ++stale;
    }
    const int n = static_cast<int>(experiment_.slots.size());
    status_bar_.setRenderCounts(ready, n, stale);
}

void MainComponent::updateMemoryLabel() {
    const double used_gb =
        static_cast<double>(orchestrator_.getCurrentBufferRamBytes()) /
        kBytesPerGB;
    memory_label_.setText(
        "Buffers: " + juce::String(used_gb, 2) + " / " +
            juce::String(orchestrator_.getMemoryBudgetGB(), 1) + " GB",
        juce::dontSendNotification);
}

void MainComponent::updateRenderAllButton() {
    if (orchestrator_.isAnythingRendering()) {
        render_all_btn_.setButtonText("Cancel");
        return;
    }
    const int n_stale = orchestrator_.getStaleCount();
    render_all_btn_.setButtonText(
        "Render All Stale (" + juce::String(n_stale) + ")");
    render_all_btn_.setEnabled(n_stale > 0);
}

void MainComponent::onPlayThisSlot() {
    if (current_slot_index_ < 0) return;
    status_bar_.setError("");
    if (live_btn_.getToggleState()) {
        playLive(current_slot_index_);
        return;
    }
    player_.playSlot(current_slot_index_);
    updatePlaybackButtons();
}

// Play the slot through the live engine, preparing it first on a
// worker thread unless a preparation from the same inputs (all but the
// angle) is already there.
void MainComponent::playLive(int slot_index) {
    if (live_preparing_.load()) return;
    if (slot_index < 0 || slot_index >= static_cast<int>(experiment_.slots.size())) return;
    auto& slot = experiment_.slots[static_cast<size_t>(slot_index)];
    const std::string key = iceberg::experiment::live_inputs_key(slot);
    if (slot.live_render && slot.live_key == key) {
        player_.playSlotLive(slot_index);
        updatePlaybackButtons();
        return;
    }
    player_.stop();
    if (live_worker_.joinable()) live_worker_.join();
    live_preparing_.store(true);
    status_bar_.setPlaybackStatus("Preparing " + juce::String::fromUTF8(slot.name.c_str()) +
                                  " for live playback...");
    const iceberg::experiment::Slot snapshot = slot;
    juce::Component::SafePointer<MainComponent> safe(this);
    live_worker_ = std::thread([safe, snapshot, key, slot_index] {
        std::shared_ptr<const iceberg::rendering::LiveRender> render;
        std::string error;
        try {
            render = iceberg::experiment::prepare_slot_live(
                snapshot, iceberg::juce_app::audio::load_signal_file(snapshot.signal_path));
        } catch (const std::exception& e) {
            error = e.what();
        }
        juce::MessageManager::callAsync([safe, render, error, key, slot_index] {
            if (safe == nullptr) return;
            safe->live_preparing_.store(false);
            if (!render) {
                safe->status_bar_.setPlaybackStatus("");
                safe->status_bar_.setError("Live preparation failed: " +
                                           juce::String::fromUTF8(error.c_str()));
                return;
            }
            if (slot_index >= static_cast<int>(safe->experiment_.slots.size())) return;
            auto& s = safe->experiment_.slots[static_cast<size_t>(slot_index)];
            s.live_render = render;
            s.live_key = key;
            safe->player_.playSlotLive(slot_index);
            safe->updatePlaybackButtons();
        });
    });
}

// Render the slot along its path on the live worker, reusing the
// live preparation when only the angle changed since it was made.
void MainComponent::onRenderTrajectory() {
    if (live_preparing_.load()) return;
    const int slot_index = current_slot_index_;
    if (slot_index < 0 || slot_index >= static_cast<int>(experiment_.slots.size())) return;
    auto& slot = experiment_.slots[static_cast<size_t>(slot_index)];
    if (slot.trajectory.empty()) return;
    player_.stop();
    if (live_worker_.joinable()) live_worker_.join();
    live_preparing_.store(true);
    status_bar_.setError("");
    status_bar_.setPlaybackStatus("Rendering the trajectory of " +
                                  juce::String::fromUTF8(slot.name.c_str()) + "...");
    const iceberg::experiment::Slot snapshot = slot;
    const std::string live_key = iceberg::experiment::live_inputs_key(slot);
    const std::string traj_key = iceberg::experiment::trajectory_key(slot);
    std::shared_ptr<const iceberg::rendering::LiveRender> prepared =
        (slot.live_render && slot.live_key == live_key) ? slot.live_render : nullptr;
    juce::Component::SafePointer<MainComponent> safe(this);
    live_worker_ = std::thread([safe, snapshot, live_key, traj_key, prepared, slot_index] {
        std::shared_ptr<const iceberg::rendering::LiveRender> render = prepared;
        std::shared_ptr<iceberg::Signal> buffer;
        std::string error;
        try {
            if (!render) {
                render = iceberg::experiment::prepare_slot_live(
                    snapshot, iceberg::juce_app::audio::load_signal_file(snapshot.signal_path));
            }
            buffer = std::make_shared<iceberg::Signal>(
                iceberg::rendering::render_trajectory(render, snapshot.trajectory));
        } catch (const std::exception& e) {
            error = e.what();
        }
        juce::MessageManager::callAsync([safe, render, buffer, error, live_key, traj_key,
                                         slot_index] {
            if (safe == nullptr) return;
            safe->live_preparing_.store(false);
            safe->status_bar_.setPlaybackStatus("");
            if (!buffer) {
                safe->status_bar_.setError("Trajectory render failed: " +
                                           juce::String::fromUTF8(error.c_str()));
                return;
            }
            if (slot_index >= static_cast<int>(safe->experiment_.slots.size())) return;
            auto& s = safe->experiment_.slots[static_cast<size_t>(slot_index)];
            s.live_render = render;
            s.live_key = live_key;
            s.trajectory_buffer = buffer;
            s.trajectory_key = traj_key;
            safe->status_bar_.setMessage(juce::String::fromUTF8(s.name.c_str()) +
                                         ": trajectory rendered (moving source: no MATLAB "
                                         "reference).");
            safe->props_panel_.refreshFromSlot();
            safe->list_panel_.refresh();
            safe->updateMemoryLabel();
        });
    });
}

bool MainComponent::canExportWav(bool trajectory) const {
    if (current_slot_index_ < 0 ||
        current_slot_index_ >= static_cast<int>(experiment_.slots.size())) return false;
    const auto& s = experiment_.slots[static_cast<size_t>(current_slot_index_)];
    if (trajectory) {
        return s.trajectory_buffer != nullptr &&
               s.trajectory_key == iceberg::experiment::trajectory_key(s);
    }
    return s.is_rendered && !s.is_stale && s.output_buffer != nullptr;
}

void MainComponent::onExportWav(bool trajectory) {
    if (!canExportWav(trajectory)) return;
    const auto& slot = experiment_.slots[static_cast<size_t>(current_slot_index_)];
    const std::shared_ptr<const iceberg::Signal> buffer =
        trajectory ? std::shared_ptr<const iceberg::Signal>(slot.trajectory_buffer)
                   : std::shared_ptr<const iceberg::Signal>(slot.output_buffer);
    const juce::String base = juce::File::createLegalFileName(
        juce::String::fromUTF8(slot.name.c_str()) + (trajectory ? " trajectory" : ""));
    active_chooser_ = std::make_unique<juce::FileChooser>(
        trajectory ? "Export trajectory render (WAV)" : "Export slot render (WAV)",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile(base + ".wav"),
        "*.wav");
    active_chooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [this, buffer](const juce::FileChooser& fc) {
            const auto file = fc.getResult();
            if (file == juce::File{}) return;
            try {
                // float32, so levels above 1.0 (a calibrated render is in
                // volts) are kept, not clipped.
                iceberg::io::write_wav(file.getFullPathName().toStdString(), *buffer);
                status_bar_.setMessage("Exported " + file.getFileName() + " (" +
                                       juce::String(static_cast<int>(buffer->samples.cols())) +
                                       " channels, float32, " +
                                       juce::String(buffer->sample_rate, 0) + " Hz).");
            } catch (const std::exception& e) {
                status_bar_.setError("Export failed: " + juce::String::fromUTF8(e.what()));
            }
        });
}

void MainComponent::onPlayTrajectory() {
    if (current_slot_index_ < 0) return;
    status_bar_.setError("");
    player_.playTrajectory(current_slot_index_);
    updatePlaybackButtons();
}

void MainComponent::onLiveToggleChanged() {
    const bool on = live_btn_.getToggleState();
    live_btn_.setButtonText(on ? "Live (ON)" : "Live (OFF)");
    if (!on && player_.isLive()) onStop();
    props_panel_.setLiveMode(on);
}

void MainComponent::onPlaySequence() {
    status_bar_.setError("");
    player_.playSequence();
    updatePlaybackButtons();
}

void MainComponent::onStop() {
    player_.stop();
    list_panel_.setCurrentlyPlayingIndex(-1);
    status_bar_.setPlaybackStatus("");
    updatePlaybackButtons();
}

void MainComponent::onNext() {
    player_.next();
    updatePlaybackButtons();
}

void MainComponent::onWaitToggleChanged() {
    experiment_.play_options.wait_between_slots =
        wait_toggle_.getToggleState();
    markDirty();
}

void MainComponent::onWaitSliderChanged() {
    experiment_.play_options.wait_seconds = wait_slider_.getValue();
    markDirty();
}

void MainComponent::onPrepToggleChanged() {
    const bool on = prep_btn_.getToggleState();
    experiment_.play_options.preparation_mode = on;
    refreshPrepButtonAppearance();
    markDirty();
}

void MainComponent::refreshPrepButtonAppearance() {
    const bool on = prep_btn_.getToggleState();
    prep_btn_.setButtonText(on ? "Prep mode (ON)" : "Prep mode (OFF)");
}

// ============ Dirty tracking + window title ===========================

void MainComponent::markDirty() {
    if (is_dirty_) return;
    is_dirty_ = true;
    updateWindowTitle();
}

void MainComponent::clearDirty() {
    if (!is_dirty_) return;
    is_dirty_ = false;
    updateWindowTitle();
}

// The constructor's updateWindowTitle() runs before the window holds this
// component, so the title is set again once it does.
void MainComponent::parentHierarchyChanged() {
    updateWindowTitle();
}

void MainComponent::updateWindowTitle() {
    auto* win = findParentComponentOfClass<juce::DocumentWindow>();
    if (!win) return;
    juce::String name = experiment_path_.isEmpty()
        ? juce::String("Untitled")
        : juce::File(experiment_path_).getFileNameWithoutExtension();
    win->setName(juce::String("Iceberg - ") + name +
                 (is_dirty_ ? " *" : ""));
}

// ============ Drag-drop file ==========================================

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files) {
    return files.size() == 1 &&
           files[0].endsWithIgnoreCase(".iceberg-exp");
}

void MainComponent::filesDropped(const juce::StringArray& files,
                                 int /*x*/, int /*y*/) {
    if (files.size() != 1) return;
    promptIfDirty([this, path = files[0]](bool proceed) {
        if (!proceed) return;
        loadExperimentFromPath(path);
    });
}

// ============ Dirty prompt ============================================

void MainComponent::promptIfDirty(
    std::function<void(bool)> on_decide) {
    if (!is_dirty_) { on_decide(true); return; }

    juce::AlertWindow::showYesNoCancelBox(
        juce::AlertWindow::QuestionIcon,
        "Unsaved changes",
        "The experiment has unsaved changes. Save before continuing?",
        "Save", "Discard", "Cancel", this,
        juce::ModalCallbackFunction::create(
            [this, cb = std::move(on_decide)](int result) {
                // 1 = Save, 2 = Discard, 0 = Cancel.
                if (result == 0) { cb(false); return; }
                if (result == 2) { cb(true);  return; }
                // Save first; if path empty, route through Save As.
                if (experiment_path_.isEmpty()) {
                    // Async Save As, then continue.
                    active_chooser_ = std::make_unique<juce::FileChooser>(
                        "Save Experiment As...",
                        juce::File{}, "*.iceberg-exp");
                    auto flags = juce::FileBrowserComponent::saveMode |
                                 juce::FileBrowserComponent::warnAboutOverwriting;
                    active_chooser_->launchAsync(flags,
                        [this, inner_cb = std::move(cb)](
                            const juce::FileChooser& fc) {
                            const auto f = fc.getResult();
                            if (f == juce::File{}) { inner_cb(false); return; }
                            saveExperimentToPath(f.getFullPathName());
                            inner_cb(true);
                        });
                    return;
                }
                saveExperimentToPath(experiment_path_);
                cb(true);
            }));
}

void MainComponent::canQuit(std::function<void(bool)> continuation) {
    promptIfDirty(std::move(continuation));
}

// ============ New / Open / Save / Save As =============================

void MainComponent::onNewExperiment() {
    promptIfDirty([this](bool proceed) {
        if (!proceed) return;
        player_.stop();
        orchestrator_.cancelAll();
        experiment_ = iceberg::experiment::Experiment{};
        experiment_.name = "Untitled Experiment";
        experiment_.slots.push_back(makeDefaultSlot(1));
        experiment_path_.clear();
        rebuildAfterExperimentLoad();
        clearDirty();
    });
}

void MainComponent::onOpenExperiment() {
    promptIfDirty([this](bool proceed) {
        if (!proceed) return;
        active_chooser_ = std::make_unique<juce::FileChooser>(
            "Open Experiment", juce::File{}, "*.iceberg-exp");
        auto flags = juce::FileBrowserComponent::openMode |
                     juce::FileBrowserComponent::canSelectFiles;
        active_chooser_->launchAsync(flags,
            [this](const juce::FileChooser& fc) {
                const auto f = fc.getResult();
                if (f == juce::File{}) return;
                loadExperimentFromPath(f.getFullPathName());
            });
    });
}

void MainComponent::onSaveExperiment() {
    if (experiment_path_.isEmpty()) {
        onSaveExperimentAs();
        return;
    }
    saveExperimentToPath(experiment_path_);
}

void MainComponent::onSaveExperimentAs() {
    active_chooser_ = std::make_unique<juce::FileChooser>(
        "Save Experiment As...", juce::File{}, "*.iceberg-exp");
    auto flags = juce::FileBrowserComponent::saveMode |
                 juce::FileBrowserComponent::warnAboutOverwriting;
    active_chooser_->launchAsync(flags,
        [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File{}) return;
            auto path = f.getFullPathName();
            if (!path.endsWithIgnoreCase(".iceberg-exp")) {
                path += ".iceberg-exp";
            }
            saveExperimentToPath(path);
        });
}

void MainComponent::loadExperimentFromPath(const juce::String& path) {
    try {
        player_.stop();
        orchestrator_.cancelAll();
        experiment_ = iceberg::experiment::load_experiment(
            path.toStdString());
        experiment_path_ = path;
        recent_files_.addFile(juce::File(path));
        persistRecentFiles();
        if (menu_model_) menu_model_->menuItemsChanged();
        rebuildAfterExperimentLoad();
        clearDirty();
        status_bar_.setError("");
    } catch (const std::exception& e) {
        // Silently remove a missing file from Recent (per spec F).
        recent_files_.removeFile(juce::File(path));
        persistRecentFiles();
        if (menu_model_) menu_model_->menuItemsChanged();
        status_bar_.setError(juce::String("Open failed: ") + e.what());
    }
}

void MainComponent::saveExperimentToPath(const juce::String& path) {
    try {
        iceberg::experiment::save_experiment(path.toStdString(),
                                             experiment_);
        experiment_path_ = path;
        recent_files_.addFile(juce::File(path));
        persistRecentFiles();
        if (menu_model_) menu_model_->menuItemsChanged();
        clearDirty();
        status_bar_.setError("");
    } catch (const std::exception& e) {
        status_bar_.setError(juce::String("Save failed: ") + e.what());
    }
}

void MainComponent::rebuildAfterExperimentLoad() {
    orchestrator_.setExperiment(&experiment_);
    player_.setExperiment(&experiment_);
    list_panel_.setExperiment(&experiment_);

    // Restore play_options to UI controls.
    wait_toggle_.setToggleState(
        experiment_.play_options.wait_between_slots,
        juce::dontSendNotification);
    wait_slider_.setValue(experiment_.play_options.wait_seconds,
                          juce::dontSendNotification);
    prep_btn_.setToggleState(experiment_.play_options.preparation_mode,
                             juce::dontSendNotification);
    refreshPrepButtonAppearance();

    const int sel = experiment_.slots.empty() ? -1 : 0;
    current_slot_index_ = sel;
    list_panel_.setSelectedIndex(sel);
    list_panel_.refresh();
    selectSlot(sel);
    updateStatusCounts();
    updateMemoryLabel();
    updateRenderAllButton();
    updatePlaybackButtons();
    updateWindowTitle();
}

void MainComponent::onRecentClicked(int recent_index) {
    if (recent_index < 0 ||
        recent_index >= recent_files_.getNumFiles()) return;
    const auto f = recent_files_.getFile(recent_index);
    promptIfDirty([this, path = f.getFullPathName()](bool proceed) {
        if (!proceed) return;
        loadExperimentFromPath(path);
    });
}

void MainComponent::onClearRecent() {
    recent_files_.clear();
    persistRecentFiles();
    if (menu_model_) menu_model_->menuItemsChanged();
}

// ============ Experiment menu ========================================

void MainComponent::onClearAllBuffers() {
    int n = 0;
    size_t bytes = 0;
    for (const auto& s : experiment_.slots) {
        if (s.output_buffer) {
            ++n;
            bytes += static_cast<size_t>(s.output_buffer->samples.size()) *
                     sizeof(float);
        }
    }
    if (n == 0) return;
    const double gb = static_cast<double>(bytes) / kBytesPerGB;
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon,
        "Clear all buffers",
        "Clear " + juce::String(n) + " buffer(s) (" +
            juce::String(gb, 2) + " GB)? Cannot be undone.",
        "Clear", "Cancel", this,
        juce::ModalCallbackFunction::create([this](int result) {
            if (result != 1) return;
            for (size_t i = 0; i < experiment_.slots.size(); ++i) {
                orchestrator_.clearSlotBuffer(static_cast<int>(i));
            }
            list_panel_.refresh();
            props_panel_.refreshFromSlot();
            updateStatusCounts();
            updateMemoryLabel();
            updateRenderAllButton();
            updatePlaybackButtons();
            markDirty();
        }));
}

void MainComponent::onAudioSettings() {
    auto* sel = new juce::AudioDeviceSelectorComponent(
        player_.getDeviceManager(),
        /*minIn*/0, /*maxIn*/0, /*minOut*/0, /*maxOut*/32,
        /*showMidiInputs*/false, /*showMidiOutputs*/false,
        /*showChannels*/true, /*hideAdvanced*/false);
    sel->setSize(560, 380);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(sel);
    opts.dialogTitle = "Audio Settings";
    opts.componentToCentreAround = this;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    auto* win = opts.launchAsync();
    if (win) {
        // Persist on close (per ajuste 4: only on close, not on
        // change events).
        win->setLookAndFeel(&getLookAndFeel());
        // The launched DialogWindow doesn't expose a "closed"
        // callback directly; instead, attach a listener on its
        // destruction by using a juce::Component::SafePointer
        // and a Timer. Simpler: poll once via async after the
        // user closes it. JUCE pattern: set a closeCallback on
        // ModalComponentManager.
        win->enterModalState(true,
            juce::ModalCallbackFunction::create(
                [this](int /*result*/) { persistDeviceState(); }),
            true);
    }
}

// Measure the room and write a calibration file. The
// window opens its own device connection (inputs and outputs) on the
// main output device; playback stops while it measures.
void MainComponent::onCalibrateRoom() {
    if (calibration_window_) {
        calibration_window_->toFront(true);
        return;
    }
    player_.stop();
    updatePlaybackButtons();
    calibration_window_ = std::make_unique<iceberg::juce_app::room_calibration::CalibrationWindow>(
        player_.getDeviceManager().createStateXml(), [this] {
            // Deleted outside its own callback.
            juce::MessageManager::callAsync([this] { calibration_window_.reset(); });
        });
}

void MainComponent::onMemoryBudget() {
    // Custom small dialog: slider 1.0..16.0 GB, step 0.5.
    auto* content = new juce::Component();
    content->setSize(360, 130);

    auto* slider = new juce::Slider();
    slider->setSliderStyle(juce::Slider::LinearHorizontal);
    slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 22);
    slider->setRange(1.0, 16.0, 0.5);
    slider->setValue(orchestrator_.getMemoryBudgetGB(),
                     juce::dontSendNotification);
    slider->setTextValueSuffix(" GB");
    slider->setBounds(16, 24, 328, 28);
    content->addAndMakeVisible(slider);

    auto* lbl = new juce::Label("budget_lbl",
                                 "Maximum RAM for slot output buffers:");
    lbl->setFont(juce::FontOptions{12.0f});
    lbl->setBounds(16, 4, 328, 18);
    content->addAndMakeVisible(lbl);

    auto* apply = new juce::TextButton("Apply");
    apply->setBounds(196, 84, 70, 30);
    content->addAndMakeVisible(apply);
    auto* cancel = new juce::TextButton("Cancel");
    cancel->setBounds(274, 84, 70, 30);
    content->addAndMakeVisible(cancel);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);
    opts.dialogTitle = "Memory Budget";
    opts.componentToCentreAround = this;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    auto* win = opts.launchAsync();
    apply->onClick = [this, slider, win] {
        const double v = slider->getValue();
        orchestrator_.setMemoryBudgetGB(v);
        persistMemoryBudget(v);
        updateMemoryLabel();
        if (win) win->exitModalState(1);
    };
    cancel->onClick = [win] { if (win) win->exitModalState(0); };
}

// ============ Persistence helpers ====================================

void MainComponent::persistRecentFiles() {
    auto* user = props_.getUserSettings();
    user->setValue(kPropRecentFiles, recent_files_.toString());
    props_.saveIfNeeded();
}

void MainComponent::persistDeviceState() {
    auto xml = player_.getDeviceManager().createStateXml();
    auto* user = props_.getUserSettings();
    if (xml) user->setValue(kPropDeviceState, xml->toString());
    else     user->removeValue(kPropDeviceState);
    props_.saveIfNeeded();
}

void MainComponent::persistMemoryBudget(double gb) {
    auto* user = props_.getUserSettings();
    user->setValue(kPropMemBudgetGB, gb);
    props_.saveIfNeeded();
}

// ============ MainMenu inner class ===================================

juce::StringArray MainComponent::MainMenu::getMenuBarNames() {
    return {"File", "Experiment", "View", "Calibration"};
}

juce::PopupMenu MainComponent::MainMenu::getMenuForIndex(
    int idx, const juce::String&) {
    juce::PopupMenu m;
    if (idx == 0) {
        m.addItem(1, "New Experiment");
        m.addItem(2, "Open Experiment...");
        m.addSeparator();
        m.addItem(3, "Save", !owner_.experiment_.slots.empty());
        m.addItem(4, "Save As...");
        m.addSeparator();
        m.addItem(5, "Export Slot Render (WAV)...", owner_.canExportWav(false));
        m.addItem(6, "Export Trajectory Render (WAV)...", owner_.canExportWav(true));
        m.addSeparator();
        juce::PopupMenu recent;
        const int n = owner_.recent_files_.getNumFiles();
        for (int i = 0; i < n; ++i) {
            const auto f = owner_.recent_files_.getFile(i);
            recent.addItem(100 + i, f.getFileName());
        }
        if (n > 0) recent.addSeparator();
        recent.addItem(99, "Clear Recent", n > 0);
        m.addSubMenu("Recent Experiments", recent, n > 0 || true);
        // (Quit is provided by the system app menu on macOS.)
    } else if (idx == 1) {
        m.addItem(50, "Render All Stale",
                  owner_.orchestrator_.getStaleCount() > 0);
        m.addItem(51, "Clear All Buffers",
                  owner_.orchestrator_.getCurrentBufferRamBytes() > 0);
        m.addSeparator();
        m.addItem(52, "Audio Settings...");
        m.addItem(53, "Memory Budget...");
    } else if (idx == 2) {
        juce::PopupMenu appearance;
        appearance.addItem(70, "System", true, owner_.appearance_ == Appearance::System);
        appearance.addItem(71, "Light", true, owner_.appearance_ == Appearance::Light);
        appearance.addItem(72, "Dark", true, owner_.appearance_ == Appearance::Dark);
        m.addSubMenu("Appearance", appearance);
    } else if (idx == 3) {
        m.addItem(60, "Calibrate room (experimental)...");
    }
    return m;
}

void MainComponent::MainMenu::menuItemSelected(int item_id, int /*top*/) {
    switch (item_id) {
        case 1:  owner_.onNewExperiment();      break;
        case 2:  owner_.onOpenExperiment();     break;
        case 3:  owner_.onSaveExperiment();     break;
        case 4:  owner_.onSaveExperimentAs();   break;
        case 5:  owner_.onExportWav(false);     break;
        case 6:  owner_.onExportWav(true);      break;
        case 50: owner_.onRenderAllStaleToggle(); break;
        case 51: owner_.onClearAllBuffers();    break;
        case 52: owner_.onAudioSettings();      break;
        case 53: owner_.onMemoryBudget();       break;
        case 60: owner_.onCalibrateRoom();      break;
        case 70: owner_.setAppearance(Appearance::System); break;
        case 71: owner_.setAppearance(Appearance::Light);  break;
        case 72: owner_.setAppearance(Appearance::Dark);   break;
        case 99: owner_.onClearRecent();        break;
        default:
            if (item_id >= 100 && item_id < 200) {
                owner_.onRecentClicked(item_id - 100);
            }
            break;
    }
}

bool MainComponent::anyRenderedSlot() const {
    for (const auto& s : experiment_.slots) {
        if (s.is_rendered && s.output_buffer) return true;
    }
    return false;
}

void MainComponent::updatePlaybackButtons() {
    const bool playing = player_.isPlaying();
    const bool waiting = player_.isWaitingForNext();
    play_seq_btn_.setEnabled(!playing && !waiting && anyRenderedSlot());
    stop_btn_   .setEnabled(playing || waiting);
    next_btn_   .setVisible(waiting);
    next_btn_   .setEnabled(waiting);
}

void MainComponent::updatePlaybackStatus() {
    if (!player_.isPlaying()) {
        // Don't clobber a "Sequence finished." or "Press Next" message
        // set elsewhere; only clear when there's no audio activity at
        // all (no current source AND not waiting).
        if (!player_.isWaitingForNext()) {
            // Leave whatever was last set; explicit clear happens on Stop
            // or when starting playback.
        }
        return;
    }
    const int idx = player_.currentSlotIndex();
    if (idx < 0) return;
    const double pos = player_.currentSlotPositionSec();
    const double dur = player_.currentSlotDurationSec();
    auto fmt = [](double t) {
        const int total_s = static_cast<int>(t);
        const int m = total_s / 60;
        const int s = total_s % 60;
        return juce::String(m) + ":" +
               (s < 10 ? "0" : "") + juce::String(s);
    };
    juce::String name = "Slot " + juce::String(idx);
    if (idx >= 0 && idx < static_cast<int>(experiment_.slots.size())) {
        name = juce::String(experiment_.slots[static_cast<size_t>(idx)].name);
    }
    status_bar_.setPlaybackStatus(
        (player_.isLive() ? "Live " : "Playing ") + name + " (" + fmt(pos) + " / " +
        fmt(dur) + ")" + (player_.isLive() ? "  moving source: no MATLAB reference" : ""));
}
