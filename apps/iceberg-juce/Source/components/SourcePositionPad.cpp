#include "SourcePositionPad.h"

#include "Theme.h"

// Design exception: the pair highlight is geometric
// feedback (which two LS would carry the source if rendered now), not
// a DSP call. `vbap_active_pair` is pure and cheap (the pair search of
// vbap_2d over a handful of azimuths). Calling it from paint() is OK;
// calibrate_*, vbap_render, etc. are NOT.
#include "rendering/vbap_active_pair.h"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

// Geometry, in pixels. An active loudspeaker is a disc with its output
// channel number inside; the source dot sits far enough in to clear it,
// and the angle labels sit outside the pair halo.
constexpr float kMargin       = 34.0f;   // ring to component edge
constexpr float kActiveRadius = 8.0f;
constexpr float kHaloRadius   = 13.0f;
constexpr float kSourceRadius = 9.0f;
constexpr float kSourceRing   = 0.72f;   // source dot radius / ring radius
constexpr float kLabelGap     = 15.0f;   // ring to the angle label's near edge
constexpr float kLabelHalfW   = 10.0f;
constexpr float kLabelHalfH   = 6.0f;
}  // namespace

// A polar angle in degrees to a unit vector in screen space (positive y
// is down, so 0 deg, the front, is -y). Counter-clockwise puts +90 at -x
// (left), clockwise at +x (right).
juce::Point<float> SourcePositionPad::angleToUnit(double angle_deg) const {
    const double a = angle_deg * kPi / 180.0;
    const double x = clockwise_ ? std::sin(a) : -std::sin(a);
    return {static_cast<float>(x), static_cast<float>(-std::cos(a))};
}

double SourcePositionPad::unitToAngleDeg(juce::Point<float> v) const {
    const double x = clockwise_ ? v.x : -v.x;
    const double a = std::atan2(x, -v.y) * 180.0 / kPi;
    return a < 0.0 ? a + 360.0 : a;
}

SourcePositionPad::SourcePositionPad() {
    setOpaque(true);
    // The 4 default loudspeakers, until a slot is bound.
    ring_deg_ = {0.0, 90.0, 180.0, 270.0};
    active_indices_ = {0, 1, 2, 3};
}

