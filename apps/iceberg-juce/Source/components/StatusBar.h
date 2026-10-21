#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

/// Bottom status bar: render counts, the selected slot's fields and a
/// message on the right. While something plays (or a trajectory renders)
/// its status line takes the place of the out / latency / cTime fields.
class StatusBar : public juce::Component {
public:
    /// The selected slot. Zero channels or a negative time shows "--".
    struct SlotFields {
        int    ir_channels  = 0;      // the IR file
        double ir_seconds   = -1.0;
        int    out_channels = 0;      // the slot's render
        double out_seconds  = -1.0;
        double out_peak     = 0.0;
        double latency_ms   = -1.0;   // the audio device's output
        double ctime_s      = -1.0;   // the render's early / late split time
    };

    StatusBar();
    ~StatusBar() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    /// `ready` = rendered + not stale; `total` = `experiment.slots.size()`;
    /// `stale` = anything flagged stale (rendered or not).
    void setRenderCounts(int ready, int total, int stale);
    void setSlotFields(const SlotFields& f);

    /// Right-hand message: setError in red, setMessage in grey.
    void setError(const juce::String& msg);
    void setMessage(const juce::String& msg);

    /// Playback (or trajectory render) status. Empty brings the slot
    /// fields back.
    void setPlaybackStatus(const juce::String& msg);

private:
    void setMessageText(const juce::String& msg, bool is_error);
    void applyColours();

    bool msg_is_error_ = true;

    juce::Label counts_label_   {"counts_label",   ""};
    juce::Label ir_label_       {"ir_label",       "IR: --"};
    juce::Label out_label_      {"out_label",      "out: --"};
    juce::Label latency_label_  {"latency_label",  "latency: --"};
    juce::Label ctime_label_    {"ctime_label",    "cTime: --"};
    juce::Label playback_label_ {"playback_label", ""};
    juce::Label msg_label_      {"msg_label",      ""};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};
