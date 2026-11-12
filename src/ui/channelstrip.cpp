// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <element/ui/style.hpp>
#include "ui/channelstrip.hpp"

namespace element {

struct ChannelStripComponent::FaderStyle : public LookAndFeel_E1
{
    void drawLinearSlider (Graphics& g,
                           int x,
                           int y,
                           int width,
                           int height,
                           float sliderPos,
                           float minSliderPos,
                           float maxSliderPos,
                           const Slider::SliderStyle sliderStyle,
                           Slider& slider) override
    {
        // clang-format off
        Style::drawFader (g, x, y, width, height, 
                                    sliderPos, minSliderPos, maxSliderPos,
                                    sliderStyle, slider);
        // clang-format on
    }
};

ChannelStripComponent::VolumeLabel::VolumeLabel() {}
ChannelStripComponent::VolumeLabel::~VolumeLabel() {}

void ChannelStripComponent::VolumeLabel::settingLabelDoubleClicked()
{
    if (auto* const strip = findParentComponentOfClass<ChannelStripComponent>())
        strip->volumeLabelDoubleClicked();
}

ChannelStripComponent::ChannelStripComponent()
    : meter (2, false)
{
    _fstyle = std::make_unique<FaderStyle>();

    addAndMakeVisible (fader);
    fader.setSliderStyle (Slider::LinearVertical);
    fader.setTextBoxStyle (Slider::NoTextBox, true, 1, 1);
    fader.setRange (-60.0, 6.0, 0.001);
    fader.setValue (0.f, dontSendNotification);
    fader.setSkewFactor (2);
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.addListener (this);
    fader.setColour (Slider::trackColourId, Colours::black);
    fader.setColour (Slider::thumbColourId, Colours::black.brighter (0.2f));
    fader.setLookAndFeel (_fstyle.get());

    addAndMakeVisible (meter, 100);
    addAndMakeVisible (scale, 101);

    addAndMakeVisible (powerButton);
    powerButton.setColour (SettingButton::backgroundOnColourId, Colors::toggleBlue);
    powerButton.setButtonText ("M");
    powerButton.addListener (this);

    addAndMakeVisible (clipLight);
    clipLight.onClick = [this]() { resetClip(); };

    addAndMakeVisible (levelLabel);
    levelLabel.setFont (juce::FontOptions (11.f, juce::Font::bold));
    levelLabel.setJustificationType (Justification::centred);
    levelLabel.setTooltip ("Meter level (dB)");
    levelLabel.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (muteButton);
    muteButton.setYesNoText ("M", "M");
    muteButton.setButtonText ("M");
    muteButton.setColour (SettingButton::backgroundOnColourId, Colors::toggleRed);
    muteButton.setColour (SettingButton::textColourId, Colours::black);
    muteButton.addListener (this);

    addAndMakeVisible (volume);
    volume.setFontSize (Style::fontSizeLarge);
    volume.setNumDecimalPlaces (1);
    volume.setMinMax (fader.getMinimum(), fader.getMaximum());
    volume.setValue (fader.getValue());
    volume.setTextWhenMinimum ("-inf");
    volume.getValueObject().addListener (this);

    stabilizeContent();
}

ChannelStripComponent::~ChannelStripComponent() noexcept
{
    fader.setLookAndFeel (nullptr);
    fader.removeListener (this);
    volume.getValueObject().removeListener (this);
    _fstyle.reset();
}

void ChannelStripComponent::setMinMaxDecibels (double minDb, double maxDb)
{
    jassert (maxDb > minDb);
    fader.setRange (minDb, maxDb, 0.001);
    volume.setMinMax (fader.getMinimum(), fader.getMaximum());
    volume.setValue (fader.getValue());
}

void ChannelStripComponent::addButton (Component* btn)
{
    jassert (! extraButtons.contains (btn));
    if (! extraButtons.contains (btn))
        addAndMakeVisible (extraButtons.add (btn));
    resized();
}

void ChannelStripComponent::resized()
{
    auto r1 = getLocalBounds().reduced (2);
    // with more than two channels the left column and fader keep their stereo width and the meter takes the rest
    const bool wideMeter = meter.getPortCount() > 2;
    auto r2 = r1.removeFromRight (wideMeter ? jmax (0, r1.getWidth() - stereoLeftWidth) : r1.getWidth() / 2);

    gainCaption = r1.removeFromTop (11);
    volume.setBounds (r1.removeFromTop (18).withSizeKeepingCentre (40, 18));
    levelLabel.setBounds (r1.removeFromTop (14).withSizeKeepingCentre (40, 14));
    levelCaption = r1.removeFromTop (10);
    r1.removeFromBottom (4);

    for (auto* const button : extraButtons)
    {
        button->setBounds (r1.removeFromBottom (18).withSizeKeepingCentre (26, 18));
        r1.removeFromBottom (1);
    }

    powerButton.setBounds (r1.removeFromBottom (18).withSizeKeepingCentre (26, 18));

    if (muteButton.isVisible())
    {
        r1.removeFromBottom (1);
        muteButton.setBounds (r1.removeFromBottom (18).withSizeKeepingCentre (26, 18));
    }

    const int quarter = wideMeter ? stereoFaderWidth : r2.getWidth() / 2;
    fader.setBounds (r2.removeFromRight (quarter));
    auto r3 = wideMeter ? r2 : r2.removeFromRight (quarter);
    r3.removeFromTop (4);
    r3.removeFromBottom (4);
    clipLight.setBounds (r3.removeFromTop (6));
    r3.removeFromTop (1);
    meter.setBounds (r3);
    scale.setBounds (meter.getBoundsInParent());
}

void ChannelStripComponent::buttonClicked (Button* b)
{
    if (b == &powerButton)
    {
        powerButton.setToggleState (! powerButton.getToggleState(), juce::dontSendNotification);
        powerChanged();
    }
    else if (b == &muteButton)
    {
        muteButton.setToggleState (! muteButton.getToggleState(), juce::dontSendNotification);
        muteChanged();
    }
}

void ChannelStripComponent::sliderValueChanged (Slider* slider)
{
    volumeChanged (slider->getValue());
    stabilizeContent();
}

void ChannelStripComponent::valueChanged (Value& value)
{
    fader.setValue ((double) value.getValue(), sendNotificationAsync);
}

void ChannelStripComponent::paint (Graphics& g)
{
    g.setFont (FontOptions (9.f));
    g.setColour (Colors::textColor.withAlpha (0.6f));
    g.drawText ("Gain dB", gainCaption, Justification::centred, false);
    g.drawText ("RMS dB", levelCaption, Justification::centred, false);
}

void ChannelStripComponent::stabilizeContent()
{
    volume.getValueObject().removeListener (this);
    volume.setValue (fader.getValue());
    volume.getValueObject().addListener (this);
}

} // namespace element
