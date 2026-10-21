#include "SlotPropertiesPanel.h"

#include "Theme.h"
#include "experiment/slot_config.h"
#include "rendering/trajectory.h"

#include <vector>

namespace {
// ASCII-only suffix strings.
constexpr const char* kAngleSuffix = " deg";

// The level slider has two semantics
// driven by the "Use full scale" toggle. With cal loaded it maps
// to a real-world SPL target (0..95 dB SPL, default 80). With
// neutral cal (full scale mode) it is a dBFS attenuation of the
// raw output (-60..0 dBFS, default -20). The slider value is the
// `level_db` field passed straight into iceberg() -- no conversion
// in the lib, since neutral_calibration's math already collapses
// `level_db` into a dBFS-like gain (lsdBperVolt = 0,
// filter = identity -> output = signal * 10^(level_db / 20)).
constexpr double kSplMin     = 0.0;
constexpr double kSplMax     = 95.0;
constexpr double kSplDefault = 80.0;
constexpr const char* kSplSuffix = " dB SPL";

constexpr double kFsMin      = -60.0;
constexpr double kFsMax      = 0.0;
constexpr double kFsDefault  = -20.0;
constexpr const char* kFsSuffix = " dBFS";

// The 4 default loudspeakers. A channel menu's item id is the index
// here + 1 (ComboBox ids must be non-zero).
constexpr std::array<int, 4> kDefaultAngles = {0, 90, 180, 270};

int menuIdForAngle(int angle) {
    for (size_t i = 0; i < kDefaultAngles.size(); ++i) {
        if (kDefaultAngles[i] == angle) return static_cast<int>(i) + 1;
    }
    return 0;
}

// ComboBox ids of the option menus.
enum FormatId { kFuMa = 1, kAmbiX, kN3D };
enum ConventionId { kCounterClockwise = 1, kClockwise };
enum DserId { kUnity = 1, kThesis, kScale };

// Library strings (notices, error messages, paths) are UTF-8.
juce::String fromLib(const std::string& s) {
    return juce::String::fromUTF8(s.c_str());
}
}  // namespace

