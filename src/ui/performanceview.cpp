// SPDX-License-Identifier: GPL-3.0-or-later

#include <element/devices.hpp>
#include <element/node.hpp>
#include <element/processor.hpp>
#include <element/services.hpp>
#include <element/session.hpp>
#include <element/ui/style.hpp>

#include "ui/performanceview.hpp"
#include "common.hpp"

namespace element {

class PerformanceView::Content : public Component,
                                 private Timer
{
public:
    explicit Content (Services& s) : services (s)
    {
        setOpaque (true);
        startTimerHz (5);
    }

    void timerCallback() override
    {
        repaint();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colors::contentBackgroundColor);
        g.setColour (Colors::textColor);
        g.setFont (Font (FontOptions (14.f)));

        auto& devices = services.context().devices();
        auto r = getLocalBounds().reduced (10, 6);

        double budgetMs = 0.0;
        String header = "No audio device";
        if (auto* dev = devices.getCurrentAudioDevice())
        {
            budgetMs = dev->getCurrentBufferSizeSamples() * 1000.0 / jmax (1.0, dev->getCurrentSampleRate());
            header = "Engine CPU: " + String (devices.getCpuUsage() * 100.f, 1) + "%     Drops: "
                     + String (devices.getXRunCount()) + "     Block budget: " + String (budgetMs, 2) + " ms";
        }
        g.drawText (header, r.removeFromTop (22), Justification::centredLeft);
        r.removeFromTop (6);

        const auto session = services.context().session();
        const auto graph = session != nullptr ? session->getActiveGraph() : Node();

        struct Row
        {
            String name;
            float avg, peak;
        };
        std::vector<Row> rows;
        for (int i = 0; i < graph.getNumNodes(); ++i)
        {
            const auto node = graph.getNode (i);
            if (auto* proc = node.getObject())
                rows.push_back ({ node.getDisplayName(), proc->getRenderMsAverage(), proc->getRenderMsPeak() });
        }
        std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.avg > b.avg; });

        const int nameW = jmax (120, r.getWidth() / 3);
        auto head = r.removeFromTop (20);
        g.setColour (Colors::textColor.withAlpha (0.6f));
        g.drawText ("Node", head.removeFromLeft (nameW), Justification::centredLeft);
        g.drawText ("Avg ms", head.removeFromLeft (70), Justification::centredRight);
        g.drawText ("Peak ms", head.removeFromLeft (70), Justification::centredRight);
        g.drawText ("% of block", head.reduced (10, 0), Justification::centredLeft);

        float total = 0.f;
        for (const auto& row : rows)
        {
            total += row.avg;
            auto line = r.removeFromTop (22);
            if (r.getHeight() < 0)
                break;

            const float load = budgetMs > 0.0 ? (float) (row.avg / budgetMs) : 0.f;
            g.setColour (Colors::textColor);
            g.drawText (row.name, line.removeFromLeft (nameW), Justification::centredLeft, true);
            g.drawText (String (row.avg, 3), line.removeFromLeft (70), Justification::centredRight);
            g.drawText (String (row.peak, 3), line.removeFromLeft (70), Justification::centredRight);

            auto bar = line.reduced (10, 5);
            g.setColour (Colors::textColor.withAlpha (0.12f));
            g.fillRect (bar);
            g.setColour (load > 0.8f ? Colours::red : load > 0.5f ? Colours::orange : Colours::limegreen);
            g.fillRect (bar.withWidth (roundToInt (bar.getWidth() * jlimit (0.f, 1.f, load))));
            g.setColour (Colors::textColor);
            g.drawText (String (load * 100.f, 1) + "%", bar, Justification::centred);
        }

        if (rows.empty())
        {
            g.setColour (Colors::textColor);
            g.drawText ("No nodes in the active graph", r, Justification::centred);
        }
        else if (r.getHeight() >= 0)
        {
            r.removeFromTop (6);
            g.setColour (Colors::textColor);
            g.drawText ("Graph total: " + String (total, 3) + " ms", r.removeFromTop (22), Justification::centredLeft);
        }
    }

private:
    Services& services;
};

PerformanceView::PerformanceView()
{
    setName (EL_VIEW_PERFORMANCE);
}

PerformanceView::~PerformanceView()
{
    content = nullptr;
}

void PerformanceView::resized()
{
    if (content)
        content->setBounds (getLocalBounds());
}

void PerformanceView::initializeView (Services& app)
{
    content.reset (new Content (app));
    addAndMakeVisible (content.get());
    resized();
}

} // namespace element
