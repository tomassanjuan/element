// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <element/ui/meterbridge.hpp>
#include <element/services.hpp>
#include <element/context.hpp>
#include <element/devices.hpp>

#include <element/ui/style.hpp>
#include <element/ui/simplemeter.hpp>
#include <element/ui/cliphold.hpp>
#include <element/ui/decibelscale.hpp>

using namespace juce;

namespace element {

struct SimpleLevelMeter : public Component,
                          public Timer
{
    SimpleLevelMeter() = delete;
    SimpleLevelMeter (AudioEnginePtr e, int channel, bool input)
    {
        setOpaque (false);
        startTimerHz (20);
        meter = e->getLevelMeter (channel, input);
    }

    void timerCallback() override
    {
        if (isShowing())
        {
            auto newLevel = (float) meter->level();

            if (std::abs (level - newLevel) > 0.005f)
            {
                level = newLevel;
                repaint();
            }
        }
        else
        {
            level = 0;
        }
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colours::black.withAlpha (0.2f));
        // (add a bit of a skew to make the level more obvious)
        drawLevelMeter (g, getWidth(), getHeight(), (float) std::exp (std::log (level * 0.75) / 3.f), true);
    }

    void drawLevelMeter (Graphics& g, int width, int height, float level, bool vertical)
    {
        // level = 1.f;
        float corner = 3.f;
        // g.setColour (Colours::black); //Colours::white.withAlpha (0.7f));
        // g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, corner);
        g.setColour (Colours::black.withAlpha (0.2f));
        g.drawRoundedRectangle (1.0f, 1.0f, (float) width - 2.0f, (float) height - 2.0f, corner, 1.0f);

        const int numBlocks = roundToInt (totalBlocks * level);
        auto sz = vertical ? ((float) height - corner * 2.f) / (float) totalBlocks
                           : ((float) width - corner * 2.f) / (float) totalBlocks;

        int i2 = totalBlocks - 1;
        for (int i = 0; i < totalBlocks; ++i)
        {
            if (vertical)
            {
                if (i >= numBlocks)
                {
                    g.setColour (Colours::black.withAlpha (0.6f));
                }
                else if (i < totalBlocks - 2)
                {
                    g.setColour (Colours::green.withAlpha (0.8f));
                }
                else if (i < totalBlocks - 1)
                {
                    g.setColour (Colours::green.withAlpha (0.7f));
                }
                else
                {
                    g.setColour (Colours::orange.withAlpha (0.8f));
                }
                g.fillRoundedRectangle (corner,
                                        corner + (float) i2 * sz + sz * 0.1f,
                                        (float) width - corner * 2,
                                        (float) sz * 0.8f,
                                        1.f);
                --i2;
            }
            else
            {
                if (i >= numBlocks)
                    g.setColour (Colours::lightblue.withAlpha (0.6f));
                else
                    g.setColour (i < totalBlocks - 1 ? Colours::blue.withAlpha (0.5f)
                                                     : Colours::red);

                g.fillRoundedRectangle (corner + (float) i * sz + sz * 0.1f,
                                        corner,
                                        (float) sz * 0.8f,
                                        (float) height - corner * 2,
                                        1.f);
            }
        }
    }

    AudioEngine::LevelMeterPtr meter;
    ;
    float level = 0;
    int totalBlocks = 7;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleLevelMeter)
};

