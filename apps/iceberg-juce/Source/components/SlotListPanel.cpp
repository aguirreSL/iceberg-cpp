#include "SlotListPanel.h"

#include "Theme.h"
#include "experiment/slot_config.h"
#include "rendering/trajectory.h"

#include <cmath>

namespace {
// Drag source ID; the only drag-drop source from this panel is a row
// index. Constant prevents collision with future drag sources.
constexpr const char* kDragSource = "iceberg.slot_list_row";
constexpr int kRowHeight = 28;

enum class MovingState { None, NotRendered, Rendered };

// Whether the slot has a trajectory and, if so, whether its trajectory
// render was made from the slot as it is now.
MovingState movingState(const iceberg::experiment::Slot& s) {
    if (s.trajectory.empty()) return MovingState::None;
    return s.trajectory_buffer != nullptr &&
                   s.trajectory_key == iceberg::experiment::trajectory_key(s)
               ? MovingState::Rendered
               : MovingState::NotRendered;
}
}  // namespace

SlotListPanel::SlotListPanel() {
    header_.setFont(juce::FontOptions{14.0f, juce::Font::bold});
    addAndMakeVisible(header_);

    list_.setRowHeight(kRowHeight);
    list_.setOutlineThickness(1);
    list_.setMultipleSelectionEnabled(false);
    addAndMakeVisible(list_);

    for (auto* b : {&add_btn_, &remove_btn_, &duplicate_btn_,
                    &up_btn_, &down_btn_}) {
        addAndMakeVisible(*b);
    }

    add_btn_.onClick       = [this] { if (onAddSlot) onAddSlot(); };
    remove_btn_.onClick    = [this] {
        if (onRemoveSlot && getSelectedIndex() >= 0)
            onRemoveSlot(getSelectedIndex());
    };
    duplicate_btn_.onClick = [this] {
        if (onDuplicateSlot && getSelectedIndex() >= 0)
            onDuplicateSlot(getSelectedIndex());
    };
    up_btn_.onClick   = [this] {
        const int t = computeMoveButtonTarget(true);
        if (t >= 0 && onReorder) onReorder(getSelectedIndex(), t);
    };
    down_btn_.onClick = [this] {
        const int t = computeMoveButtonTarget(false);
        if (t >= 0 && onReorder) onReorder(getSelectedIndex(), t);
    };
    applyColours();
}

void SlotListPanel::lookAndFeelChanged() {
    applyColours();
}

void SlotListPanel::applyColours() {
    const iceberg::juce_app::Palette& p = iceberg::juce_app::theme();
    header_.setColour(juce::Label::textColourId, p.text);
    list_.setColour(juce::ListBox::backgroundColourId, p.field);
    list_.setColour(juce::ListBox::outlineColourId, p.outline);
    list_.repaint();
    repaint();
}

void SlotListPanel::setExperiment(iceberg::experiment::Experiment* exp) {
    experiment_ = exp;
    refresh();
}

void SlotListPanel::setOrchestrator(
    iceberg::juce_app::RenderOrchestrator* orch) {
    orchestrator_ = orch;
    refresh();
}

void SlotListPanel::setCurrentlyPlayingIndex(int idx) {
    if (playing_idx_ == idx) return;
    playing_idx_ = idx;
    list_.repaint();
}

void SlotListPanel::setSelectedIndex(int idx) {
    if (idx >= 0 && experiment_ && idx < static_cast<int>(experiment_->slots.size())) {
        list_.selectRow(idx, juce::dontSendNotification);
    } else {
        list_.deselectAllRows();
    }
}

int SlotListPanel::getSelectedIndex() const {
    return list_.getSelectedRow();
}

void SlotListPanel::refresh() {
    const int n = experiment_ ? static_cast<int>(experiment_->slots.size()) : 0;
    header_.setText("Stimuli (" + juce::String(n) + ")",
                    juce::dontSendNotification);
    list_.updateContent();
    list_.repaint();
}

void SlotListPanel::paint(juce::Graphics& g) {
    g.fillAll(iceberg::juce_app::theme().panel);
    g.setColour(iceberg::juce_app::theme().outline);
    g.drawVerticalLine(getWidth() - 1, 0.0f,
                       static_cast<float>(getHeight()));
}

void SlotListPanel::resized() {
    auto area = getLocalBounds().reduced(12);
    header_.setBounds(area.removeFromTop(22));
    area.removeFromTop(6);

    auto buttons_area = area.removeFromBottom(64);
    list_.setBounds(area);
    layoutButtons(buttons_area);
}

