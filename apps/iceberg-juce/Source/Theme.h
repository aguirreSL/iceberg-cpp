#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace iceberg::juce_app {

/// The app's colours by role, one palette per theme. Components read
/// theme() when they paint, and re-apply the colours they set on widgets in
/// lookAndFeelChanged(), which a theme switch triggers on every window.
struct Palette {
    // Surfaces.
    juce::Colour window;        // behind everything
    juce::Colour panel;         // the list and the slot panel
    juce::Colour field;         // text fields, file labels, the pad
    juce::Colour row_alt;       // every other row of the list
    juce::Colour status;        // the status bar
    juce::Colour outline;
    juce::Colour button_off;    // Live / Prep mode when off
    // Text.
    juce::Colour text;
    juce::Colour text_dim;      // captions, fields of the status bar
    juce::Colour text_faint;    // the angle column, tick labels
    juce::Colour placeholder;
    juce::Colour message;       // status messages that are not errors
    // List rows.
    juce::Colour row_selected;
    juce::Colour row_playing;
    juce::Colour playing_marker;
    // States.
    juce::Colour good;          // rendered, calibration checks passed
    juce::Colour warn;          // stale, calibration warnings
    juce::Colour busy;          // rendering
    juce::Colour idle;          // never rendered, queued
    juce::Colour error;
    juce::Colour trajectory;    // the amber of a programmed path
    juce::Colour live_on;       // Live (ON)
    juce::Colour prep_on;       // Prep mode (ON)
    juce::Colour on_text;       // text on live_on / prep_on
    // The position pad.
    juce::Colour pad_ring;
    juce::Colour pad_cross;
    juce::Colour ls_dim;        // loudspeakers of a layout file that are off
    juce::Colour ls;
    juce::Colour ls_lit;        // the ones that carry the source
    juce::Colour ls_halo;
    juce::Colour ls_number;
    juce::Colour source;
    juce::Colour source_ring;
    juce::Colour pad_centre;
};

/// Where the theme comes from: the operating system (the default, followed
/// when it changes) or a fixed choice.
enum class Appearance { System, Light, Dark };

const Palette& theme();

/// Selects the palette and gives `lnf` the matching JUCE colour scheme.
/// Windows then need sendLookAndFeelChange() to repaint in it.
void apply_theme(bool dark, juce::LookAndFeel_V4& lnf);

}  // namespace iceberg::juce_app
