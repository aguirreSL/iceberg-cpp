#pragma once

#include "SourcePositionPad.h"

#include "experiment/slot.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <functional>
#include <vector>

/// Per-slot properties editor, bound to a single
/// `iceberg::experiment::Slot*` rather than driving free callbacks.
/// Setting `nullptr` disables the UI (no slot selected). `onSlotEdited`
/// fires after each user edit so `MainComponent` can mark the slot stale
/// and refresh the list.
class SlotPropertiesPanel : public juce::Component {
public:
    SlotPropertiesPanel();
    ~SlotPropertiesPanel() override = default;

    /// Bind a slot to edit. `nullptr` disables all child controls.
    /// Called by MainComponent on selection change.
    void setSlot(iceberg::experiment::Slot* slot);

    /// Refresh per-slot status (render/clear/play enabled state and the
    /// last render's error or notices). Cheap; safe to call after every
    /// state change.
    void refreshFromSlot();

    /// Fires after any user edit (slider, toggle, preset, browse).
    std::function<void()> onSlotEdited;

    /// Browse field IDs. Surfaced via callback so MainComponent can
    /// run the dialog + cross-slot fs validation centrally.
    enum class BrowseField { IR, Layout, Calibration, Signal };
    std::function<void(BrowseField)> onBrowseRequested;

    /// Render-only buttons. MainComponent owns the orchestrator and
    /// dispatches the actual render/clear via these callbacks.
    std::function<void()> onRenderThisSlot;
    std::function<void()> onClearBuffer;
    std::function<void()> onPlayThisSlot;
    // The slot rendered along its path, and playing that.
    std::function<void()> onRenderTrajectory;
    std::function<void()> onPlayTrajectory;

    /// In live mode "Play this" needs only the slot's files
    /// (the live engine prepares the slot itself), not a rendered buffer.
    void setLiveMode(bool on);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    void syncFromSlot();
    void syncLayout();
    void syncOptions();
    void writeAngle();
    void writeLevel();
    void writeNeutral();
    void writeChannelAngle(int channel);
    void clearLayoutFile();
    void writeOptions();
    void writeTrajectory();
    void addTrajectoryPoint();
    void clearTrajectory();
    void takeTrajectory(std::vector<iceberg::rendering::TrajectoryPoint> points);
    void outlineTrajectory(bool taken);
    void applyColours();
    void markEdited();
    void setControlsEnabled(bool on);

    iceberg::experiment::Slot* current_slot_ = nullptr;
    bool live_mode_ = false;

    SourcePositionPad pad_;

    juce::Label  angle_caption_  {"angle_caption", "Source angle"};
    juce::Slider angle_slider_;
    juce::Label  level_caption_  {"level_caption", "Level"};
    juce::Slider level_slider_;

    // Each file row is [caption] [filename label, read-only] [Browse];
    // the filename label is the canonical "what file is loaded"
    // indicator.
    juce::Label      ir_caption_     {"ir_cap", "IR (.wav, B-Format)"};
    juce::Label      ir_path_label_  {"ir_path", "(no file selected)"};
    juce::TextButton ir_browse_      {"Browse"};

    // With no layout file the slot renders on 4 loudspeakers, one menu per output channel holding
    // its azimuth (a permutation of 0/90/180/270). Another grid comes
    // from a layout file, which disables the menus; Clear goes back.
    juce::Label      layout_caption_ {"layout_cap",
                                      "Loudspeakers (output channel: azimuth)"};
    std::array<juce::Label, 4>    channel_labels_;
    std::array<juce::ComboBox, 4> channel_menus_;
    juce::Label      layout_file_caption_ {"layout_file_cap", "or layout file (.json)"};
    juce::Label      layout_path_label_ {"layout_path", "(none)"};
    juce::TextButton layout_browse_  {"Browse"};
    juce::TextButton layout_clear_   {"Clear"};

    // Iceberg v1.0.0's options. Format and convention always hold a
    // value, so both are always declared.
    juce::Label    format_caption_     {"format_cap", "IR format"};
    juce::ComboBox format_menu_;
    juce::Label    convention_caption_ {"conv_cap", "Angles"};
    juce::ComboBox convention_menu_;
    juce::Label    dser_caption_       {"dser_cap", "Direct scale (dserFactor)"};
    juce::ComboBox dser_menu_;
    juce::Slider   dser_value_;
    juce::ToggleButton anechoic_toggle_ {"Anechoic case (10 ms split)"};

    // A programmed path, "time:angle" pairs; rendered by its own action,
    // the slot's render stays static.
    juce::Label      trajectory_caption_ {"traj_cap", "Trajectory (s:deg)"};
    juce::TextEditor trajectory_edit_;
    bool trajectory_taken_ = true;   // false: the red outline of a rejected path
    // Labels by style, filled in the constructor, coloured by applyColours().
    std::vector<juce::Label*> captions_;
    std::vector<juce::Label*> sub_captions_;
    std::vector<juce::Label*> path_labels_;
    // Building the path from the pad: drag the source,
    // set how long it takes to get there and how long it stays, Add point.
    juce::Label      move_caption_ {"move_cap", "Move (s)"};
    juce::Slider     move_;
    juce::Label      hold_caption_ {"hold_cap", "Hold (s)"};
    juce::Slider     hold_;
    juce::TextButton add_point_btn_ {"Add point"};
    juce::TextButton clear_path_btn_ {"Clear path"};

    juce::Label      cal_caption_    {"cal_cap", "Calibration (.json)"};
    juce::Label      cal_path_label_ {"cal_path", "(full scale)"};
    juce::TextButton cal_browse_     {"Browse"};
    juce::ToggleButton neutral_toggle_ {"Use full scale"};

    juce::Label      signal_caption_   {"sig_cap", "Signal"};
    juce::Label      signal_path_label_ {"sig_path", "(no file selected)"};
    juce::TextButton signal_browse_    {"Browse"};

    juce::TextButton render_btn_ {"Render this slot"};
    juce::TextButton clear_btn_  {"Clear buffer"};
    juce::TextButton play_btn_   {"Play this"};
    juce::TextButton render_traj_btn_ {"Render trajectory"};
    juce::TextButton play_traj_btn_   {"Play trajectory"};

    // The last render's error, or the notices iceberg() returned.
    juce::Label      last_render_caption_ {"last_cap", "Last render"};
    juce::TextEditor last_render_box_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlotPropertiesPanel)
};
