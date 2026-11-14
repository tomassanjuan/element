// Copyright 2019-2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <element/engine.hpp>
#include <element/services.hpp>
#include <element/ui/style.hpp>
#include <element/ui/decibelscale.hpp>
#include <element/ui/meterbridge.hpp>
#include <element/ui/standard.hpp>

#include "ui/nodechannelstrip.hpp"
#include "ui/graphmixerview.hpp"
#include "ui/horizontallistbox.hpp"
#include "ui/viewhelpers.hpp"

#include "common.hpp"

namespace element {

class GraphMixerChannelStrip : public NodeChannelStripComponent,
                               public DragAndDropTarget,
                               public ComponentListener
{
public:
    std::function<void()> onRefreshNeeded;

    GraphMixerChannelStrip (GuiService& gui) : NodeChannelStripComponent (gui, false)
    {
        onNodeChanged = [this]() { setNodeNameEditable (! (getNode().isIONode())); };
        listener.reset (new ChildListener (*this));
        addMouseListener (listener.get(), true);
    }

    ~GraphMixerChannelStrip()
    {
        removeMouseListener (listener.get());
        listener.reset();
    }

    void selectInGuiController()
    {
        if (auto* const cc = ViewHelpers::findContentComponent (this))
            if (auto* const gui = cc->services().find<GuiService>())
                if (getNode() != gui->getSelectedNode())
                    gui->selectNode (getNode());
    }

    void mouseDown (const MouseEvent& ev) override
    {
        if (ev.mods.isPopupMenu())
            return;

        if (! down)
        {
            down = true;
            dragging = false;
            selectInGuiController();
        }
    }

    void showContextMenu()
    {
        PopupMenu menu;
        menu.addItem (1, TRANS ("Hide from mixer"));
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [strip = Component::SafePointer<GraphMixerChannelStrip> (this)] (int result) {
                                if (strip == nullptr || result != 1)
                                    return;
                                strip->getNode().setHiddenInMixer (true);
                                if (strip->onRefreshNeeded)
                                    strip->onRefreshNeeded();
                            });
    }

    void mouseDrag (const MouseEvent& ev) override
    {
        if (down && ! dragging)
        {
            dragging = true;
            auto* dnd = findParentComponentOfClass<DragAndDropContainer>();
            Image image (Image::ARGB, 1, 1, true);
            ScaledImage si (image);
            dnd->startDragging (var ("graphMixerStrip"), this, si);
        }
    }

    void mouseMove (const MouseEvent& ev) override
    {
        // noop
    }

    void mouseUp (const MouseEvent& ev) override
    {
        dragging = down = hover = false;
    }

    bool shouldDrawDragImageWhenOver() override
    {
        return false;
    }

    void itemDragEnter (const SourceDetails&) override
    {
        hover = true;
        repaint();
    }

    void itemDragExit (const SourceDetails&) override
    {
        hover = false;
        repaint();
    }

    void paint (Graphics& g) override
    {
        NodeChannelStripComponent::paint (g);
        if (selected)
        {
            g.setColour (Colours::white);
            g.setOpacity (0.09f);
            g.fillAll();
        }
    }

    void paintOverChildren (Graphics& g) override
    {
        if (selected || (hover && ! dragging && ! down))
        {
            g.setColour (Colors::toggleBlue);
            g.drawRect (0.f, 0.f, (float) getWidth(), (float) getHeight(), selected ? 1.5f : 1.0f);
        }
    }

    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        return details.description == "graphMixerStrip";
    }

    void itemDropped (const SourceDetails& details) override
    {
        if (details.description == "graphMixerStrip")
        {
            auto* strip = dynamic_cast<GraphMixerChannelStrip*> (details.sourceComponent.get());
            auto myNode = getNode().data();
            auto dNode = strip->getNode().data();
            ValueTree parent = dNode.getParent();

            int myIndex = parent.indexOf (myNode);
            int dIndex = parent.indexOf (dNode);
            if (myIndex >= 0 && dIndex >= 0)
            {
                parent.moveChild (dIndex, myIndex, nullptr);
                if (onRefreshNeeded)
                    onRefreshNeeded();
            }
        }

        hover = false;
        repaint();
    }

    void setSelected (bool isNowSelected)
    {
        if (selected == isNowSelected)
            return;
        selected = isNowSelected;
        repaint();
    }

