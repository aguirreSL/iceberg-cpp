#pragma once

#include "rendering/trajectory.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <vector>

/// 2D circular pad showing the loudspeaker ring (dots on the
/// circumference) and a draggable source position. The pair of active
/// loudspeakers VBAP pans the source between is highlighted live in
/// `paint()` via `iceberg::rendering::vbap_active_pair`, the pair the
/// render and the preparation mode use.
///
/// 0 deg is at the top. Counter-clockwise (the default, Iceberg
/// v1.0.0's) draws +90 on the left; clockwise draws it on the right. The
/// angles themselves do not change, only the side they are drawn on, as
/// in MATLAB, where the convention only mirrors the IR's Y axis.
class SourcePositionPad : public juce::Component, public juce::SettableTooltipClient {
public:
    SourcePositionPad();
    ~SourcePositionPad() override = default;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    void setAngle(double angle_deg);

    /// The loudspeaker ring (`ls_array`, degrees, output channel i + 1 at
    /// entry i) and which of its entries are active (0-based indices,
    /// `active_ls_numbers - 1`). Inactive positions draw dim; active ones
    /// carry their output channel number; the VBAP pair (among the
    /// active ones) draws accented. An empty ring draws a "layout not
    /// readable" note.
    void setLayout(std::vector<double> ring_deg, std::vector<int> active_indices);

    void setClockwise(bool clockwise);

    /// The slot's programmed path, drawn as a spiral from the
    /// outside in (start to end) so its direction and turns show; empty
    /// draws none.
    void setTrajectory(std::vector<iceberg::rendering::TrajectoryPoint> points);

    /// Fired when the user drags the source dot. Argument = angle in
    /// degrees in `[0, 360)`.
    std::function<void(double)> on_angle_changed;

private:
    juce::Point<float> angleToUnit(double angle_deg) const;
    double unitToAngleDeg(juce::Point<float> v) const;
    void updateAngleFromPosition(juce::Point<float> p);

    double angle_deg_ = 0.0;
    bool clockwise_ = false;
    std::vector<double> ring_deg_;
    std::vector<int>    active_indices_;   // indices into ring_deg_
    std::vector<iceberg::rendering::TrajectoryPoint> trajectory_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourcePositionPad)
};