/** Bridge meter drawn like the channel strip VU meters (IEC dB scale). */
struct VuLevelMeter : public Component,
                      public Timer
{
    /** Height of the level readout box above the meter. It turns red on clipping. */
    static constexpr int readoutHeight = 16;

    VuLevelMeter (AudioEnginePtr e, int channel, bool input)
        : meter (1, false)
    {
        levelMeter = e->getLevelMeter (channel, input);
        addAndMakeVisible (meter);
        // clicks go to this component so they clear the clip light and the held peak
        meter.setInterceptsMouseClicks (false, false);
        startTimerHz (20);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (readoutHeight + 2);
        meter.setBounds (r);
    }

    void paint (Graphics& g) override
    {
        const auto box = getLocalBounds().removeFromTop (readoutHeight);
        g.setColour (clip.isClipped() ? Colours::red : Colour (0xff101010));
        g.fillRect (box);

        g.setColour (clip.isClipped() ? Colours::white
                     : levelmeter::colourFor (levelDb, Colours::white));
        float size = 11.f;
        Font font (FontOptions (size, Font::bold));
        while (size > 6.f && GlyphArrangement::getStringWidthInt (font, levelText) > box.getWidth() - 4)
            font = Font (FontOptions (size -= 0.5f, Font::bold));

        g.setFont (font);
        g.drawText (levelText, box, Justification::centred, false);
    }

    void mouseDown (const MouseEvent&) override
    {
        clip.clear();
        meter.resetPeaks();
        repaint();
    }

    void timerCallback() override
    {
        if (! isShowing())
            return;

        const float raw = (float) levelMeter->level();
        const bool rmsMode = MeterBridge::getLevelMode() == MeterBridge::LevelMode::RMS;

        // RMS uses the strips' 0.3 s window and +3 dB sine calibration. Peak is
        // scaled by 1/sqrt(2) so SimpleMeter's +3 dB cancels and full scale reads 0 dB.
        const float level = rmsMode ? (float) levelMeter->rms() * 1.41421356f : raw;
        meter.setValue (0, level * 0.70710678f);
        meter.refresh();

        const bool wasClipped = clip.isClipped();
        clip.update (level >= 1.0f);

        const auto dB = Decibels::gainToDecibels (level, -60.f);
        const bool colourChanged = (dB > 0.f) != (levelDb > 0.f) || (dB > -6.f) != (levelDb > -6.f);
        levelDb = dB;
        const auto text = levelmeter::textFor (dB, getWidth() >= 24 ? 1 : 0);

        if (wasClipped != clip.isClipped() || colourChanged || text != levelText)
        {
            levelText = text;
            repaint();
        }
    }

    SimpleMeter meter;
    AudioEngine::LevelMeterPtr levelMeter;
    ClipHold clip;
    String levelText { "-inf" };
    float levelDb = -60.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VuLevelMeter)
};

static MeterBridge::LevelMode sLevelMode = MeterBridge::LevelMode::Peak;

static juce::ChangeBroadcaster& levelModeBroadcaster()
{
    static juce::ChangeBroadcaster broadcaster;
    return broadcaster;
}

void MeterBridge::setLevelMode (LevelMode mode)
{
    if (sLevelMode == mode)
        return;
    sLevelMode = mode;
    levelModeBroadcaster().sendChangeMessage();
}

MeterBridge::LevelMode MeterBridge::getLevelMode() { return sLevelMode; }

class MeterBridge::Impl : public juce::ChangeListener
{
public:
    Impl (MeterBridge& mb) : bridge (mb)
    {
        bridge.addAndMakeVisible (audioInLabel);
        bridge.addAndMakeVisible (audioOutLabel);
        bridge.addAndMakeVisible (modeLabel);
        bridge.addChildComponent (dbScale);
        dbScale.setInterceptsMouseClicks (false, false);
        modeLabel.setInterceptsMouseClicks (false, false);
        modeLabel.setJustificationType (Justification::centred);
        modeLabel.setFont (Font (FontOptions (9.4f)));
        levelModeBroadcaster().addChangeListener (this);
    }

    ~Impl()
    {
        jassert (ctx != nullptr);
        ctx->devices().removeChangeListener (this);
        levelModeBroadcaster().removeChangeListener (this);
    }

    void updateSectionLabels()
    {
        audioInLabel.setText ("IN", dontSendNotification);
        audioOutLabel.setText ("OUT", dontSendNotification);
        audioInLabel.setJustificationType (Justification::centred);
        audioOutLabel.setJustificationType (Justification::centred);
        modeLabel.setText (sLevelMode == LevelMode::RMS ? "RMS dB" : "Peak dB", dontSendNotification);
        modeLabel.setVisible (vuStyle);
    }

    int sectionPadding() { return 6; }

    /** Width reserved on the left for the dB scale (VU style only). */
    int leftInset() const { return vuStyle ? scaleWidth : 0; }

    int meterSpaceRequired (bool input)
    {
        return ((meterSize + meterSpace) * (input ? meters.size() : metersOut.size()));
    }

    int totalSpaceRequired()
    {
        return meterSpaceRequired (true) + (sectionPadding() * 2) + meterSpaceRequired (false);
    }