void SourcePositionPad::paint(juce::Graphics& g) {
    const iceberg::juce_app::Palette& pal = iceberg::juce_app::theme();
    g.fillAll(pal.field);

    const auto bounds = getLocalBounds().toFloat().reduced(kMargin);
    const auto centre = bounds.getCentre();
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;

    // Outer ring guide.
    g.setColour(pal.pad_ring);
    g.drawEllipse(centre.x - radius, centre.y - radius,
                  radius * 2.0f, radius * 2.0f, 1.5f);

    // Cardinal-tick guides at 0/90/180/270, labelled outside the ring
    // (clear of a lit loudspeaker's halo) so the drawing direction reads
    // at a glance.
    g.setFont(juce::FontOptions{10.0f});
    for (const double t : {0.0, 90.0, 180.0, 270.0}) {
        const auto u = angleToUnit(t);
        const auto p = centre + u * radius;
        g.setColour(pal.pad_cross);
        g.drawLine(centre.x, centre.y, p.x, p.y, 0.5f);
        const float out = radius + kLabelGap +
                          std::abs(u.x) * kLabelHalfW + std::abs(u.y) * kLabelHalfH;
        const auto lp = centre + u * out;
        g.setColour(pal.text_faint);
        g.drawText(juce::String(static_cast<int>(t)),
                   juce::Rectangle<float>(lp.x - kLabelHalfW, lp.y - kLabelHalfH,
                                          2.0f * kLabelHalfW, 2.0f * kLabelHalfH),
                   juce::Justification::centred, false);
    }

    // The VBAP pair among the active loudspeakers, as ring indices.
    int s1_ring = -1;
    int s2_ring = -1;
    if (active_indices_.size() >= 2) {
        Eigen::MatrixXd ls_dir = Eigen::MatrixXd::Zero(
            static_cast<Eigen::Index>(active_indices_.size()), 2);
        bool valid = true;
        for (size_t i = 0; i < active_indices_.size(); ++i) {
            const int r = active_indices_[i];
            if (r < 0 || r >= static_cast<int>(ring_deg_.size())) { valid = false; break; }
            ls_dir(static_cast<Eigen::Index>(i), 0) = ring_deg_[static_cast<size_t>(r)];
        }
        if (valid) {
            try {
                const auto pair = iceberg::rendering::vbap_active_pair(angle_deg_, ls_dir);
                // A source on a loudspeaker gets (1, 0): only the
                // loudspeaker carrying energy gets the halo.
                constexpr double kGainEpsilon = 1e-6;
                if (pair.g1_norm > kGainEpsilon)
                    s1_ring = active_indices_[static_cast<size_t>(pair.s1_idx)];
                if (pair.g2_norm > kGainEpsilon)
                    s2_ring = active_indices_[static_cast<size_t>(pair.s2_idx)];
            } catch (...) {
                // Degenerate layout: draw it without a pair.
            }
        }
    }

    // Loudspeaker dots: inactive ring positions dim and small; active
    // ones a grey disc with their output channel number inside; the pair
    // accented with a halo.
    g.setFont(juce::FontOptions{10.0f, juce::Font::bold});
    for (size_t i = 0; i < ring_deg_.size(); ++i) {
        const auto u = angleToUnit(ring_deg_[i]);
        const auto p = centre + u * radius;
        const int ring_index = static_cast<int>(i);
        const bool active = std::find(active_indices_.begin(), active_indices_.end(),
                                      ring_index) != active_indices_.end();
        if (!active) {
            g.setColour(pal.ls_dim);
            g.fillEllipse(p.x - 3.5f, p.y - 3.5f, 7.0f, 7.0f);
            continue;
        }
        if (ring_index == s1_ring || ring_index == s2_ring) {
            g.setColour(pal.ls_halo);
            g.fillEllipse(p.x - kHaloRadius, p.y - kHaloRadius,
                          2.0f * kHaloRadius, 2.0f * kHaloRadius);
            g.setColour(pal.ls_lit);
        } else {
            g.setColour(pal.ls);
        }
        const juce::Rectangle<float> disc(p.x - kActiveRadius, p.y - kActiveRadius,
                                          2.0f * kActiveRadius, 2.0f * kActiveRadius);
        g.fillEllipse(disc);
        g.setColour(pal.ls_number);
        g.drawText(juce::String(ring_index + 1), disc,
                   juce::Justification::centred, false);
    }

    if (ring_deg_.empty()) {
        g.setColour(pal.text_faint);
        g.setFont(juce::FontOptions{12.0f});
        g.drawFittedText("(layout not readable)",
                         getLocalBounds().reduced(10),
                         juce::Justification::centredBottom, 1);
    }

    // The programmed path: a spiral from 0.66 to 0.38 of the ring radius,
    // start to end, so the turns and the direction read; the typed points
    // marked, the end a filled dot.
    if (!trajectory_.empty()) {
        const double t0 = trajectory_.front().time_s;
        const double t1 = trajectory_.back().time_s;
        const auto at = [&](double t) {
            const double frac = t1 > t0 ? (t - t0) / (t1 - t0) : 0.0;
            const float r = radius * static_cast<float>(0.66 - 0.28 * frac);
            return centre + angleToUnit(iceberg::rendering::trajectory_angle(trajectory_, t)) * r;
        };
        const juce::Colour amber = pal.trajectory;
        if (trajectory_.size() > 1) {
            juce::Path path;
            constexpr int kSteps = 240;
            path.startNewSubPath(at(t0));
            for (int i = 1; i <= kSteps; ++i) path.lineTo(at(t0 + (t1 - t0) * i / kSteps));
            g.setColour(amber.withAlpha(0.8f));
            g.strokePath(path, juce::PathStrokeType(1.6f));
            for (const auto& p : trajectory_) {
                const auto q = at(p.time_s);
                g.drawEllipse(q.x - 3.0f, q.y - 3.0f, 6.0f, 6.0f, 1.2f);
            }
        }
        const auto end = at(t1);
        g.setColour(amber);
        g.fillEllipse(end.x - 4.0f, end.y - 4.0f, 8.0f, 8.0f);
    }

    // Source dot.
    const auto su = angleToUnit(angle_deg_);
    const auto sp = centre + su * (radius * kSourceRing);
    const juce::Rectangle<float> source(sp.x - kSourceRadius, sp.y - kSourceRadius,
                                        2.0f * kSourceRadius, 2.0f * kSourceRadius);
    g.setColour(pal.source);
    g.fillEllipse(source);
    g.setColour(pal.source_ring);
    g.drawEllipse(source, 1.5f);

    // Centre crosshair.
    g.setColour(pal.pad_centre);
    g.fillEllipse(centre.x - 2.0f, centre.y - 2.0f, 4.0f, 4.0f);
}

void SourcePositionPad::mouseDown(const juce::MouseEvent& e) {
    updateAngleFromPosition(e.position);
}

void SourcePositionPad::mouseDrag(const juce::MouseEvent& e) {
    updateAngleFromPosition(e.position);
}

void SourcePositionPad::updateAngleFromPosition(juce::Point<float> p) {
    const auto centre = getLocalBounds().toFloat().getCentre();
    const auto delta = p - centre;
    if (delta.getDistanceFromOrigin() < 1.0f) return;
    setAngle(unitToAngleDeg(delta));
    if (on_angle_changed) on_angle_changed(angle_deg_);
}

void SourcePositionPad::setAngle(double angle_deg) {
    double a = std::fmod(angle_deg, 360.0);
    if (a < 0.0) a += 360.0;
    if (std::abs(a - angle_deg_) < 1e-9) return;
    angle_deg_ = a;
    repaint();
}

void SourcePositionPad::setLayout(std::vector<double> ring_deg,
                                  std::vector<int> active_indices) {
    ring_deg_ = std::move(ring_deg);
    active_indices_ = std::move(active_indices);
    repaint();
}

void SourcePositionPad::setTrajectory(std::vector<iceberg::rendering::TrajectoryPoint> points) {
    trajectory_ = std::move(points);
    repaint();
}

void SourcePositionPad::setClockwise(bool clockwise) {
    if (clockwise_ == clockwise) return;
    clockwise_ = clockwise;
    repaint();
}
