#pragma once

#include "experiment/experiment.h"
#include "render/RenderOrchestrator.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

/// Left-hand stimuli list. Holds a non-owning pointer to the
/// MainComponent's `Experiment` and surfaces selection / edit
/// callbacks. ASCII-pure: status badges drawn via `juce::Path`
/// shapes, no Unicode glyphs in strings.
///
/// Buttons + drag-drop reorder: `onAddSlot` / `onRemoveSlot` /
/// `onDuplicateSlot` / `onReorder` fire into MainComponent which owns
/// the `Experiment` mutation.
class SlotListPanel : public juce::Component,
                      public juce::DragAndDropContainer,
                      public juce::ListBoxModel,
                      public juce::DragAndDropTarget {
public:
    SlotListPanel();
    ~SlotListPanel() override = default;

    void setExperiment(iceberg::experiment::Experiment* exp);
    void setOrchestrator(iceberg::juce_app::RenderOrchestrator* orch);
    void setSelectedIndex(int idx);
    int  getSelectedIndex() const;
    /// Mark which slot is currently being played by `AudioPlayer`.
    /// Pass `-1` to clear. Triggers a repaint.
    void setCurrentlyPlayingIndex(int idx);
    void refresh();

    std::function<void(int)>  onSelectionChanged;
    std::function<void()>     onAddSlot;
    std::function<void(int)>  onRemoveSlot;
    std::function<void(int)>  onDuplicateSlot;
    std::function<void(int /*from*/, int /*to*/)> onReorder;
    /// Renaming. Fires when the
    /// user double-clicks a row or picks Rename from the right-click
    /// menu. MainComponent owns the modal dialog + slot mutation.
    std::function<void(int /*idx*/)> onRenameSlot;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    // ListBoxModel
    int  getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g,
                          int width, int height, bool selected) override;
    juce::String getTooltipForRow(int row) override;
    void selectedRowsChanged(int last_row) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription(
        const juce::SparseSet<int>& rows) override;

    // DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

private:
    void layoutButtons(juce::Rectangle<int> area);
    void applyColours();
    int  computeMoveButtonTarget(bool up) const;

    iceberg::experiment::Experiment*      experiment_  = nullptr;
    iceberg::juce_app::RenderOrchestrator* orchestrator_ = nullptr;
    int                                    playing_idx_  = -1;

    juce::ListBox    list_  {"slot_list", this};
    juce::Label      header_{"hdr", "Stimuli (0)"};
    juce::TextButton add_btn_       {"+ Add"};
    juce::TextButton remove_btn_    {"- Remove"};
    juce::TextButton duplicate_btn_ {"Duplicate"};
    juce::TextButton up_btn_        {"Up"};
    juce::TextButton down_btn_      {"Down"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlotListPanel)
};
