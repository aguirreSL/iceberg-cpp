#include "StatusBar.h"

#include "Theme.h"

StatusBar::StatusBar() {
    for (auto* l : {&counts_label_, &ir_label_, &out_label_, &latency_label_,
                    &ctime_label_, &playback_label_}) {
        l->setFont(juce::FontOptions{12.0f});
        addAndMakeVisible(*l);
    }
    playback_label_.setVisible(false);
    msg_label_.setFont(juce::FontOptions{12.0f, juce::Font::bold});
    msg_label_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(msg_label_);
    applyColours();
}

void StatusBar::lookAndFeelChanged() {
    applyColours();
}

void StatusBar::applyColours() {
    const iceberg::juce_app::Palette& p = iceberg::juce_app::theme();
    for (auto* l : {&counts_label_, &ir_label_, &out_label_, &latency_label_,
                    &ctime_label_, &playback_label_}) {
        l->setColour(juce::Label::textColourId, p.text_dim);
    }
    msg_label_.setColour(juce::Label::textColourId, msg_is_error_ ? p.error : p.message);
    repaint();
}

void StatusBar::paint(juce::Graphics& g) {
    g.fillAll(iceberg::juce_app::theme().status);
    g.setColour(iceberg::juce_app::theme().outline);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
}

void StatusBar::resized() {
    auto area = getLocalBounds().reduced(8, 4);
    counts_label_.setBounds(area.removeFromLeft(120));
    ir_label_    .setBounds(area.removeFromLeft(115));
    // The playback line spans the three fields it replaces.
    auto fields = area.removeFromLeft(190 + 105 + 100);
    playback_label_.setBounds(fields);
    out_label_    .setBounds(fields.removeFromLeft(190));
    latency_label_.setBounds(fields.removeFromLeft(105));
    ctime_label_  .setBounds(fields);
    msg_label_    .setBounds(area);
}

void StatusBar::setRenderCounts(int ready, int total, int stale) {
    counts_label_.setText(juce::String(ready) + "/" + juce::String(total) + " ready, " +
                              juce::String(stale) + " stale",
                          juce::dontSendNotification);
}

void StatusBar::setSlotFields(const SlotFields& f) {
    const juce::String ir = f.ir_channels > 0 && f.ir_seconds >= 0.0
        ? juce::String(f.ir_channels) + " ch, " + juce::String(f.ir_seconds, 3) + " s"
        : juce::String("--");
    const juce::String out = f.out_channels > 0 && f.out_seconds >= 0.0
        ? juce::String(f.out_channels) + " ch, " + juce::String(f.out_seconds, 2) +
              " s, peak " + juce::String(f.out_peak, 3)
        : juce::String("--");
    ir_label_.setText("IR: " + ir, juce::dontSendNotification);
    out_label_.setText("out: " + out, juce::dontSendNotification);
    latency_label_.setText(
        "latency: " + (f.latency_ms >= 0.0 ? juce::String(f.latency_ms, 1) + " ms"
                                           : juce::String("--")),
        juce::dontSendNotification);
    ctime_label_.setText(
        "cTime: " + (f.ctime_s >= 0.0 ? juce::String(f.ctime_s * 1000.0, 1) + " ms"
                                      : juce::String("--")),
        juce::dontSendNotification);
}

void StatusBar::setError(const juce::String& msg) {
    setMessageText(msg, true);
}

void StatusBar::setMessage(const juce::String& msg) {
    setMessageText(msg, false);
}

void StatusBar::setMessageText(const juce::String& msg, bool is_error) {
    msg_is_error_ = is_error;
    const iceberg::juce_app::Palette& p = iceberg::juce_app::theme();
    msg_label_.setColour(juce::Label::textColourId, is_error ? p.error : p.message);
    msg_label_.setText(msg, juce::dontSendNotification);
}

void StatusBar::setPlaybackStatus(const juce::String& msg) {
    const bool show = msg.isNotEmpty();
    playback_label_.setText(msg, juce::dontSendNotification);
    playback_label_.setVisible(show);
    out_label_.setVisible(!show);
    latency_label_.setVisible(!show);
    ctime_label_.setVisible(!show);
}