SlotPropertiesPanel::SlotPropertiesPanel() {
    auto styleCaption = [](juce::Label& l) {
        l.setFont(juce::FontOptions{14.0f, juce::Font::bold});
    };
    auto styleSubCaption = [](juce::Label& l) {
        l.setFont(juce::FontOptions{12.0f});
    };
    auto stylePath = [](juce::Label& l) {
        l.setFont(juce::FontOptions{12.0f});
        l.setMinimumHorizontalScale(0.5f);
        l.setBorderSize(juce::BorderSize<int>(2, 8, 2, 8));
    };

    addAndMakeVisible(pad_);
    pad_.setTooltip(
        "Drag to set the source angle (0 at the top; 90 on the left counter-clockwise, on "
        "the right clockwise). Numbered dots are the loudspeakers by output channel; the lit "
        "ones carry the source now. An amber spiral is the slot's trajectory, from the "
        "outside in, start to end.");
    pad_.on_angle_changed = [this](double a) {
        angle_slider_.setValue(a, juce::dontSendNotification);
        if (current_slot_) {
            current_slot_->source_angle_deg = a;
            current_slot_->is_stale = true;
            if (onSlotEdited) onSlotEdited();
        }
    };

    for (auto* l : {&angle_caption_, &level_caption_,
                    &ir_caption_, &layout_caption_,
                    &cal_caption_, &signal_caption_,
                    &last_render_caption_}) {
        styleCaption(*l);
        captions_.push_back(l);
        addAndMakeVisible(*l);
    }
    for (auto* l : {&layout_file_caption_, &format_caption_,
                    &convention_caption_, &dser_caption_, &trajectory_caption_,
                    &move_caption_, &hold_caption_}) {
        styleSubCaption(*l);
        sub_captions_.push_back(l);
        addAndMakeVisible(*l);
    }
    for (auto* l : {&ir_path_label_, &layout_path_label_,
                    &cal_path_label_, &signal_path_label_}) {
        stylePath(*l);
        path_labels_.push_back(l);
        addAndMakeVisible(*l);
    }

    angle_slider_.setSliderStyle(juce::Slider::LinearHorizontal);
    angle_slider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 22);
    angle_slider_.setRange(0.0, 360.0, 0.1);
    angle_slider_.setTextValueSuffix(kAngleSuffix);
    angle_slider_.onValueChange = [this] { writeAngle(); };
    addAndMakeVisible(angle_slider_);

    level_slider_.setSliderStyle(juce::Slider::LinearHorizontal);
    level_slider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 22);
    // Range / suffix re-applied per slot in syncFromSlot() based on
    // the toggle state. SPL defaults shown here so the empty-slot
    // state reads sensibly.
    level_slider_.setRange(kSplMin, kSplMax, 0.1);
    level_slider_.setTextValueSuffix(kSplSuffix);
    level_slider_.onValueChange = [this] { writeLevel(); };
    addAndMakeVisible(level_slider_);

    for (size_t c = 0; c < channel_menus_.size(); ++c) {
        auto& label = channel_labels_[c];
        label.setText("ch " + juce::String(static_cast<int>(c) + 1),
                      juce::dontSendNotification);
        styleSubCaption(label);
        label.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(label);

        auto& menu = channel_menus_[c];
        for (size_t i = 0; i < kDefaultAngles.size(); ++i) {
            menu.addItem(juce::String(kDefaultAngles[i]) + kAngleSuffix,
                         static_cast<int>(i) + 1);
        }
        const int channel = static_cast<int>(c);
        menu.onChange = [this, channel] { writeChannelAngle(channel); };
        addAndMakeVisible(menu);
    }

    format_menu_.addItem("FuMa", kFuMa);
    format_menu_.addItem("AmbiX", kAmbiX);
    format_menu_.addItem("N3D", kN3D);
    convention_menu_.addItem("Counter-clockwise (90 left)", kCounterClockwise);
    convention_menu_.addItem("Clockwise (90 right)", kClockwise);
    dser_menu_.addItem("Unity", kUnity);
    dser_menu_.addItem("Thesis (x max DSER)", kThesis);
    dser_menu_.addItem("Scale", kScale);
    for (auto* m : {&format_menu_, &convention_menu_, &dser_menu_}) {
        m->onChange = [this] { writeOptions(); };
        addAndMakeVisible(*m);
    }
    dser_value_.setSliderStyle(juce::Slider::IncDecButtons);
    dser_value_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 56, 22);
    dser_value_.setRange(0.0, 10.0, 0.01);
    dser_value_.setValue(1.0, juce::dontSendNotification);
    dser_value_.onValueChange = [this] { writeOptions(); };
    addAndMakeVisible(dser_value_);
    anechoic_toggle_.onClick = [this] { writeOptions(); };
    addAndMakeVisible(anechoic_toggle_);

    trajectory_edit_.setFont(juce::FontOptions{13.0f});
    const juce::String traj_tip =
        "A programmed path for the source: time:angle pairs in seconds and degrees. The "
        "angle moves in a straight line in time between two points and holds before the "
        "first and after the last. Angles are taken as written: 0:0, 3:270 turns "
        "counter-clockwise through 90 and 180; 0:0, 3:-90 goes the short way; 0:0, 4:720 "
        "goes twice round. Angles keep 2 decimals. Press Enter to take it; Render "
        "trajectory renders it, the slot's own render stays at the fixed angle.";
    trajectory_caption_.setTooltip(traj_tip);
    trajectory_edit_.setTooltip(traj_tip);
    trajectory_edit_.onReturnKey = [this] { writeTrajectory(); };
    trajectory_edit_.onFocusLost = [this] { writeTrajectory(); };
    addAndMakeVisible(trajectory_edit_);

    const juce::String move_tip =
        "Add point: how long the source takes to get from the last point to this one, in "
        "seconds. It goes the short way round; for the long way, edit the text.";
    const juce::String hold_tip =
        "Add point: how long the source stays at this position, in seconds (0: it moves on "
        "at once).";
    for (auto* s : {&move_, &hold_}) {
        s->setSliderStyle(juce::Slider::IncDecButtons);
        s->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 46, 22);
        addAndMakeVisible(*s);
    }
    move_.setRange(0.05, 60.0, 0.05);
    move_.setValue(1.0, juce::dontSendNotification);
    hold_.setRange(0.0, 60.0, 0.05);
    hold_.setValue(1.0, juce::dontSendNotification);
    move_caption_.setTooltip(move_tip);
    move_.setTooltip(move_tip);
    hold_caption_.setTooltip(hold_tip);
    hold_.setTooltip(hold_tip);
    add_point_btn_.setTooltip(
        "Drag the source on the pad to where it should go, set Move and Hold, then Add "
        "point: the position joins the end of the trajectory (the first one at time 0). "
        "The text above updates and stays editable.");
    clear_path_btn_.setTooltip("Removes the whole trajectory.");
    add_point_btn_.onClick = [this] { addTrajectoryPoint(); };
    clear_path_btn_.onClick = [this] { clearTrajectory(); };
    addAndMakeVisible(add_point_btn_);
    addAndMakeVisible(clear_path_btn_);

    addAndMakeVisible(ir_browse_);
    addAndMakeVisible(layout_browse_);
    addAndMakeVisible(layout_clear_);
    addAndMakeVisible(cal_browse_);
    addAndMakeVisible(signal_browse_);
    layout_clear_.onClick = [this] { clearLayoutFile(); };

    addAndMakeVisible(neutral_toggle_);
    neutral_toggle_.onClick = [this] { writeNeutral(); };

    addAndMakeVisible(render_btn_);
    addAndMakeVisible(clear_btn_);
    addAndMakeVisible(play_btn_);
    render_btn_.onClick = [this] { if (onRenderThisSlot) onRenderThisSlot(); };
    clear_btn_ .onClick = [this] { if (onClearBuffer)    onClearBuffer(); };
    play_btn_  .onClick = [this] { if (onPlayThisSlot)   onPlayThisSlot(); };
    addAndMakeVisible(render_traj_btn_);
    addAndMakeVisible(play_traj_btn_);
    render_traj_btn_.setTooltip("Renders the slot with the source following its trajectory "
                                "(the live engine, offline): the same result every time. "
                                "No MATLAB reference for a moving source.");
    play_traj_btn_.setTooltip("Plays the trajectory render. Prep mode folds it down to stereo.");
    render_traj_btn_.onClick = [this] { if (onRenderTrajectory) onRenderTrajectory(); };
    play_traj_btn_  .onClick = [this] { if (onPlayTrajectory)   onPlayTrajectory(); };

    last_render_box_.setMultiLine(true, true);
    last_render_box_.setReadOnly(true);
    last_render_box_.setCaretVisible(false);
    last_render_box_.setScrollbarsShown(true);
    last_render_box_.setFont(juce::FontOptions{12.0f});
    addAndMakeVisible(last_render_box_);

    auto wireBrowse = [this](juce::TextButton& b, BrowseField field) {
        b.onClick = [this, field] {
            if (onBrowseRequested) onBrowseRequested(field);
        };
    };
    wireBrowse(ir_browse_,     BrowseField::IR);
    wireBrowse(layout_browse_, BrowseField::Layout);
    wireBrowse(cal_browse_,    BrowseField::Calibration);
    wireBrowse(signal_browse_, BrowseField::Signal);

    setControlsEnabled(false);
    applyColours();
}

