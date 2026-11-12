// SPDX-License-Identifier: GPL-3.0-or-later

#include <element/devices.hpp>
#include <element/engine.hpp>
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
        auto* engine = services.find<EngineService>();

        struct Row
        {
            String name;
            float avg, peak;
        };
        struct GraphRows
        {
            String name;
            std::vector<Row> rows;
        };
        std::vector<GraphRows> graphs;
        for (int g = 0; session != nullptr && g < session->getNumGraphs(); ++g)
        {
            const auto graph = session->getGraph (g);
            if (engine != nullptr && ! engine->isGraphLoaded (graph))
                continue;

            GraphRows gr { graph.getDisplayName(), {} };
            for (int i = 0; i < graph.getNumNodes(); ++i)
            {
                const auto node = graph.getNode (i);
                if (auto* proc = node.getObject())
                    gr.rows.push_back ({ node.getDisplayName(), proc->getRenderMsAverage(), proc->getRenderMsPeak() });
            }
            std::sort (gr.rows.begin(), gr.rows.end(), [] (const Row& x, const Row& y) { return x.avg > y.avg; });
            graphs.push_back (std::move (gr));
        }

        const int nameW = jmax (120, r.getWidth() / 3);
        auto head = r.removeFromTop (20);
        g.setColour (Colors::textColor.withAlpha (0.6f));
        g.drawText ("Node", head.removeFromLeft (nameW), Justification::centredLeft);
        g.drawText ("Avg ms", head.removeFromLeft (70), Justification::centredRight);
        g.drawText ("Peak ms", head.removeFromLeft (70), Justification::centredRight);
        g.drawText ("% of block", head.reduced (10, 0), Justification::centredLeft);

        float total = 0.f;
        for (const auto& gr : graphs)
        {
            if (r.getHeight() < 22)
                break;
            float graphTotal = 0.f;
            for (const auto& row : gr.rows)
                graphTotal += row.avg;
            total += graphTotal;

            g.setColour (Colors::textColor);
            g.setFont (Font (FontOptions (14.f, Font::bold)));
            g.drawText (gr.name + "  -  " + String (graphTotal, 3) + " ms", r.removeFromTop (24), Justification::bottomLeft, true);
            g.setFont (Font (FontOptions (14.f)));

            for (const auto& row : gr.rows)
            {
                if (r.getHeight() < 22)
                    break;
                auto line = r.removeFromTop (22);
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
        }

        if (graphs.empty())
        {
            g.setColour (Colors::textColor);
            g.drawText ("No loaded graphs", r, Justification::centred);
        }
        else if (r.getHeight() >= 28)
        {
            r.removeFromTop (6);
            g.setColour (Colors::textColor);
            g.drawText ("All graphs: " + String (total, 3) + " ms", r.removeFromTop (22), Justification::centredLeft);
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
