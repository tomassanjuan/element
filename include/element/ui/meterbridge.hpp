// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <element/element.hpp>
#include <element/ui/content.hpp>

namespace element {

class Context;

class MeterBridge : public juce::Component {
public:
    enum Visibility : uint32_t {
        AudioIns = (1u << 0u),
        AudioOuts = (1u << 1u),
        MidiIns = (1u << 2u),
        MIdiOuts = (1u << 3u)
    };

    MeterBridge() = delete;
    MeterBridge (Context&);
    ~MeterBridge();

    uint32 visibility() const noexcept;
    void setVisibility (uint32);
    bool hasVisibility (uint32) const noexcept;

    int meterSize() const;
    void setMeterSize (int);

    /** Draw the meters like the channel strip VU meters instead of segments. */
    void setVuStyle (bool);

    /** Width needed to show all currently visible meters. */
    int preferredWidth() const;

    /** @internal */
    void resized() override;
    /** @internal */
    void paint (juce::Graphics&) override;

private:
    class Impl;
    friend class Impl;
    std::unique_ptr<Impl> impl;
    EL_DISABLE_COPY (MeterBridge)
};

//==============================================================================
class MeterBridgeView : public ContentView {
public:
    MeterBridgeView();
    ~MeterBridgeView();

    void initializeView (Services& sm) override;

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    MeterBridge& meterBridge();

    /** Called after the user changes the bridge's visibility or size. */
    std::function<void()> onConfigChanged;

private:
    std::unique_ptr<MeterBridge> bridge;
    EL_DISABLE_COPY (MeterBridgeView)
};

} // namespace element