void SlotPropertiesPanel::lookAndFeelChanged() {
    applyColours();
}

// The colours set on this panel's widgets; buttons, sliders and menus take
// the look and feel's scheme.
void SlotPropertiesPanel::applyColours() {
    const iceberg::juce_app::Palette& p = iceberg::juce_app::theme();
    for (juce::Label* l : captions_) l->setColour(juce::Label::textColourId, p.text);
    for (juce::Label* l : sub_captions_) l->setColour(juce::Label::textColourId, p.text_dim);
    for (juce::Label* l : path_labels_) {
        l->setColour(juce::Label::textColourId, p.text_dim);
        l->setColour(juce::Label::backgroundColourId, p.field);
        l->setColour(juce::Label::outlineColourId, p.outline);
    }
    trajectory_edit_.setColour(juce::TextEditor::backgroundColourId, p.field);
    trajectory_edit_.setTextToShowWhenEmpty("e.g. 0:0, 2:90, 3:90, 5:-90", p.placeholder);
    // A TextEditor keeps the colour its text was typed with, the old
    // theme's after a switch; this also sets the colour of new text.
    trajectory_edit_.applyColourToAllText(p.text);
    outlineTrajectory(trajectory_taken_);
    last_render_box_.setColour(juce::TextEditor::backgroundColourId, p.field);
    last_render_box_.setColour(juce::TextEditor::outlineColourId, p.outline);
    last_render_box_.setColour(juce::TextEditor::textColourId, p.text_dim);
    // A TextEditor keeps the colour its text was set with; set it again.
    last_render_box_.applyColourToAllText(p.text_dim);
    repaint();
}

void SlotPropertiesPanel::paint(juce::Graphics& g) {
    g.fillAll(iceberg::juce_app::theme().panel);
    g.setColour(iceberg::juce_app::theme().outline);
    g.drawHorizontalLine(getHeight() - 1, 0.0f,
                         static_cast<float>(getWidth()));
}

