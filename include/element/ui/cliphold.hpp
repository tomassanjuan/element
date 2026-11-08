// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <element/juce/core.hpp>

namespace element {

/** Latches a clip state and releases it after a time without further clips. */
class ClipHold {
public:
    explicit ClipHold (juce::uint32 holdMilliseconds = 5000) noexcept
        : holdMs (holdMilliseconds) {}

    /** Call whenever the signal clips. Restarts the hold time. */
    void hit() noexcept
    {
        clipped = true;
        lastHit = juce::Time::getMillisecondCounter();
    }

    void clear() noexcept { clipped = false; }

    bool isClipped() const noexcept { return clipped; }

    /** Releases the clip state when the hold time has passed.

        @return true if the state was released by this call
    */
    bool update() noexcept
    {
        if (clipped && juce::Time::getMillisecondCounter() - lastHit > holdMs)
        {
            clipped = false;
            return true;
        }

        return false;
    }

private:
    juce::uint32 holdMs;
    juce::uint32 lastHit = 0;
    bool clipped = false;
};

} // namespace element