private:
    bool selected = false;
    bool dragging = false;
    bool down = false;
    bool hover = false;

    struct ChildListener : public MouseListener
    {
        ChildListener (GraphMixerChannelStrip& o) : owner (o) {}
        void mouseDown (const MouseEvent& ev) override
        {
            if (ev.mods.isPopupMenu())
                owner.showContextMenu();
            else
                owner.selectInGuiController();
        }

        GraphMixerChannelStrip& owner;
    };

    std::unique_ptr<ChildListener> listener;
};

class GraphMixerListBoxModel : public ListBoxModel
{
public:
    GraphMixerListBoxModel (GuiService& g, HorizontalListBox& b) : gui (g), box (b) { refreshNodes(); }
    ~GraphMixerListBoxModel() {}

    int getNumRows() override
    {
        return nodes.size();
    }

    void paintListBoxItem (int rowNumber, Graphics& g, int width, int height, bool rowIsSelected) override
    {
        ignoreUnused (rowNumber, g, width, height, rowIsSelected);
    }

    Node getNode (int r)
    {
        return nodes[r];
    }

    Component* refreshComponentForRow (int rowNumber, bool isRowSelected, Component* existing) override
    {
        GraphMixerChannelStrip* const strip = existing == nullptr
                                                  ? new GraphMixerChannelStrip (gui)
                                                  : dynamic_cast<GraphMixerChannelStrip*> (existing);
        strip->onRefreshNeeded = [this] { refresh(); };
        auto node = getNode (rowNumber);
        strip->setNode (node);
        strip->setSelected (node == gui.getSelectedNode());
        return strip;
    }

    void refresh()
    {
        refreshNodes();
        box.updateContent();
    }

    void setNode (const Node& node)
    {
        if (node == _node)
            return;
        _node = node.isGraph() ? node : node.getParentGraph();
        refreshNodes();
    }

    /** True if the graph being shown is unloaded, so there is nothing live to display. */
    bool isUnloaded() const { return unloaded; }

    const NodeArray& getNodes() const noexcept { return nodes; }

    void refreshNodes()
    {
        nodes.clearQuick();
        const auto graph = _node.isGraph() ? _node
                                           : gui.context().session()->getActiveGraph();
        auto* engine = gui.sibling<EngineService>();
        unloaded = engine != nullptr && ! engine->isGraphLoaded (graph);
        if (unloaded)
            return;

        for (int i = 0; i < graph.getNumNodes(); ++i)
        {
            const auto n = graph.getNode (i);
            // clang-format off
            if (n.isMidiIONode() || 
                n.getIdentifier() == EL_NODE_ID_MIDI_INPUT_DEVICE || 
                n.getIdentifier() == EL_NODE_ID_MIDI_OUTPUT_DEVICE ||
                n.isHiddenInMixer())
            {
                continue;
            }
            // clang-format on

            nodes.add (n);
        }
    }

    void backgroundClicked (const MouseEvent& ev) override
    {
        if (! ev.mods.isPopupMenu())
            return;

        const auto graph = _node.isGraph() ? _node
                                           : gui.context().session()->getActiveGraph();
        NodeArray hidden;
        for (int i = 0; i < graph.getNumNodes(); ++i)
        {
            const auto n = graph.getNode (i);
            if (n.isHiddenInMixer())
                hidden.add (n);
        }

        PopupMenu menu;
        for (int i = 0; i < hidden.size(); ++i)
            menu.addItem (i + 2, TRANS ("Show") + " " + hidden.getReference (i).getDisplayName());
        if (hidden.size() > 1)
        {
            menu.addSeparator();
            menu.addItem (1, TRANS ("Show all"));
        }
        else if (hidden.isEmpty())
        {
            menu.addItem (1, TRANS ("No hidden channels"), false);
        }

        menu.showMenuAsync (PopupMenu::Options(),
                            [safeBox = Component::SafePointer<Component> (&box), this, hidden] (int result) {
                                if (safeBox == nullptr || result <= 0)
                                    return;
                                if (result == 1)
                                    for (auto n : hidden)
                                        n.setHiddenInMixer (false);
                                else if (result - 2 < hidden.size())
                                    hidden[result - 2].setHiddenInMixer (false);
                                refresh();
                            });
    }

private:
    GuiService& gui;
    HorizontalListBox& box;
    Node _node;
    NodeArray nodes;
    bool unloaded = false;
    [[maybe_unused]] bool dragging = false;
};

