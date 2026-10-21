#include "Theme.h"

namespace iceberg::juce_app {

namespace {

juce::Colour rgb(int r, int g, int b) {
    return juce::Colour::fromRGB(static_cast<juce::uint8>(r), static_cast<juce::uint8>(g),
                                 static_cast<juce::uint8>(b));
}

// Dark surfaces with light text.
Palette make_dark() {
    Palette p;
    p.window         = rgb(20, 22, 28);
    p.panel          = rgb(36, 38, 44);
    p.field          = rgb(28, 30, 36);
    p.row_alt        = rgb(32, 34, 40);
    p.status         = rgb(22, 24, 30);
    p.outline        = rgb(54, 58, 66);
    p.button_off     = rgb(58, 60, 68);
    p.text           = rgb(220, 222, 226);
    p.text_dim       = rgb(180, 184, 192);
    p.text_faint     = rgb(160, 164, 172);
    p.placeholder    = rgb(110, 114, 122);
    p.message        = rgb(200, 204, 212);
    p.row_selected   = rgb(58, 78, 120);
    p.row_playing    = rgb(40, 110, 110);
    p.playing_marker = rgb(120, 230, 200);
    p.good           = rgb(96, 200, 120);
    p.warn           = rgb(220, 180, 80);
    p.busy           = rgb(240, 210, 100);
    p.idle           = rgb(120, 124, 132);
    p.error          = rgb(240, 110, 100);
    p.trajectory     = rgb(240, 180, 80);
    p.live_on        = rgb(90, 170, 120);
    p.prep_on        = rgb(220, 140, 60);
    p.on_text        = rgb(0, 0, 0);
    p.pad_ring       = rgb(60, 64, 72);
    p.pad_cross      = rgb(50, 54, 62);
    p.ls_dim         = rgb(70, 74, 82);
    p.ls             = rgb(140, 144, 152);
    p.ls_lit         = rgb(96, 168, 255);
    p.ls_halo        = juce::Colour::fromRGBA(96, 168, 255, 89);
    p.ls_number      = rgb(20, 22, 26);
    p.source         = rgb(80, 180, 255);
    p.source_ring    = rgb(255, 255, 255);
    p.pad_centre     = rgb(80, 84, 92);
    return p;
}

// Light surfaces with dark text; the state colours are darker so they
// keep their contrast on white.
Palette make_light() {
    Palette p;
    p.window         = rgb(226, 229, 234);
    p.panel          = rgb(242, 243, 246);
    p.field          = rgb(255, 255, 255);
    p.row_alt        = rgb(234, 237, 241);
    p.status         = rgb(220, 223, 228);
    p.outline        = rgb(190, 195, 203);
    p.button_off     = rgb(214, 218, 224);
    p.text           = rgb(28, 30, 36);
    p.text_dim       = rgb(74, 79, 88);
    p.text_faint     = rgb(96, 101, 110);
    p.placeholder    = rgb(150, 154, 162);
    p.message        = rgb(52, 56, 64);
    p.row_selected   = rgb(196, 212, 240);
    p.row_playing    = rgb(184, 228, 220);
    p.playing_marker = rgb(20, 140, 110);
    p.good           = rgb(40, 150, 70);
    p.warn           = rgb(196, 140, 20);
    p.busy           = rgb(210, 170, 40);
    p.idle           = rgb(140, 145, 153);
    p.error          = rgb(200, 50, 45);
    p.trajectory     = rgb(214, 130, 20);
    p.live_on        = rgb(110, 190, 140);
    p.prep_on        = rgb(236, 160, 80);
    p.on_text        = rgb(0, 0, 0);
    p.pad_ring       = rgb(176, 182, 191);
    p.pad_cross      = rgb(214, 218, 224);
    p.ls_dim         = rgb(200, 204, 211);
    p.ls             = rgb(140, 145, 153);
    p.ls_lit         = rgb(40, 110, 220);
    p.ls_halo        = juce::Colour::fromRGBA(40, 110, 220, 64);
    p.ls_number      = rgb(255, 255, 255);
    p.source         = rgb(30, 140, 235);
    p.source_ring    = rgb(20, 50, 100);
    p.pad_centre     = rgb(160, 165, 173);
    return p;
}

const Palette kDark = make_dark();
const Palette kLight = make_light();
const Palette* current = &kDark;

}  // namespace

const Palette& theme() { return *current; }

void apply_theme(bool dark, juce::LookAndFeel_V4& lnf) {
    current = dark ? &kDark : &kLight;
    lnf.setColourScheme(dark ? juce::LookAndFeel_V4::getDarkColourScheme()
                             : juce::LookAndFeel_V4::getLightColourScheme());
}

}  // namespace iceberg::juce_app