void SlotPropertiesPanel::resized() {
    auto area = getLocalBounds().reduced(16);

    // Top: pad on the left (square ~240px); sliders, then the v1.0.0
    // options in two columns, on the right.
    constexpr int kPadSize = 240;
    auto top = area.removeFromTop(kPadSize);
    auto pad_area = top.removeFromLeft(kPadSize);
    pad_.setBounds(pad_area);

    top.removeFromLeft(16);  // gap
    auto sliders = top;
    // Captions beside the two sliders, so the trajectory rows fit below.
    auto slider_row = sliders.removeFromTop(28);
    angle_caption_.setBounds(slider_row.removeFromLeft(110));
    angle_slider_ .setBounds(slider_row);
    sliders.removeFromTop(6);
    slider_row = sliders.removeFromTop(28);
    level_caption_.setBounds(slider_row.removeFromLeft(110));
    level_slider_ .setBounds(slider_row);
    sliders.removeFromTop(8);

    const int col_width = (sliders.getWidth() - 16) / 2;
    auto optionRow = [&](juce::Label& left_cap, juce::Component& left,
                         juce::Label* right_cap, juce::Component& right) {
        auto caps = sliders.removeFromTop(16);
        left_cap.setBounds(caps.removeFromLeft(col_width));
        caps.removeFromLeft(16);
        if (right_cap) right_cap->setBounds(caps);
        auto ctrls = sliders.removeFromTop(24);
        left.setBounds(ctrls.removeFromLeft(col_width));
        ctrls.removeFromLeft(16);
        right.setBounds(ctrls);
        sliders.removeFromTop(8);
    };
    optionRow(format_caption_, format_menu_, &convention_caption_, convention_menu_);
    // The dser row's left column holds the menu and, next to it, the
    // scale for "Scale".
    {
        auto caps = sliders.removeFromTop(16);
        dser_caption_.setBounds(caps.removeFromLeft(col_width));
        auto ctrls = sliders.removeFromTop(24);
        auto left = ctrls.removeFromLeft(col_width);
        dser_value_.setBounds(left.removeFromRight(100));
        left.removeFromRight(6);
        dser_menu_.setBounds(left);
        ctrls.removeFromLeft(16);
        anechoic_toggle_.setBounds(ctrls);
    }
    sliders.removeFromTop(4);
    trajectory_caption_.setBounds(sliders.removeFromTop(16));
    trajectory_edit_.setBounds(sliders.removeFromTop(24));
    sliders.removeFromTop(6);
    {
        auto r = sliders.removeFromTop(26);
        move_caption_.setBounds(r.removeFromLeft(54));
        move_.setBounds(r.removeFromLeft(100));
        r.removeFromLeft(8);
        hold_caption_.setBounds(r.removeFromLeft(50));
        hold_.setBounds(r.removeFromLeft(100));
        r.removeFromLeft(8);
        clear_path_btn_.setBounds(r.removeFromRight(84));
        r.removeFromRight(6);
        add_point_btn_.setBounds(r);
    }

    area.removeFromTop(12);

    // Loudspeakers: the 4 channel menus on one row, the layout file
    // (which overrides them) on the next.
    layout_caption_.setBounds(area.removeFromTop(18));
    {
        auto row = area.removeFromTop(26);
        for (size_t c = 0; c < channel_menus_.size(); ++c) {
            channel_labels_[c].setBounds(row.removeFromLeft(40));
            row.removeFromLeft(4);
            channel_menus_[c].setBounds(row.removeFromLeft(96));
            row.removeFromLeft(12);
        }
    }
    area.removeFromTop(4);
    {
        auto row = area.removeFromTop(26);
        layout_file_caption_.setBounds(row.removeFromLeft(150));
        layout_browse_.setBounds(row.removeFromRight(80));
        row.removeFromRight(8);
        layout_clear_.setBounds(row.removeFromRight(60));
        row.removeFromRight(8);
        layout_path_label_.setBounds(row);
    }
    area.removeFromTop(12);

    // Each row is [caption] / [filename label + Browse]; no
    // ComboBox. Filename label is read-only with a faint outline so
    // it reads as a "field" rather than free text.
    auto fileRow = [&](juce::Label& cap, juce::Label& path,
                       juce::TextButton& btn) {
        cap.setBounds(area.removeFromTop(18));
        auto picker_row = area.removeFromTop(28);
        btn.setBounds(picker_row.removeFromRight(80));
        picker_row.removeFromRight(8);
        path.setBounds(picker_row);
        area.removeFromTop(8);
    };

    fileRow(ir_caption_,     ir_path_label_,     ir_browse_);
    fileRow(cal_caption_,    cal_path_label_,    cal_browse_);
    neutral_toggle_.setBounds(area.removeFromTop(22));
    area.removeFromTop(8);
    fileRow(signal_caption_, signal_path_label_, signal_browse_);

    area.removeFromTop(8);
    auto btn_row = area.removeFromTop(32);
    const int fifth = (btn_row.getWidth() - 4 * 8) / 5;
    for (auto* b : {&render_btn_, &play_btn_, &render_traj_btn_, &play_traj_btn_}) {
        b->setBounds(btn_row.removeFromLeft(fifth));
        btn_row.removeFromLeft(8);
    }
    clear_btn_.setBounds(btn_row);

    area.removeFromTop(8);
    last_render_caption_.setBounds(area.removeFromTop(18));
    last_render_box_.setBounds(area);
}