    void changeListenerCallback (juce::ChangeBroadcaster* source) override
    {
        if (source == &levelModeBroadcaster())
        {
            updateSectionLabels();
            return;
        }

        refresh();
        bridge.resized();
    }

    bool showChannelLabels() { return meterSize >= 18; }

    /** Shrinks the meters so every channel fits the bridge width. */
    void fitMeters()
    {
        drawSize = meterSize;
        drawSpace = meterSpace;
        const int count = meters.size() + metersOut.size();
        const int padding = (audioInsVisible && audioOutsVisible) ? sectionPadding() * 2 : 0;
        const int available = bridge.getWidth() - padding - leftInset();
        const int wanted = (meterSize + meterSpace) * count;
        if (count == 0 || available <= 0 || wanted <= available)
            return;

        const float scale = (float) available / (float) wanted;
        drawSize = jmax (4, (int) (meterSize * scale));
        drawSpace = jmax (1, (int) (meterSpace * scale));
    }

    int drawSpaceRequired (bool input)
    {
        return (drawSize + drawSpace) * (input ? meters.size() : metersOut.size());
    }

    int drawTotalSpace()
    {
        return drawSpaceRequired (true) + (sectionPadding() * 2) + drawSpaceRequired (false);
    }

    void resized()
    {
        fitMeters();
        const int labSize = 15;
        const int modeHeight = vuStyle ? 12 : 0;
        modeLabel.setBounds (0, bridge.getHeight() - modeHeight - 3, bridge.getWidth(), modeHeight);
        auto r = bridge.getLocalBounds()
                     .reduced (0, 3)
                     .withWidth (drawTotalSpace());
        r.removeFromTop (4);
        r.removeFromBottom (4 + modeHeight);
        if (audioInsVisible && ! audioOutsVisible)
        {
            auto r1 = r.removeFromTop (labSize);
            auto r2 = r.removeFromBottom (showChannelLabels() ? labSize : 0);

            int totalSpace = drawSpaceRequired (true);

            r.setX ((bridge.getWidth() + leftInset()) / 2 - totalSpace / 2);
            r2.setX (r.getX());
            r1.setX (r.getX());
            r1.setWidth (totalSpace);
            audioInLabel.setBounds (r1);

            for (auto* meter : meters)
            {
                meter->setBounds (r.removeFromLeft (drawSize));
                r.removeFromLeft (drawSpace);
            }

            if (showChannelLabels())
            {
                for (auto* lab : meterLabels)
                {
                    lab->setBounds (r2.removeFromLeft (drawSize));
                    r2.removeFromLeft (drawSpace);
                }
            }
        }

        else if (audioOutsVisible && ! audioInsVisible)
        {
            auto r1 = r.removeFromTop (labSize);
            auto r2 = r.removeFromBottom (showChannelLabels() ? labSize : 0);
            int totalSpace = drawSpaceRequired (false);

            r.setX ((bridge.getWidth() + leftInset()) / 2 - totalSpace / 2);
            r2.setX (r.getX());
            r1.setX (r.getX());
            r1.setWidth (totalSpace);
            audioOutLabel.setBounds (r1);
            for (auto* meter : metersOut)
            {
                meter->setBounds (r.removeFromLeft (drawSize));
                r.removeFromLeft (drawSpace);
            }

            if (showChannelLabels())
            {
                for (auto* lab : meterOutLabels)
                {
                    lab->setBounds (r2.removeFromLeft (drawSize));
                    r2.removeFromLeft (drawSpace);
                }
            }
        }
        else if (audioInsVisible && audioOutsVisible)
        {
            auto r1 = r.removeFromTop (labSize);
            auto r2 = r.removeFromBottom (showChannelLabels() ? labSize : 0);

            int totalSpace = drawTotalSpace();

            r.setX ((bridge.getWidth() + leftInset()) / 2 - totalSpace / 2);
            r2.setX (r.getX());
            r1.setX (r.getX());
            r1.setWidth (drawSpaceRequired (true));
            audioInLabel.setBounds (r1);

            for (auto* meter : meters)
            {
                meter->setBounds (r.removeFromLeft (drawSize));
                r.removeFromLeft (drawSpace);
            }

            r.removeFromLeft (sectionPadding() * 2);

            r1.setX (r.getX());
            r1.setWidth (drawSpaceRequired (false));
            audioOutLabel.setBounds (r1);

            for (auto* meter : metersOut)
            {
                meter->setBounds (r.removeFromLeft (drawSize));
                r.removeFromLeft (drawSpace);
            }

            if (showChannelLabels())
            {
                for (auto* lab : meterLabels)
                {
                    lab->setBounds (r2.removeFromLeft (drawSize));
                    r2.removeFromLeft (drawSpace);
                }

                r2.removeFromLeft (sectionPadding() * 2);

                for (auto* lab : meterOutLabels)
                {
                    lab->setBounds (r2.removeFromLeft (drawSize));
                    r2.removeFromLeft (drawSpace);
                }
            }
        }

        // the scale spans the same area as the meters, below each readout box
        auto* first = ! meters.isEmpty() ? meters.getFirst() : metersOut.getFirst();
        dbScale.setVisible (leftInset() > 0 && first != nullptr);
        if (first != nullptr && leftInset() > 0)
        {
            const int top = first->getY() + VuLevelMeter::readoutHeight + 2;
            dbScale.setBounds (first->getX() - scaleWidth - 2, top, scaleWidth, first->getBottom() - top);
        }
    }

