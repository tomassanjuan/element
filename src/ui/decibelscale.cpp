// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <element/ui/decibelscale.hpp>
#include <element/ui/style.hpp>

using namespace juce;

namespace element {

DecibelScale::DecibelScale()
    : font (FontOptions (Style::fontSizeSmall)), scale (0.0f), lastY (0)
{
    zeromem (levels, sizeof (int) * LevelCount);
    setColour (markerColourId, Colors::textColor);
}

DecibelScale::~DecibelScale()
{
}

void DecibelScale::paint (Graphics& g)
{
    g.setFont (font);
    g.setColour (findColour (markerColourId));

    lastY = 0;

    // scale marks, positioned on the IEC curve
    for (const int dB : { 0, -6, -12, -24, -48, -60 })
        drawLabel (g, iecScale ((float) dB), String (dB));
}

void DecibelScale::resized()
{
    scale = 0.85f * getHeight();

    levels[Level0dB] = iecScale (0.0f);
    levels[Level3dB] = iecScale (-3.0f);
    levels[Level6dB] = iecScale (-6.0f);
    levels[Level10dB] = iecScale (-10.0f);
}

void DecibelScale::drawLabel (Graphics& g, const int y, const String& label)
{
    const int iCurrY = getHeight() - y;
    const int iWidth = getWidth();
    const int iMidHeight = (int) (font.getHeight() * 0.5f);

    if (iCurrY < iMidHeight || iCurrY > lastY + iMidHeight)
    {
        // one tick column against the meter, labels right-aligned to it so the digits line up
        g.drawLine ((float) iWidth - 4.f, (float) iCurrY, (float) iWidth, (float) iCurrY);
        g.drawText (label,
                    0,
                    iCurrY - iMidHeight,
                    iWidth - 6,
                    (int) font.getHeight(),
                    Justification::centredRight,
                    false);

        lastY = iCurrY + 1;
    }
}

int DecibelScale::iecScale (const float dB) const
{
    float fDef = 1.0;

    if (dB < -70.0)
        fDef = 0.0;
    else if (dB < -60.0)
        fDef = (dB + 70.0) * 0.0025;
    else if (dB < -50.0)
        fDef = (dB + 60.0) * 0.005 + 0.025;
    else if (dB < -40.0)
        fDef = (dB + 50.0) * 0.0075 + 0.075;
    else if (dB < -30.0)
        fDef = (dB + 40.0) * 0.015 + 0.15;
    else if (dB < -20.0)
        fDef = (dB + 30.0) * 0.02 + 0.3;
    else // if (dB < 0.0)
        fDef = (dB + 20.0) * 0.025 + 0.5;

    return (int) (fDef * scale);
}

int DecibelScale::iecLevel (const int index) const
{
    return levels[index];
}

} // namespace element
