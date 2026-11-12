// Copyright 2019-2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <element/engine.hpp>
#include <element/services.hpp>
#include <element/ui/style.hpp>
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

/** Whether the meter bridge is shown in the graph mixer (kept while the app runs). */
static bool sShowMixerMeterBridge = true;

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
        box.setRowHeight (80);
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
        box.updateContent();
        resized();
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        meterToggle.setBounds (r.removeFromRight (24).removeFromTop (28).reduced (1));

        const bool showBridge = sShowMixerMeterBridge && (model == nullptr || ! model->isUnloaded());
        meterView.setVisible (showBridge);
        if (showBridge)
            meterView.setBounds (r.removeFromRight (jmax (40, meterView.meterBridge().preferredWidth() + 8)));

        box.setBounds (r);
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
        resized();
        repaint();
    }

private:
    UI& ui;
    SessionPtr session;
    [[maybe_unused]] GraphMixerView& view;
    std::unique_ptr<GraphMixerListBoxModel> model;
    HorizontalListBox box;
    MeterBridgeView meterView;
    TextButton meterToggle { "VU" };
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