void SlotPropertiesPanel::setSlot(iceberg::experiment::Slot* slot) {
    current_slot_ = slot;
    setControlsEnabled(slot != nullptr);
    syncFromSlot();
    refreshFromSlot();
}

void SlotPropertiesPanel::refreshFromSlot() {
    if (!current_slot_) {
        clear_btn_.setEnabled(false);
        play_btn_ .setEnabled(false);
        render_traj_btn_.setEnabled(false);
        play_traj_btn_.setEnabled(false);
        last_render_box_.setText("", false);
        return;
    }
    const bool has_buf = current_slot_->is_rendered ||
                         current_slot_->output_buffer != nullptr;
    clear_btn_.setEnabled(has_buf);
    const bool has_files =
        !current_slot_->ir_path.empty() && !current_slot_->signal_path.empty();
    play_btn_ .setEnabled(live_mode_ ? has_files
                                     : current_slot_->is_rendered &&
                                           current_slot_->output_buffer != nullptr);
    render_traj_btn_.setEnabled(has_files && !current_slot_->trajectory.empty());
    play_traj_btn_.setEnabled(current_slot_->trajectory_buffer != nullptr &&
                              current_slot_->trajectory_key ==
                                  iceberg::experiment::trajectory_key(*current_slot_));

    // What iceberg() reported (MATLAB's warnings: resampling,
    // format conversion, order reduction, a suspect direct sound), or
    // why the render failed.
    juce::String text;
    if (current_slot_->error_message) {
        text = "Error: " + fromLib(*current_slot_->error_message);
    } else if (current_slot_->is_rendered) {
        if (current_slot_->render_notices.empty()) {
            text = "No notices.";
        }
        for (const auto& n : current_slot_->render_notices) {
            text += "- " + fromLib(n.message) + "\n";
        }
    }
    last_render_box_.setText(text.trimEnd(), false);
}

void SlotPropertiesPanel::setLiveMode(bool on) {
    live_mode_ = on;
    play_btn_.setButtonText(on ? "Play live" : "Play this");
    refreshFromSlot();
}