/** Every channel of every node on one screen: a meter and a level readout per
    channel, grouped by node. Clicking a group selects its node. */
class ChannelOverview : public Component,
                        private Timer
{
public:
    explicit ChannelOverview (GuiService& g) : gui (g)
    {
        viewport.setViewedComponent (&inner, false);
        viewport.setScrollBarsShown (false, true);
        addAndMakeVisible (viewport);
        addAndMakeVisible (scale);
        scale.setInterceptsMouseClicks (false, false);
    }

    ~ChannelOverview() override { viewport.setViewedComponent (nullptr, false); }

    void setNodes (const NodeArray& nodes)
    {
        Array<uint32> ids;
        for (const auto& n : nodes)
            ids.add (n.getNodeId());

        if (ids == shownIds)
        {
            repaint();
            return;
        }

        shownIds = ids;
        groups.clear();
        for (const auto& n : nodes)
        {
            auto group = std::make_unique<Group> (gui, n);
            if (group->numColumns() > 0)
                inner.addAndMakeVisible (groups.add (group.release()));
        }

        layoutGroups();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        scaleArea = r.removeFromLeft (scaleWidth);
        viewport.setBounds (r);
        layoutGroups();
    }

    void paint (Graphics& g) override
    {
        g.setColour (Colors::textColor.withAlpha (0.6f));
        g.setFont (Font (FontOptions (9.f)));
        g.drawFittedText ("RMS\ndB", scaleArea.withTop (scale.getBottom()), Justification::centred, 2);
    }

    void visibilityChanged() override
    {
        if (isVisible())
            startTimerHz (15);
        else
            stopTimer();
    }

private:
    static constexpr int headerHeight = 22;
    static constexpr int readoutHeight = 32;
    static constexpr int scaleWidth = 30;
    static constexpr int columnWidth = 36;

    class Column : public Component
    {
    public:
        explicit Column (int channelNumber) : number (channelNumber) { addAndMakeVisible (meter); }

        void setLevel (float rms)
        {
            meter.setValue (0, rms);
            meter.refresh();

            const float newDb = Decibels::gainToDecibels (rms, -100.f) + levelmeter::rmsCalibrationDb;
            if (std::abs (newDb - db) >= 0.1f)
            {
                db = newDb;
                repaint (getLocalBounds().removeFromBottom (readoutHeight));
            }
        }

        void resized() override { meter.setBounds (getLocalBounds().withTrimmedBottom (readoutHeight).reduced (3, 0)); }

        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().removeFromBottom (readoutHeight);
            g.setFont (Font (FontOptions (11.f)));
            g.setColour (Colors::textColor);
            g.drawText (String (number), r.removeFromTop (readoutHeight / 2), Justification::centred, false);
            g.setFont (Font (FontOptions (11.f, Font::bold)));
            g.setColour (levelmeter::colourFor (db, Colors::textColor));
            g.drawText (levelmeter::textFor (db), r, Justification::centred, false);
        }

    private:
        SimpleMeter meter { 1, false };
        int number;
        float db = -100.f;
    };

    class Group : public Component
    {
    public:
        Group (GuiService& g, const Node& n) : gui (g), node (n)
        {
            if (ProcessorPtr p = node.getObject())
            {
                monitorInputs = node.isAudioOutputNode() || p->getNumAudioOutputs() == 0;
                const int count = monitorInputs ? p->getNumAudioInputs() : p->getNumAudioOutputs();
                for (int c = 0; c < count; ++c)
                    addAndMakeVisible (columns.add (new Column (c + 1)));
            }
        }

        int numColumns() const { return columns.size(); }
        int preferredWidth() const { return jmax (72, columns.size() * columnWidth + 8); }

        void update()
        {
            if (ProcessorPtr p = node.getObject())
                for (int c = 0; c < columns.size(); ++c)
                    columns[c]->setLevel (monitorInputs ? p->getInputRMS (c) : p->getOutputRMS (c));
        }

        void paint (Graphics& g) override
        {
            g.setColour (Colors::widgetBackgroundColor);
            g.fillAll();

            auto header = getLocalBounds().removeFromTop (headerHeight);
            const auto color = node.getColor (Colours::transparentBlack);
            if (color != Colours::transparentBlack)
            {
                g.setColour (color);
                g.fillRect (header);
            }

            g.setColour (color != Colours::transparentBlack ? Colours::white.overlaidWith (color).contrasting() : Colors::textColor);
            g.setFont (Font (FontOptions (Style::fontSizeDefault)));
            g.drawText (node.getDisplayName(), header.reduced (3, 0), Justification::centred, true);

            g.setColour (Colors::backgroundColor);
            g.fillRect (getWidth() - 1, 0, 1, getHeight());

            if (node == gui.getSelectedNode())
            {
                g.setColour (Colors::toggleBlue);
                g.drawRect (getLocalBounds(), 2);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().withTrimmedTop (headerHeight).reduced (4, 2);
            const int w = columns.isEmpty() ? 0 : r.getWidth() / columns.size();
            for (auto* col : columns)
                col->setBounds (r.removeFromLeft (w));
        }

        void mouseDown (const MouseEvent&) override
        {
            gui.selectNode (node);
            if (auto* parent = getParentComponent())
                parent->repaint();
        }

    private:
        GuiService& gui;
        Node node;
        bool monitorInputs = false;
        OwnedArray<Column> columns;
    };

    void layoutGroups()
    {
        int x = 0;
        const int h = jmax (50, viewport.getMaximumVisibleHeight() - viewport.getScrollBarThickness());
        for (auto* group : groups)
        {
            group->setBounds (x, 0, group->preferredWidth(), h);
            x += group->preferredWidth();
        }

        inner.setSize (jmax (x, viewport.getMaximumVisibleWidth()), h);

        // the scale spans the same area as the column meters
        const int top = headerHeight + 2;
        scale.setBounds (scaleArea.getX(), top, scaleArea.getWidth(), jmax (0, h - headerHeight - 4 - readoutHeight));
    }

    void timerCallback() override
    {
        for (auto* group : groups)
            group->update();
    }

    GuiService& gui;
    Component inner;
    Viewport viewport;
    DecibelScale scale;
    Rectangle<int> scaleArea;
    OwnedArray<Group> groups;
    Array<uint32> shownIds;
};

