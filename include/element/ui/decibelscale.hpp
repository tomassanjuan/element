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

/** Shared level metering constants and display helpers. */
namespace levelmeter {

/** RMS calibration so a full scale sine reads 0 dB, as on a digital meter. */
static constexpr float rmsCalibrationDb = 3.0103f;

/** Level at or below which a readout shows "-inf". */
static constexpr float floorDb = -60.f;

static const juce::Colour overColour (0xffe5484d);
static const juce::Colour warnColour (0xfff2a900);

/** Warning colour for a level: red above 0 dB, amber above -6 dB, otherwise @p normal. */
inline juce::Colour colourFor (float dB, juce::Colour normal)
{
    return dB > 0.f ? overColour : dB > -6.f ? warnColour
                                             : normal;
}

/** Readout text for a level in dB. */
inline juce::String textFor (float dB, int decimals = 1)
{
    return dB <= floorDb ? juce::String ("-inf") : juce::String (dB, decimals);
}

} // namespace levelmeter

} // namespace element
