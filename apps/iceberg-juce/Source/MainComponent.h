#pragma once

#include "audio/AudioPlayer.h"
#include "components/SlotListPanel.h"
#include "components/SlotPropertiesPanel.h"
#include "components/StatusBar.h"
#include "render/RenderOrchestrator.h"
#include "room_calibration/CalibrationComponent.h"
#include "Theme.h"

#include "experiment/experiment.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

/// Drag-drop `.iceberg-exp` and macOS native menu bar are
/// hosted by `MainComponent`. The menu bar model is a private inner
/// class; ApplicationProperties + RecentlyOpenedFilesList live as
/// members so save/restore is straightforward.
class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::DarkModeSettingListener,
                      private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void lookAndFeelChanged() override;

    // FileDragAndDropTarget - accept a single .iceberg-exp file.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files,
                      int x, int y) override;

    /// Called by `IcebergApplication::systemRequestedQuit` and
    /// `MainWindow::closeButtonPressed`. If unsaved changes,
    /// presents Save / Discard / Cancel async; otherwise invokes
    /// `continuation(true)` immediately. Always invokes on the
    /// message thread.
    void canQuit(std::function<void(bool /*proceed*/)> continuation);

private:
    void timerCallback() override;

    // View > Appearance: the theme follows the OS (System) or is fixed;
    // saved in the preferences.
    using Appearance = iceberg::juce_app::Appearance;
    Appearance appearance_ = Appearance::System;
    void darkModeSettingChanged() override;
    void setAppearance(Appearance a);
    void applyAppearance();
    void applyColours();
    iceberg::experiment::Experiment experiment_;
    int  current_slot_index_ = -1;

    iceberg::juce_app::RenderOrchestrator orchestrator_;
    iceberg::juce_app::audio::AudioPlayer player_;

    SlotListPanel       list_panel_;
    SlotPropertiesPanel props_panel_;
    StatusBar           status_bar_;
    // Shows the setTooltip() texts of this window's controls; JUCE shows
    // none without one (the calibration window has its own).
    juce::TooltipWindow tooltip_window_ {this, 400};

    juce::TextButton    render_all_btn_ {"Render All Stale (0)"};
    juce::Label         memory_label_   {"mem", "Buffers: 0.0 / 3.0 GB"};

    juce::TextButton    play_seq_btn_   {"Play sequence"};
    juce::TextButton    stop_btn_       {"Stop"};
    juce::TextButton    next_btn_       {"Next"};
    juce::ToggleButton  wait_toggle_    {"Wait between slots"};
    juce::Slider        wait_slider_;
    juce::Label         wait_caption_   {"wait_cap", "wait (s)"};
    juce::TextButton    prep_btn_       {"Prep mode (OFF)"};
    // Live mode: "Play this" goes through the live engine and the pad moves
    // the source while it plays. A session mode, not saved.
    juce::TextButton    live_btn_       {"Live (OFF)"};

    std::unique_ptr<juce::FileChooser> active_chooser_;

    // ---- Files and menu bar ----
    juce::ApplicationProperties  props_;
    juce::RecentlyOpenedFilesList recent_files_;
    juce::String                  experiment_path_;   // empty = unsaved
    bool                          is_dirty_ = false;

    class MainMenu : public juce::MenuBarModel {
    public:
        explicit MainMenu(MainComponent& owner) : owner_(owner) {}
        juce::StringArray getMenuBarNames() override;
        juce::PopupMenu getMenuForIndex(int idx,
                                        const juce::String&) override;
        void menuItemSelected(int item_id, int top_idx) override;
    private:
        MainComponent& owner_;
    };
    std::unique_ptr<MainMenu> menu_model_;
    // Windows and Linux: the menus in the window (declared after the model,
    // so it goes first).
    std::unique_ptr<juce::MenuBarComponent> menu_bar_;
    // Enabled states the menu bar was last built with (refreshMenuIfChanged).
    int menu_state_ = -1;
    void refreshMenuIfChanged();

    // The selected slot's IR header, read once per path (updateSlotFields).
    juce::String ir_info_path_;
    int          ir_info_channels_ = 0;
    double       ir_info_seconds_ = -1.0;
    void updateSlotFields();

    // Action handlers wired into list_panel_ / props_panel_.
    void onSlotSelected(int idx);
    void onAddSlot();
    void onRemoveSlot(int idx);
    void onDuplicateSlot(int idx);
    void onReorder(int from, int to);
    void onRenameSlot(int idx);
    void onSlotEdited();
    void onBrowseField(SlotPropertiesPanel::BrowseField field);
    void onRenderThisSlot();
    void onClearBuffer();
    void onRenderAllStaleToggle();
    void onRenderStateChanged(int slot_index,
                              iceberg::juce_app::SlotRenderState new_state);
    void onPlayThisSlot();
    void onPlaySequence();
    void onStop();
    void onNext();
    void onWaitToggleChanged();
    void onWaitSliderChanged();
    void onPrepToggleChanged();
    void refreshPrepButtonAppearance();
    void onLiveToggleChanged();
    void playLive(int slot_index);
    void onRenderTrajectory();
    void onPlayTrajectory();
    // Writes the selected slot's render (or its trajectory render) as a
    // float32 WAV, one channel per output.
    void onExportWav(bool trajectory);
    bool canExportWav(bool trajectory) const;

    // Menu handlers.
    void onNewExperiment();
    void onOpenExperiment();
    void onSaveExperiment();
    void onSaveExperimentAs();
    void onRecentClicked(int recent_index);
    void onClearRecent();
    void onClearAllBuffers();
    void onAudioSettings();
    void onMemoryBudget();
    void onCalibrateRoom();

    // File / persistence helpers.
    void loadExperimentFromPath(const juce::String& path);
    void saveExperimentToPath(const juce::String& path);
    void rebuildAfterExperimentLoad();
    void markDirty();
    void clearDirty();
    void updateWindowTitle();
    void persistRecentFiles();
    void persistDeviceState();
    void persistMemoryBudget(double gb);
    void promptIfDirty(std::function<void(bool /*proceed*/)> on_decide);

    void selectSlot(int idx);
    void updateStatusCounts();
    void updateMemoryLabel();
    void updateRenderAllButton();
    void updatePlaybackButtons();
    void updatePlaybackStatus();
    bool anyRenderedSlot() const;
    static iceberg::experiment::Slot makeDefaultSlot(int slot_number);

    // The room calibration window, open or null. Last
    // member, so it goes before the player it may be measuring beside.
    std::unique_ptr<iceberg::juce_app::room_calibration::CalibrationWindow> calibration_window_;

    // Prepares a slot for live playback off the message thread.
    std::thread live_worker_;
    std::atomic<bool> live_preparing_ {false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