    void paint (Graphics& g)
    {
        g.fillAll (Colors::widgetBackgroundColor.darker());
    }

    void init (Context& context)
    {
        ctx = &context;
        engine = context.audio();
        context.devices().addChangeListener (this);
        refresh();
    }

    void refresh()
    {
        meters.clear();
        meterLabels.clear();
        metersOut.clear();
        meterOutLabels.clear();

        const auto makeMeter = [this] (int channel, bool input) -> Component* {
            if (vuStyle)
                return new VuLevelMeter (engine, channel, input);
            return new SimpleLevelMeter (engine, channel, input);
        };

        if (audioInsVisible)
            for (int c = 0; c < engine->getNumChannels (true); ++c)
            {
                bridge.addAndMakeVisible (meters.add (makeMeter (c, true)));
                bridge.addAndMakeVisible (meterLabels.add (new Label (String (c + 1), String (c + 1))));
            }

        if (audioOutsVisible)
            for (int c = 0; c < engine->getNumChannels (false); ++c)
            {
                bridge.addAndMakeVisible (metersOut.add (makeMeter (c, false)));
                bridge.addAndMakeVisible (meterOutLabels.add (new Label (String (c + 1), String (c + 1))));
            }

        auto refreshIOLabel = [this] (Label& lab, bool vis) {
            lab.setVisible (vis);
            lab.setFont (juce::Font (FontOptions (meterSize > 14 ? 10.f : 9.4f)));
            lab.setJustificationType (juce::Justification::centred);
        };

        auto refreshMeterLabel = [this] (Label& lab) {
            lab.setFont (juce::Font (FontOptions (12.f)));
            lab.setJustificationType (juce::Justification::centred);
            lab.setVisible (showChannelLabels());
        };

        refreshIOLabel (audioInLabel, audioInsVisible);
        refreshIOLabel (audioOutLabel, audioOutsVisible);
        updateSectionLabels();

        for (auto* lab : meterLabels)
            refreshMeterLabel (*lab);
        for (auto* lab : meterOutLabels)
            refreshMeterLabel (*lab);
    }

    void setMeterSizes (int size, int space)
    {
        meterSize = size;
        meterSpace = space;
        refresh();
        bridge.resized();
    }

    void setVisibility (uint32_t newVis)
    {
        if (visibility == newVis)
            return;
        visibility = newVis;
        audioInsVisible = bridge.hasVisibility (AudioIns);
        audioOutsVisible = bridge.hasVisibility (AudioOuts);
        refresh();
    }

private:
    friend class MeterBridge;
    Context* ctx = nullptr;
    MeterBridge& bridge;
    AudioEnginePtr engine;
    OwnedArray<Component> meters, metersOut;
    bool vuStyle = false;
    OwnedArray<Label> meterLabels, meterOutLabels;
    Label audioInLabel { "audioin", "IN" };
    Label audioOutLabel { "audioout", "OUT" };
    Label modeLabel;
    static constexpr int scaleWidth = 26;
    DecibelScale dbScale;
    bool audioInsVisible = false;
    bool audioOutsVisible = false;
    int meterSize = 18;
    int drawSize = 18;
    int drawSpace = 4;
    int meterSpace = 4;
    [[maybe_unused]] int meterSegments = 7;
    uint32_t visibility = 0;
};