void SlotPropertiesPanel::syncFromSlot() {
    if (!current_slot_) {
        angle_slider_.setValue(0.0, juce::dontSendNotification);
        level_slider_.setValue(80.0, juce::dontSendNotification);
        pad_.setAngle(0.0);
        pad_.setLayout({0.0, 90.0, 180.0, 270.0}, {0, 1, 2, 3});
        pad_.setClockwise(false);
        for (size_t c = 0; c < channel_menus_.size(); ++c) {
            channel_menus_[c].setSelectedId(static_cast<int>(c) + 1,
                                            juce::dontSendNotification);
        }
        format_menu_    .setSelectedId(kFuMa, juce::dontSendNotification);
        convention_menu_.setSelectedId(kCounterClockwise, juce::dontSendNotification);
        dser_menu_      .setSelectedId(kUnity, juce::dontSendNotification);
        dser_value_.setValue(1.0, juce::dontSendNotification);
        anechoic_toggle_.setToggleState(false, juce::dontSendNotification);
        trajectory_edit_.setText("", false);
        pad_.setTrajectory({});
        ir_path_label_    .setText("(no file selected)", juce::dontSendNotification);
        layout_path_label_.setText("(none)",             juce::dontSendNotification);
        cal_path_label_   .setText("(full scale)",       juce::dontSendNotification);
        signal_path_label_.setText("(no file selected)", juce::dontSendNotification);
        neutral_toggle_.setToggleState(true, juce::dontSendNotification);
        return;
    }

    angle_slider_.setValue(current_slot_->source_angle_deg,
                           juce::dontSendNotification);

    // Slider semantics from the BOUND slot's
    // `use_full_scale` flag (independent of cal_path so the UI
    // stays consistent if the user toggles OFF before browsing
    // a calibration).
    const bool full_scale = current_slot_->use_full_scale;
    if (full_scale) {
        level_slider_.setRange(kFsMin, kFsMax, 0.1);
        level_slider_.setTextValueSuffix(kFsSuffix);
    } else {
        level_slider_.setRange(kSplMin, kSplMax, 0.1);
        level_slider_.setTextValueSuffix(kSplSuffix);
    }
    level_slider_.setValue(current_slot_->level_db,
                           juce::dontSendNotification);
    pad_.setAngle(current_slot_->source_angle_deg);

    syncOptions();
    syncLayout();
    trajectory_edit_.setText(iceberg::rendering::format_trajectory(current_slot_->trajectory),
                             false);
    pad_.setTrajectory(current_slot_->trajectory);
    outlineTrajectory(true);

    auto loadedLabel = [](const std::string& s, const char* placeholder) {
        if (s.empty()) return juce::String(placeholder);
        return juce::File(fromLib(s)).getFileName();
    };
    ir_path_label_    .setText(loadedLabel(current_slot_->ir_path,
                                           "(no file selected)"),
                               juce::dontSendNotification);
    signal_path_label_.setText(loadedLabel(current_slot_->signal_path,
                                           "(no file selected)"),
                               juce::dontSendNotification);

    // Toggle "Use full scale" reflects `use_full_scale`:
    //   true  -> "(full scale)"; cal Browse disabled (cal_path
    //            ignored at render time).
    //   false -> shows cal filename if set, "(no cal selected)"
    //            otherwise; Browse enabled. Render-time check in
    //            MainComponent / RenderOrchestrator enforces a cal
    //            file before letting render proceed.
    neutral_toggle_.setToggleState(full_scale,
                                   juce::dontSendNotification);
    cal_browse_.setEnabled(!full_scale);
    if (full_scale) {
        cal_path_label_.setText("(full scale)",
                                juce::dontSendNotification);
    } else if (current_slot_->cal_path.has_value()) {
        cal_path_label_.setText(
            juce::File(fromLib(*current_slot_->cal_path)).getFileName(),
            juce::dontSendNotification);
    } else {
        cal_path_label_.setText("(no cal selected)",
                                juce::dontSendNotification);
    }
}

void SlotPropertiesPanel::syncLayout() {
    if (!current_slot_) return;
    const bool has_file = !current_slot_->layout_path.empty();
    for (size_t c = 0; c < channel_menus_.size(); ++c) {
        channel_menus_[c].setSelectedId(
            menuIdForAngle(current_slot_->default_ls_order[c]),
            juce::dontSendNotification);
        channel_menus_[c].setEnabled(!has_file);
    }
    layout_clear_.setEnabled(has_file);
    layout_path_label_.setText(
        has_file ? juce::File(fromLib(current_slot_->layout_path)).getFileName()
                 : juce::String("(none: the 4 loudspeakers above)"),
        juce::dontSendNotification);

    // The pad draws the layout the render uses (slot_layout), so the
    // highlighted pair is the pair that plays.
    try {
        const auto cfg = iceberg::experiment::slot_layout(*current_slot_);
        std::vector<double> ring(static_cast<size_t>(cfg.ls_array.size()));
        for (size_t i = 0; i < ring.size(); ++i) {
            ring[i] = static_cast<double>(cfg.ls_array(static_cast<Eigen::Index>(i)));
        }
        std::vector<int> active(static_cast<size_t>(cfg.active_ls_numbers.size()));
        for (size_t i = 0; i < active.size(); ++i) {
            // matlab: activeLSNumbers(i)  ->  c++: active_ls_numbers(i) - 1
            active[i] = cfg.active_ls_numbers(static_cast<Eigen::Index>(i)) - 1;
        }
        pad_.setLayout(std::move(ring), std::move(active));
    } catch (...) {
        pad_.setLayout({}, {});   // the pad says the layout is unreadable
    }
    pad_.setClockwise(current_slot_->azimuth_convention ==
                      iceberg::rendering::AzimuthConvention::Clockwise);
}