/** Whether the meter bridge is shown in the graph mixer (kept while the app runs). */
static bool sShowMixerMeterBridge = true;
static bool sShowMixerOverview = false;

class GraphMixerView::Content : public Component,
                                public DragAndDropContainer
{
public:
    Content (GraphMixerView& v, GuiService& gui, Session* sess)
        : ui (gui), session (sess), view (v)
    {
        setOpaque (true);
        addAndMakeVisible (box);

        meterView.willBecomeActive();
        addAndMakeVisible (meterView);
        meterView.initializeView (gui.services());
        meterView.didBecomeActive();
        meterView.meterBridge().setVuStyle (true);
        meterView.meterBridge().setMeterSize (26);
        meterView.onConfigChanged = [this]() { resized(); };

        addAndMakeVisible (meterToggle);
        meterToggle.setTooltip ("Show or hide the meter bridge");
        meterToggle.setClickingTogglesState (true);
        meterToggle.setToggleState (sShowMixerMeterBridge, dontSendNotification);
        meterToggle.onClick = [this]() {
            sShowMixerMeterBridge = meterToggle.getToggleState();
            resized();
            updateBottomBridge();
        };

        overview.reset (new ChannelOverview (gui));
        addChildComponent (overview.get());
        addAndMakeVisible (overviewToggle);
        overviewToggle.setTooltip ("Show every channel of every node");
        overviewToggle.setClickingTogglesState (true);
        overviewToggle.setToggleState (sShowMixerOverview, dontSendNotification);
        overviewToggle.onClick = [this]() {
            sShowMixerOverview = overviewToggle.getToggleState();
            refreshOverview();
            resized();
        };
        box.setRowHeight (ChannelStripComponent::widthForChannels (2));
        model.reset (new GraphMixerListBoxModel (gui, box));
        box.setModel (model.get());
        box.updateContent();

        _conns.push_back (ui.nodeSelected.connect ([this] { onNodeSelected(); }));
        auto& ssrv = *gui.sibling<SessionService>();
        _conns.push_back (ssrv.sigSessionLoaded.connect ([this]() {
            model->refreshNodes();
            model->setNode (session->getActiveGraph());
        }));
    }

    /** While the mixer shows its own meter bridge, the bottom one is collapsed. */
    void updateBottomBridge()
    {
        if (auto* found = findParentComponentOfClass<StandardContent>())
            standard = found;

        if (standard != nullptr)
            standard->setMeterBridgeSuppressed (isShowing() && sShowMixerMeterBridge);
    }

    void parentHierarchyChanged() override { updateBottomBridge(); }
    void visibilityChanged() override { updateBottomBridge(); }

    ~Content()
    {
        if (standard != nullptr)
            standard->setMeterBridgeSuppressed (false);

        for (auto& c : _conns)
            c.disconnect();
        _conns.clear();

        box.setModel (nullptr);
        model.reset();
    }

    void onNodeSelected()
    {
        model->setNode (ui.getSelectedNode());
        model->refreshNodes();
        box.updateContent();
        updateStripWidth();
        refreshOverview();
        resized();
        repaint();
    }

    /** Widens every strip to fit the node with the most channels. */
    void updateStripWidth()
    {
        int widest = 2;
        for (const auto& n : model->getNodes())
            if (ProcessorPtr p = n.getObject())
                widest = jmax (widest, jmin (NodeChannelStripComponent::maxMeterChannels,
                                             jmax (p->getNumAudioInputs(), p->getNumAudioOutputs())));

        const int width = ChannelStripComponent::widthForChannels (widest);
        if (width != box.getRowHeight())
        {
            box.setRowHeight (width);
            box.updateContent();
        }
    }

    /** Feeds the overview the nodes the strips are showing. */
    void refreshOverview()
    {
        if (sShowMixerOverview && model != nullptr)
            overview->setNodes (model->getNodes());
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto side = r.removeFromRight (toggleColumnWidth);
        meterToggle.setBounds (side.removeFromTop (26).reduced (2, 1));
        overviewToggle.setBounds (side.removeFromTop (26).reduced (2, 1));

        const bool showBridge = sShowMixerMeterBridge && (model == nullptr || ! model->isUnloaded());
        meterView.setVisible (showBridge);
        if (showBridge)
            meterView.setBounds (r.removeFromRight (jmax (40, meterView.meterBridge().preferredWidth() + 8)));

        const bool showOverview = sShowMixerOverview && (model == nullptr || ! model->isUnloaded());
        box.setVisible (! showOverview);
        overview->setVisible (showOverview);
        box.setBounds (r);
        overview->setBounds (r);
    }

    void paint (Graphics& g) override
    {
        g.setColour (Colors::contentBackgroundColor);
        g.fillAll();

        if (model->getNumRows() <= 0)
        {
            g.setColour (Colors::textColor);
            g.setFont (Font (FontOptions (15.f)));
            g.drawText (model->isUnloaded() ? TRANS ("Graph unloaded") : TRANS ("No channels to display"),
                        getLocalBounds().toFloat(),
                        Justification::centred);
        }
    }

    void stabilize()
    {
        model->refreshNodes();
        box.updateContent();
        updateStripWidth();
        refreshOverview();
        resized();
        repaint();
    }

private:
    static constexpr int toggleColumnWidth = 40;
    UI& ui;
    SessionPtr session;
    [[maybe_unused]] GraphMixerView& view;
    std::unique_ptr<GraphMixerListBoxModel> model;
    HorizontalListBox box;
    MeterBridgeView meterView;
    TextButton meterToggle { "VU" };
    TextButton overviewToggle { "ALL" };
    std::unique_ptr<ChannelOverview> overview;
    Component::SafePointer<StandardContent> standard;
    std::vector<SignalConnection> _conns;
};

GraphMixerView::GraphMixerView()
{
    setName (EL_VIEW_GRAPH_MIXER);
}

GraphMixerView::~GraphMixerView()
{
    content = nullptr;
}

void GraphMixerView::resized()
{
    if (content)
        content->setBounds (getLocalBounds());
}

void GraphMixerView::stabilizeContent()
{
    if (content)
        content->stabilize();
}

void GraphMixerView::initializeView (Services& app)
{
    content.reset (new Content (*this, *app.find<GuiService>(), app.context().session()));
    addAndMakeVisible (content.get());
    content->stabilize();
}

} // namespace element