MeterBridge::MeterBridge (Context& ctx)
{
    setName ("Meter Bridge");
    setComponentID ("el.MeterBridge");
    impl = std::make_unique<Impl> (*this);
    impl->init (ctx);
    setSize (4 * 30, 80);
    setVisibility (AudioIns | AudioOuts);
}

MeterBridge::~MeterBridge() {}
void MeterBridge::resized() { impl->resized(); }
void MeterBridge::paint (juce::Graphics& g) { impl->paint (g); }

int MeterBridge::meterSize() const { return impl->meterSize; }
void MeterBridge::setMeterSize (int newSize)
{
    newSize = jmax (12, newSize);
    impl->setMeterSizes (newSize, impl->meterSpace);
}

void MeterBridge::setVuStyle (bool useVuStyle)
{
    if (impl->vuStyle == useVuStyle)
        return;
    impl->vuStyle = useVuStyle;
    impl->refresh();
    resized();
}

int MeterBridge::preferredWidth() const
{
    const bool ins = hasVisibility (AudioIns);
    const bool outs = hasVisibility (AudioOuts);
    return impl->leftInset()
           + (ins ? impl->meterSpaceRequired (true) : 0)
           + (outs ? impl->meterSpaceRequired (false) : 0)
           + (ins && outs ? impl->sectionPadding() * 2 : 0);
}

//==============================================================================
uint32 MeterBridge::visibility() const noexcept
{
    return impl->visibility;
}

void MeterBridge::setVisibility (uint32 visibility)
{
    impl->setVisibility (visibility);
    resized();
}

bool MeterBridge::hasVisibility (uint32 visibility) const noexcept
{
    return (impl->visibility & visibility) != 0;
}

//==============================================================================
MeterBridgeView::MeterBridgeView()
    : ContentView()
{
    setName ("Meter Bridge");
    setComponentID ("el.MeterBridgeView");
    setSize (600, 80);
}

MeterBridgeView::~MeterBridgeView() {}

#if 1
void MeterBridgeView::initializeView (Services& sm)
{
    if (bridge == nullptr)
    {
        bridge = std::make_unique<MeterBridge> (sm.context());
        addAndMakeVisible (bridge.get());
        bridge->setInterceptsMouseClicks (false, true);
    }
    resized();
}
#endif

MeterBridge& MeterBridgeView::meterBridge()
{
    jassert (bridge);
    return *bridge;
}

void MeterBridgeView::resized()
{
    if (bridge)
        bridge->setBounds (getLocalBounds().reduced (1));
}

void MeterBridgeView::mouseDown (const juce::MouseEvent& ev)
{
    if (ev.mods.isPopupMenu())
    {
        PopupMenu menu;
        menu.addItem ("Audio Ins", true, bridge->hasVisibility (MeterBridge::AudioIns), [this]() {
            auto flags = bridge->visibility();
            if (bridge->hasVisibility (MeterBridge::AudioIns))
                flags &= ~(MeterBridge::AudioIns);
            else
                flags |= MeterBridge::AudioIns;
            bridge->setVisibility (flags);
        });
        menu.addItem ("Audio Outs", true, bridge->hasVisibility (MeterBridge::AudioOuts), [this]() {
            auto flags = bridge->visibility();
            if (bridge->hasVisibility (MeterBridge::AudioOuts))
                flags &= ~(MeterBridge::AudioOuts);
            else
                flags |= MeterBridge::AudioOuts;
            bridge->setVisibility (flags);
        });

        menu.addSeparator();

        menu.addItem (12, "Small", true, bridge->meterSize() == 12);
        menu.addItem (18, "Normal", true, bridge->meterSize() == 18);
        menu.addItem (24, "Large", true, bridge->meterSize() == 24);
        menu.addItem (32, "Extra Large", true, bridge->meterSize() == 32);

        auto res = menu.show();

        if (res >= 12 && res <= 32)
            bridge->setMeterSize (res);

        resized();
        if (onConfigChanged)
            onConfigChanged();
    }
}

} // namespace element
