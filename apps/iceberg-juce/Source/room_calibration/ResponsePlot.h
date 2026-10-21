#pragma once

#include "calibration/measure.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace iceberg::juce_app::room_calibration {

/// Step 2's check drawn for one loudspeaker: its response in 1/3-octave
/// bands before the equalisation (grey) and measured again through it
/// (blue), 50 Hz to 20 kHz on a log axis, each about its own mean.
class ResponsePlot : public juce::Component, public juce::SettableTooltipClient {
public:
    /// `loudspeaker` is 1-based, for the legend. No check: the plot says
    /// what it will show.
    void setCheck(std::optional<iceberg::calibration::ResponseCheck> check, int loudspeaker);

    void paint(juce::Graphics& g) override;

private:
    std::optional<iceberg::calibration::ResponseCheck> check_;
    int loudspeaker_ = 0;
};

}  // namespace iceberg::juce_app::room_calibration
