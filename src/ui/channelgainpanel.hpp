// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <element/juce/gui_basics.hpp>
#include <element/node.hpp>
#include <element/processor.hpp>
#include <element/tags.hpp>
#include <element/ui/simplemeter.hpp>

namespace element {

/** A fader and meter for every audio channel of a node.

    The faders trim the channels entering the node, or leaving it for nodes
    without audio inputs (e.g. the graph's audio input). Values are kept in the
    node's model so they are saved with the session.
*/
class ChannelGainPanel : public juce::Component,
                         private juce::Timer
{
public:
    static constexpr double minDb = -60.0;
    static constexpr double maxDb = 12.0;

    explicit ChannelGainPanel (const Node& n)
        : node (n)
    {
        if (ProcessorPtr p = node.getObject())
        {
            sourceNode = p->getNumAudioInputs() == 0;
            numChannels = juce::jmin ((int) Processor::maxChannelGains,
                                      sourceNode ? p->getNumAudioOutputs() : p->getNumAudioInputs());
        }

        for (int c = 0; c < numChannels; ++c)
        {
            auto* s = strips.add (new Strip());
            s->name.setText (juce::String (c + 1), juce::dontSendNotification);
            s->name.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (s->name);

            s->slider.setSliderStyle (juce::Slider::LinearVertical);
            s->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 40, 16);
            s->slider.setRange (minDb, maxDb, 0.1);
            s->slider.setSkewFactorFromMidPoint (-12.0);
            s->slider.setDoubleClickReturnValue (true, 0.0);
            s->slider.setValue (currentDb (c), juce::dontSendNotification);
            s->slider.onValueChange = [this, c] { apply (c); };
            addAndMakeVisible (s->slider);
            addAndMakeVisible (s->meter);
        }

        resetButton.onClick = [this] {
            for (auto* s : strips)
                s->slider.setValue (0.0);
        };
        addAndMakeVisible (resetButton);

        setSize (juce::jmax (3, numChannels) * columnWidth + 8, 250);
        startTimerHz (15);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        resetButton.setBounds (r.removeFromBottom (24).removeFromRight (70));
        r.removeFromBottom (4);

        for (auto* s : strips)
        {
            auto col = r.removeFromLeft (columnWidth);
            s->name.setBounds (col.removeFromTop (18));
            s->meter.setBounds (col.removeFromRight (8).withTrimmedBottom (16));
            s->slider.setBounds (col);
        }
    }

private:
    struct Strip
    {
        juce::Label name;
        juce::Slider slider;
        SimpleMeter meter { 1, false };
    };

    static constexpr int columnWidth = 52;

    Node node;
    bool sourceNode = false;
    int numChannels = 0;
    juce::OwnedArray<Strip> strips;
    juce::TextButton resetButton { "Reset" };

    double currentDb (int channel) const
    {
        if (ProcessorPtr p = node.getObject())
            return juce::jmax (minDb, (double) juce::Decibels::gainToDecibels (p->getChannelInputGain (channel), (float) minDb));
        return 0.0;
    }

    void apply (int channel)
    {
        ProcessorPtr p = node.getObject();
        if (p == nullptr)
            return;

        const double db = strips[channel]->slider.getValue();
        p->setChannelInputGain (channel, db <= minDb ? 0.0f : juce::Decibels::decibelsToGain ((float) db));

        juce::StringArray dbs;
        for (auto* s : strips)
        {
            const double v = s->slider.getValue();
            dbs.add (v <= minDb ? "-100" : juce::String (v, 1));
        }
        node.setProperty (tags::channelGains, dbs.joinIntoString (","));
    }

    void timerCallback() override
    {
        ProcessorPtr p = node.getObject();
        if (p == nullptr)
            return;

        for (int c = 0; c < strips.size(); ++c)
        {
            // both readings are taken after the trim, so the meter moves with the fader
            strips[c]->meter.setValue (0, sourceNode ? p->getOutputRMS (c) : p->getInputRMS (c));
            strips[c]->meter.refresh();
        }
    }
};

} // namespace element
