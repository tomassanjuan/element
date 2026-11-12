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

namespace {
constexpr int kMargin = 10;
constexpr int kRowH = 22;
constexpr int kGraphH = 28;
constexpr int kAvgW = 70;

struct Row
{
    String name;
    float avg, peak;
};

struct GraphRows
{
    String name;
    std::vector<Row> rows;
    float total = 0.f, peak = 0.f;
};

struct Snapshot
{
    std::vector<GraphRows> graphs;
    float total = 0.f, peak = 0.f;
    double budgetMs = 0.0;
};

int nameColumnWidth (int width) { return jmax (120, (width - 2 * kMargin) / 3); }

/** Draws one row: name, average and peak ms, and a bar of the average against the block budget. */
void drawRow (Graphics& g, Rectangle<int> line, int nameW, const String& name, float avg, float peak, double budgetMs)
{
    const float load = budgetMs > 0.0 ? (float) (avg / budgetMs) : 0.f;
    g.setColour (Colors::textColor);
    g.drawText (name, line.removeFromLeft (nameW), Justification::centredLeft, true);
    g.drawText (String (avg, 3), line.removeFromLeft (kAvgW), Justification::centredRight);
    g.drawText (String (peak, 3), line.removeFromLeft (kAvgW), Justification::centredRight);

    auto bar = line.reduced (kMargin, 5);
    g.setColour (Colors::textColor.withAlpha (0.12f));
    g.fillRect (bar);
    g.setColour (load > 0.8f ? Colours::red : load > 0.5f ? Colours::orange : Colours::limegreen);
    g.fillRect (bar.withWidth (roundToInt (bar.getWidth() * jlimit (0.f, 1.f, load))));
    g.setColour (Colors::textColor);
    g.drawText (String (load * 100.f, 1) + "%", bar, Justification::centred);
}

/** The scrolling list of graphs and nodes. Sizes itself to its content. */
class Rows : public Component
{
public:
    void setSnapshot (Snapshot&& s)
    {
        snap = std::move (s);
        int h = snap.graphs.empty() ? kRowH : 0;
        for (const auto& gr : snap.graphs)
            h += kGraphH + (int) gr.rows.size() * kRowH;
        setSize (getWidth(), h);
        repaint();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colors::contentBackgroundColor);
        g.setFont (Font (FontOptions (14.f)));

        if (snap.graphs.empty())
        {
            g.setColour (Colors::textColor);
            g.drawText ("No loaded graphs", getLocalBounds(), Justification::centred);
            return;
        }

        auto r = getLocalBounds().reduced (kMargin, 0);
        const int nameW = nameColumnWidth (getWidth());
        for (const auto& gr : snap.graphs)
        {
            r.removeFromTop (kGraphH - kRowH);
            g.setFont (Font (FontOptions (14.f, Font::bold)));
            drawRow (g, r.removeFromTop (kRowH), nameW, gr.name, gr.total, gr.peak, snap.budgetMs);
            g.setFont (Font (FontOptions (14.f)));

            for (const auto& row : gr.rows)
                drawRow (g, r.removeFromTop (kRowH), nameW, row.name, row.avg, row.peak, snap.budgetMs);
        }
    }

private:
    Snapshot snap;
};
} // namespace

class PerformanceView::Content : public Component,
                                 private Timer
{
public:
    explicit Content (Services& s) : services (s)
    {
        setOpaque (true);
        viewport.setViewedComponent (&rows, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        startTimerHz (4);
        timerCallback();
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds().withTrimmedTop (headerH));
        rows.setSize (viewport.getMaximumVisibleWidth(), rows.getHeight());
    }

    void timerCallback() override
    {
        auto& devices = services.context().devices();
        Snapshot snap;
        header = "No audio device";
        if (auto* dev = devices.getCurrentAudioDevice())
        {
            snap.budgetMs = dev->getCurrentBufferSizeSamples() * 1000.0 / jmax (1.0, dev->getCurrentSampleRate());
            header = "Engine CPU: " + String (devices.getCpuUsage() * 100.f, 1) + "%     Drops: "
                     + String (devices.getXRunCount()) + "     Block budget: " + String (snap.budgetMs, 2) + " ms";
        }

        const auto session = services.context().session();
        auto* engine = services.find<EngineService>();
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
                {
                    gr.rows.push_back ({ node.getDisplayName(), proc->getRenderMsAverage(), proc->getRenderMsPeak() });
                    gr.total += gr.rows.back().avg;
                    gr.peak += gr.rows.back().peak;
                }
            }
            std::sort (gr.rows.begin(), gr.rows.end(), [] (const Row& x, const Row& y) { return x.avg > y.avg; });
            snap.total += gr.total;
            snap.peak += gr.peak;
            snap.graphs.push_back (std::move (gr));
        }

        total = snap.total;
        totalPeak = snap.peak;
        budgetMs = snap.budgetMs;
        rows.setSnapshot (std::move (snap));
        resized();
        repaint (0, 0, getWidth(), headerH);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colors::contentBackgroundColor);
        g.setFont (Font (FontOptions (14.f)));
        g.setColour (Colors::textColor);

        auto r = getLocalBounds().reduced (kMargin, 6);
        g.drawText (header, r.removeFromTop (kRowH), Justification::centredLeft);
        r.removeFromTop (6);

        auto head = r.removeFromTop (20);
        const int nameW = nameColumnWidth (getWidth());
        g.setColour (Colors::textColor.withAlpha (0.6f));
        g.drawText ("Node", head.removeFromLeft (nameW), Justification::centredLeft);
        g.drawText ("Avg ms", head.removeFromLeft (kAvgW), Justification::centredRight);
        g.drawText ("Peak ms", head.removeFromLeft (kAvgW), Justification::centredRight);
        g.drawText ("% of block", head.reduced (kMargin, 0), Justification::centredLeft);

        g.setFont (Font (FontOptions (14.f, Font::bold)));
        drawRow (g, r.removeFromTop (kRowH), nameW, "Master (all graphs)", total, totalPeak, budgetMs);
        g.setColour (Colors::textColor.withAlpha (0.2f));
        g.fillRect (0, headerH - 1, getWidth(), 1);
    }

private:
    static constexpr int headerH = 6 + kRowH + 6 + 20 + kRowH + 6;

    Services& services;
    Rows rows;
    Viewport viewport;
    String header;
    float total = 0.f, totalPeak = 0.f;
    double budgetMs = 0.0;
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