void SlotPropertiesPanel::syncOptions() {
    if (!current_slot_) return;
    using iceberg::rendering::AmbisonicFormat;
    using iceberg::rendering::AzimuthConvention;
    using Kind = iceberg::rendering::DserFactor::Kind;
    const auto& s = *current_slot_;
    format_menu_.setSelectedId(
        s.ambisonic_format == AmbisonicFormat::AmbiX ? kAmbiX
        : s.ambisonic_format == AmbisonicFormat::N3D ? kN3D : kFuMa,
        juce::dontSendNotification);
    convention_menu_.setSelectedId(
        s.azimuth_convention == AzimuthConvention::Clockwise ? kClockwise
                                                             : kCounterClockwise,
        juce::dontSendNotification);
    dser_menu_.setSelectedId(
        s.dser_factor.kind == Kind::Thesis    ? kThesis
        : s.dser_factor.kind == Kind::Numeric ? kScale : kUnity,
        juce::dontSendNotification);
    dser_value_.setValue(s.dser_factor.value, juce::dontSendNotification);
    dser_value_.setEnabled(s.dser_factor.kind == Kind::Numeric);
    anechoic_toggle_.setToggleState(s.anechoic_special_case,
                                    juce::dontSendNotification);
}

void SlotPropertiesPanel::markEdited() {
    current_slot_->is_stale = true;
    if (onSlotEdited) onSlotEdited();
}

void SlotPropertiesPanel::writeAngle() {
    if (!current_slot_) return;
    current_slot_->source_angle_deg = angle_slider_.getValue();
    pad_.setAngle(current_slot_->source_angle_deg);
    markEdited();
}

void SlotPropertiesPanel::writeLevel() {
    if (!current_slot_) return;
    current_slot_->level_db = level_slider_.getValue();
    markEdited();
}

// Picking an azimuth another channel holds swaps the two, so the order
// stays a permutation of 0/90/180/270 (what the loader and slot_layout
// accept).
void SlotPropertiesPanel::writeChannelAngle(int channel) {
    if (!current_slot_) return;
    const int id = channel_menus_[static_cast<size_t>(channel)].getSelectedId();
    if (id < 1 || id > static_cast<int>(kDefaultAngles.size())) return;
    const int angle = kDefaultAngles[static_cast<size_t>(id - 1)];
    auto& order = current_slot_->default_ls_order;
    const int previous = order[static_cast<size_t>(channel)];
    if (previous == angle) return;
    for (auto& a : order) {
        if (a == angle) a = previous;
    }
    order[static_cast<size_t>(channel)] = angle;
    syncLayout();
    markEdited();
}

void SlotPropertiesPanel::clearLayoutFile() {
    if (!current_slot_ || current_slot_->layout_path.empty()) return;
    current_slot_->layout_path.clear();
    syncLayout();
    markEdited();
}

void SlotPropertiesPanel::writeOptions() {
    if (!current_slot_) return;
    using iceberg::rendering::AmbisonicFormat;
    using iceberg::rendering::AzimuthConvention;
    using Kind = iceberg::rendering::DserFactor::Kind;
    auto& s = *current_slot_;
    const int format = format_menu_.getSelectedId();
    s.ambisonic_format = format == kAmbiX ? AmbisonicFormat::AmbiX
                       : format == kN3D   ? AmbisonicFormat::N3D
                                          : AmbisonicFormat::FuMa;
    s.azimuth_convention = convention_menu_.getSelectedId() == kClockwise
                               ? AzimuthConvention::Clockwise
                               : AzimuthConvention::CounterClockwise;
    const int dser = dser_menu_.getSelectedId();
    s.dser_factor.kind = dser == kThesis ? Kind::Thesis
                       : dser == kScale  ? Kind::Numeric
                                         : Kind::Unity;
    s.dser_factor.value = dser_value_.getValue();
    dser_value_.setEnabled(s.dser_factor.kind == Kind::Numeric);
    s.anechoic_special_case = anechoic_toggle_.getToggleState();
    pad_.setClockwise(s.azimuth_convention == AzimuthConvention::Clockwise);
    markEdited();
}

// A path that does not parse is not taken: the field outlines red and the
// Last render box says why. A path change leaves the static render as it is.
void SlotPropertiesPanel::writeTrajectory() {
    if (!current_slot_) return;
    try {
        const auto points =
            iceberg::rendering::parse_trajectory(trajectory_edit_.getText().toStdString());
        outlineTrajectory(true);
        trajectory_edit_.setText(iceberg::rendering::format_trajectory(points), false);
        if (iceberg::rendering::format_trajectory(points) ==
            iceberg::rendering::format_trajectory(current_slot_->trajectory)) {
            return;
        }
        takeTrajectory(points);
    } catch (const std::exception& e) {
        outlineTrajectory(false);
        last_render_box_.setText("Trajectory not taken: " + fromLib(e.what()), false);
    }
}