void SlotListPanel::layoutButtons(juce::Rectangle<int> area) {
    auto top = area.removeFromTop(28);
    const int w = (top.getWidth() - 8) / 2;
    add_btn_   .setBounds(top.removeFromLeft(w));
    top.removeFromLeft(8);
    remove_btn_.setBounds(top);
    area.removeFromTop(4);

    const int w3 = (area.getWidth() - 16) / 3;
    duplicate_btn_.setBounds(area.removeFromLeft(w3));
    area.removeFromLeft(8);
    up_btn_       .setBounds(area.removeFromLeft(w3));
    area.removeFromLeft(8);
    down_btn_     .setBounds(area);
}

int SlotListPanel::getNumRows() {
    return experiment_
        ? static_cast<int>(experiment_->slots.size())
        : 0;
}

void SlotListPanel::paintListBoxItem(int row, juce::Graphics& g,
                                     int width, int height,
                                     bool selected) {
    if (!experiment_ || row < 0 ||
        row >= static_cast<int>(experiment_->slots.size())) return;

    const auto& s = experiment_->slots[static_cast<size_t>(row)];
    const iceberg::juce_app::Palette& p = iceberg::juce_app::theme();

    if (row == playing_idx_) {
        // Currently-playing highlight wins over selection. Translucent
        // green-blue tint distinguishes it visually.
        g.fillAll(p.row_playing);
    } else if (selected) {
        g.fillAll(p.row_selected);
    } else {
        g.fillAll((row & 1) ? p.row_alt : p.field);
    }

    // Right-edge "playing" triangle marker (Path-drawn, ASCII-pure).
    if (row == playing_idx_) {
        const float triw = 8.0f;
        const float trix = static_cast<float>(width) - triw - 4.0f;
        const float triy = (static_cast<float>(height) - triw) / 2.0f;
        juce::Path tri;
        tri.addTriangle(trix,        triy,
                        trix,        triy + triw,
                        trix + triw, triy + triw / 2.0f);
        g.setColour(p.playing_marker);
        g.fillPath(tri);
    }

    // Status badge. `juce::Path` shapes keep the source ASCII-pure.
    // State source: orchestrator if available, else fall back to slot
    // fields (lets the panel render before any orchestrator binds).
    constexpr int kBadgeSize = 12;
    const int by = (height - kBadgeSize) / 2;
    const int bx = 8;
    using St = iceberg::juce_app::SlotRenderState;
    St state = St::Idle;
    if (orchestrator_) {
        state = orchestrator_->getSlotState(row);
    } else {
        if (s.is_rendered && !s.is_stale)      state = St::Rendered;
        else if (s.is_rendered && s.is_stale)  state = St::Stale;
    }

    auto fx = static_cast<float>(bx);
    auto fy = static_cast<float>(by);
    auto fs = static_cast<float>(kBadgeSize);
    switch (state) {
        case St::Rendered:
            g.setColour(p.good);
            g.fillEllipse(fx, fy, fs, fs);
            break;
        case St::Stale: {
            juce::Path tri;
            tri.addTriangle(fx, fy + fs, fx + fs, fy + fs,
                            fx + fs / 2.0f, fy);
            g.setColour(p.warn);
            g.fillPath(tri);
            break;
        }
        case St::Rendering: {
            // Solid yellow circle = "in flight"; a static fill is
            // unambiguous.
            g.setColour(p.busy);
            g.fillEllipse(fx, fy, fs, fs);
            break;
        }
        case St::Queued:
            g.setColour(p.ls);    // grey solid
            g.fillEllipse(fx, fy, fs, fs);
            break;
        case St::Error:
            // Red X.
            g.setColour(p.error);
            g.drawLine(fx, fy, fx + fs, fy + fs, 2.0f);
            g.drawLine(fx, fy + fs, fx + fs, fy, 2.0f);
            break;
        case St::Cancelled:
        case St::Idle:
        default:
            g.setColour(p.idle);    // grey rect
            g.drawRect(fx, fy, fs, fs, 1.0f);
            break;
    }

    // A slot with a trajectory gets a small spiral before the
    // angle, the pad's drawing of a path: amber once its trajectory render
    // is current (a moving-source render to play), grey while it is not.
    const MovingState moving = movingState(s);
    constexpr int kSpiralSize = 14;
    if (moving != MovingState::None) {
        const float cx = static_cast<float>(width - 70 - kSpiralSize / 2 - 4);
        const float cy = static_cast<float>(height) / 2.0f;
        constexpr int kSteps = 48;
        juce::Path spiral;
        for (int i = 0; i <= kSteps; ++i) {
            // One and a half turns, counter-clockwise from the top, from
            // the outside in like the pad's.
            const float frac = static_cast<float>(i) / kSteps;
            const float a = frac * 3.0f * juce::MathConstants<float>::pi;
            const float r = (kSpiralSize / 2.0f) * (1.0f - 0.7f * frac);
            const juce::Point<float> q(cx - r * std::sin(a), cy - r * std::cos(a));
            if (i == 0) spiral.startNewSubPath(q);
            else        spiral.lineTo(q);
        }
        g.setColour(moving == MovingState::Rendered ? p.trajectory : p.idle);
        g.strokePath(spiral, juce::PathStrokeType(1.4f));
        const juce::Point<float> end = spiral.getCurrentPosition();
        g.fillEllipse(end.x - 2.0f, end.y - 2.0f, 4.0f, 4.0f);
    }

    g.setColour(p.text);
    g.setFont(juce::FontOptions{13.0f});
    const int text_x = bx + kBadgeSize + 8;
    g.drawText(juce::String(s.name),
               text_x, 0,
               width - text_x - 70 - (moving != MovingState::None ? kSpiralSize + 8 : 0),
               height, juce::Justification::centredLeft, true);

    // Right column: angle.
    g.setColour(p.text_faint);
    g.drawText(juce::String(s.source_angle_deg, 0) + " deg",
               width - 70, 0, 62, height,
               juce::Justification::centredRight, true);
}

