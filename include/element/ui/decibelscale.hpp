// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <element/juce/gui_basics.hpp>

namespace element {
//==============================================================================
/**
    Creates a decibel scale using iec lin 2 db scale
*/
class DecibelScale : public juce::Component {
public:
    enum DecibelLevels {
        LevelOver = 0,
        Level0dB = 1,
        Level3dB = 2,
        Level6dB = 3,
        Level10dB = 4,
        LevelCount = 5
    };

    enum ColourIds {
        markerColourId = 0x11112222
    };

    /** Meter length per unit of IEC deflection. IEC deflection is 1.075 at +3 dB,
        so this puts +3 dB on the top edge of the meter and its scale. */
    static constexpr float lengthScale = 1.0f / 1.075f;

    DecibelScale();
    ~DecibelScale();

    int iecScale (const float dB) const;
    int iecLevel (const int index) const;

    /** @internal */
    void paint (juce::Graphics& g) override;
    /** @internal */
    void resized() override;

protected:
    void drawLabel (juce::Graphics& g, const int y, const juce::String& label);

private:
    juce::Font font;
    float scale;
    int lastY;
    int levels[LevelCount];
};

} // namespace element
