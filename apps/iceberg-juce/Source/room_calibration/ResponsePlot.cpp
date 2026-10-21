#include "ResponsePlot.h"

#include "../Theme.h"

#include <algorithm>
#include <cmath>

namespace iceberg::juce_app::room_calibration {

namespace {
constexpr double kFLow = 50.0;
constexpr double kFHigh = 20000.0;
}  // namespace

void ResponsePlot::setCheck(std::optional<iceberg::calibration::ResponseCheck> check,
                            int loudspeaker) {
    check_ = std::move(check);
    loudspeaker_ = loudspeaker;
    repaint();
}

void ResponsePlot::paint(juce::Graphics& g) {
    const iceberg::juce_app::Palette& p = iceberg::juce_app::theme();
    g.fillAll(p.field);
    g.setColour(p.outline);
    g.drawRect(getLocalBounds(), 1);

    if (!check_) {
        g.setColour(p.text_faint);
        g.setFont(juce::FontOptions{12.0f});
        g.drawFittedText("Step 2 draws each loudspeaker's response here, before and after "
                         "its equalisation.",
                         getLocalBounds().reduced(12), juce::Justification::centred, 3);
        return;
    }
    const auto& c = *check_;

    // The plot area, with room for the dB labels on the left, the legend on
    // top and the frequency labels below.
    const juce::Rectangle<float> area = getLocalBounds().toFloat().withTrimmedLeft(34.0f)
                                            .withTrimmedRight(8.0f).withTrimmedTop(20.0f)
                                            .withTrimmedBottom(16.0f);

    // Symmetric dB range, a multiple of 5 that holds both curves.
    double peak = 5.0;
    for (const Eigen::VectorXd* v : {&c.before_db, &c.after_db}) {
        for (Eigen::Index i = 0; i < v->size(); ++i) {
            if (std::isfinite((*v)(i))) peak = std::max(peak, std::abs((*v)(i)));
        }
    }
    const double range = 5.0 * std::ceil(peak / 5.0);

    const auto x_of = [&](double f) {
        const double t = (std::log10(f) - std::log10(kFLow)) /
                         (std::log10(kFHigh) - std::log10(kFLow));
        return area.getX() + static_cast<float>(t) * area.getWidth();
    };
    const auto y_of = [&](double db) {
        return area.getCentreY() - static_cast<float>(db / range) * area.getHeight() * 0.5f;
    };

    // Grid: every 5 dB, 0 dB stronger; 100 Hz, 1 kHz and 10 kHz labelled.
    g.setFont(juce::FontOptions{10.0f});
    for (double db = -range; db <= range + 1e-9; db += 5.0) {
        const float y = y_of(db);
        g.setColour(db == 0.0 ? p.pad_ring : p.pad_cross);
        g.drawHorizontalLine(static_cast<int>(y), area.getX(), area.getRight());
        g.setColour(p.text_faint);
        g.drawText(juce::String(static_cast<int>(db)),
                   juce::Rectangle<float>(0.0f, y - 6.0f, 30.0f, 12.0f),
                   juce::Justification::centredRight, false);
    }
    g.setColour(p.text_faint);
    g.drawText("dB", juce::Rectangle<float>(0.0f, 3.0f, 30.0f, 12.0f),
               juce::Justification::centredRight, false);
    g.drawText("Hz", juce::Rectangle<float>(area.getRight() - 24.0f, area.getBottom() + 2.0f,
                                            24.0f, 12.0f),
               juce::Justification::centredRight, false);
    for (const double f : {100.0, 1000.0, 10000.0}) {
        const float x = x_of(f);
        g.setColour(p.pad_cross);
        g.drawVerticalLine(static_cast<int>(x), area.getY(), area.getBottom());
        g.setColour(p.text_faint);
        g.drawText(f >= 1000.0 ? juce::String(static_cast<int>(f / 1000.0)) + "k"
                               : juce::String(static_cast<int>(f)),
                   juce::Rectangle<float>(x - 20.0f, area.getBottom() + 2.0f, 40.0f, 12.0f),
                   juce::Justification::centred, false);
    }

    // One curve, broken where a band holds no line.
    const auto draw_curve = [&](const Eigen::VectorXd& db, juce::Colour colour) {
        juce::Path path;
        bool started = false;
        for (Eigen::Index i = 0; i < db.size(); ++i) {
            const double f = c.centres(i);
            if (!std::isfinite(db(i)) || f < kFLow || f > kFHigh) {
                started = false;
                continue;
            }
            const juce::Point<float> q(x_of(f), y_of(db(i)));
            if (started) path.lineTo(q);
            else         path.startNewSubPath(q);
            started = true;
        }
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(1.6f));
    };
    draw_curve(c.before_db, p.idle);
    draw_curve(c.after_db, p.ls_lit);

    // Legend: the loudspeaker and each curve's spread.
    g.setFont(juce::FontOptions{11.0f});
    juce::Rectangle<float> legend(area.getX(), 3.0f, area.getWidth(), 14.0f);
    g.setColour(p.text);
    g.drawText("Loudspeaker " + juce::String(loudspeaker_), legend.removeFromLeft(110.0f),
               juce::Justification::centredLeft, false);
    g.setColour(p.idle);
    g.drawText("before +-" + juce::String(c.before_spread_db, 1) + " dB",
               legend.removeFromLeft(120.0f), juce::Justification::centredLeft, false);
    g.setColour(p.ls_lit);
    g.drawText("after +-" + juce::String(c.after_spread_db, 1) + " dB",
               legend.removeFromLeft(120.0f), juce::Justification::centredLeft, false);
}

}  // namespace iceberg::juce_app::room_calibration