juce::String SlotListPanel::getTooltipForRow(int row) {
    if (!experiment_ || row < 0 ||
        row >= static_cast<int>(experiment_->slots.size())) return {};
    const auto& s = experiment_->slots[static_cast<size_t>(row)];
    const juce::String path =
        juce::String::fromUTF8(iceberg::rendering::format_trajectory(s.trajectory).c_str());
    switch (movingState(s)) {
        case MovingState::Rendered:
            return "Moving source: the trajectory render is current (Play trajectory). "
                   "Path " + path + ".";
        case MovingState::NotRendered:
            return "Has a trajectory with no current render (Render trajectory). "
                   "Path " + path + ".";
        case MovingState::None:
        default:
            return {};
    }
}

void SlotListPanel::selectedRowsChanged(int last_row) {
    if (onSelectionChanged) onSelectionChanged(last_row);
}

void SlotListPanel::listBoxItemClicked(int row,
                                       const juce::MouseEvent& e) {
    if (onSelectionChanged) onSelectionChanged(row);

    // Right-click context menu.
    if (e.mods.isPopupMenu() && row >= 0) {
        juce::PopupMenu menu;
        menu.addItem(1, "Rename...");
        menu.addItem(2, "Duplicate");
        menu.addSeparator();
        menu.addItem(3, "Remove");
        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&list_),
            [this, row](int result) {
                switch (result) {
                    case 1: if (onRenameSlot)    onRenameSlot(row);    break;
                    case 2: if (onDuplicateSlot) onDuplicateSlot(row); break;
                    case 3: if (onRemoveSlot)    onRemoveSlot(row);    break;
                }
            });
    }
}

void SlotListPanel::listBoxItemDoubleClicked(int row,
                                             const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    if (row >= 0 && onRenameSlot) onRenameSlot(row);
}

juce::var SlotListPanel::getDragSourceDescription(
    const juce::SparseSet<int>& rows) {
    if (rows.isEmpty()) return {};
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("source", kDragSource);
    obj->setProperty("row", rows[0]);
    return juce::var(obj.get());
}

bool SlotListPanel::isInterestedInDragSource(
    const SourceDetails& details) {
    auto* obj = details.description.getDynamicObject();
    if (!obj) return false;
    return obj->getProperty("source").toString() == kDragSource;
}

void SlotListPanel::itemDropped(const SourceDetails& details) {
    auto* obj = details.description.getDynamicObject();
    if (!obj) return;
    const int from = static_cast<int>(obj->getProperty("row"));

    // Translate drop coordinate into a row index. JUCE returns -1 if
    // the drop was below the last row -> treat as "move to end".
    const auto rel = details.localPosition;
    const int n = getNumRows();
    int to = list_.getRowContainingPosition(rel.x, rel.y);
    if (to < 0) to = n - 1;

    if (from != to && onReorder) onReorder(from, to);
}

int SlotListPanel::computeMoveButtonTarget(bool up) const {
    const int sel = list_.getSelectedRow();
    if (sel < 0 || !experiment_) return -1;
    const int n = static_cast<int>(experiment_->slots.size());
    if (up   && sel > 0)     return sel - 1;
    if (!up  && sel < n - 1) return sel + 1;
    return -1;
}