// Enter keeps the focus in the field, where JUCE draws the focused outline
// colour, so a rejected path sets both or the red would only show once the
// focus moves on.
void SlotPropertiesPanel::outlineTrajectory(bool taken) {
    trajectory_taken_ = taken;
    if (taken) {
        trajectory_edit_.setColour(juce::TextEditor::outlineColourId,
                                   iceberg::juce_app::theme().outline);
        trajectory_edit_.removeColour(juce::TextEditor::focusedOutlineColourId);
    } else {
        const juce::Colour red = iceberg::juce_app::theme().error;
        trajectory_edit_.setColour(juce::TextEditor::outlineColourId, red);
        trajectory_edit_.setColour(juce::TextEditor::focusedOutlineColourId, red);
    }
    trajectory_edit_.repaint();
}

// A new path: the text, the pad's drawing and the slot. Leaves the static
// render alone (no markEdited).
void SlotPropertiesPanel::takeTrajectory(std::vector<iceberg::rendering::TrajectoryPoint> points) {
    trajectory_edit_.setText(iceberg::rendering::format_trajectory(points), false);
    outlineTrajectory(true);
    pad_.setTrajectory(points);
    current_slot_->trajectory = std::move(points);
    if (onSlotEdited) onSlotEdited();
}

void SlotPropertiesPanel::addTrajectoryPoint() {
    if (!current_slot_) return;
    takeTrajectory(iceberg::rendering::append_trajectory_point(
        current_slot_->trajectory, current_slot_->source_angle_deg, move_.getValue(),
        hold_.getValue()));
}

void SlotPropertiesPanel::clearTrajectory() {
    if (!current_slot_ || current_slot_->trajectory.empty()) return;
    takeTrajectory({});
}

void SlotPropertiesPanel::writeNeutral() {
    if (!current_slot_) return;
    const bool on = neutral_toggle_.getToggleState();
    current_slot_->use_full_scale = on;
    cal_browse_.setEnabled(!on);
    if (on) {
        cal_path_label_.setText("(full scale)",
                                juce::dontSendNotification);
    } else if (current_slot_->cal_path.has_value()) {
        cal_path_label_.setText(
            juce::File(fromLib(*current_slot_->cal_path)).getFileName(),
            juce::dontSendNotification);
    } else {
        cal_path_label_.setText("(no cal selected)",
                                juce::dontSendNotification);
    }

    // Mode flip resets level_db to the new mode's default.
    // Conversion isn't attempted (SPL vs dBFS units don't map
    // directly: 80 dB SPL with neutral cal = 10000x gain).
    if (on) {
        current_slot_->level_db = kFsDefault;
        level_slider_.setRange(kFsMin, kFsMax, 0.1);
        level_slider_.setTextValueSuffix(kFsSuffix);
        level_slider_.setValue(kFsDefault, juce::dontSendNotification);
    } else {
        current_slot_->level_db = kSplDefault;
        level_slider_.setRange(kSplMin, kSplMax, 0.1);
        level_slider_.setTextValueSuffix(kSplSuffix);
        level_slider_.setValue(kSplDefault, juce::dontSendNotification);
    }

    markEdited();
}

void SlotPropertiesPanel::setControlsEnabled(bool on) {
    angle_slider_.setEnabled(on);
    level_slider_.setEnabled(on);
    ir_browse_.setEnabled(on);
    layout_browse_.setEnabled(on);
    layout_clear_.setEnabled(on);
    cal_browse_.setEnabled(on);
    signal_browse_.setEnabled(on);
    neutral_toggle_.setEnabled(on);
    for (auto& m : channel_menus_) m.setEnabled(on);
    format_menu_.setEnabled(on);
    convention_menu_.setEnabled(on);
    dser_menu_.setEnabled(on);
    dser_value_.setEnabled(on);
    anechoic_toggle_.setEnabled(on);
    trajectory_edit_.setEnabled(on);
    for (juce::Component* c : std::initializer_list<juce::Component*>{
             &move_, &hold_, &add_point_btn_, &clear_path_btn_}) {
        c->setEnabled(on);
    }
    pad_.setEnabled(on);
    render_btn_.setEnabled(on);
    clear_btn_ .setEnabled(false);  // refreshFromSlot will enable when applicable
    play_btn_  .setEnabled(false);  // refreshFromSlot will enable when applicable
    // syncLayout / syncOptions narrow the menus, Clear and the dser
    // scale to what the bound slot allows.
}
