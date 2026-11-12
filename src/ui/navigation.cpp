// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <element/context.hpp>
#include <element/ui/style.hpp>
#include <element/ui/commands.hpp>
#include <element/ui/navigation.hpp>
#include <vector>

#include "ui/datapathbrowser.hpp"
#include "ui/nodeeditorview.hpp"
#include "ui/graphsettingsview.hpp"
#include "ui/nodepropertiesview.hpp"
#include "ui/pluginspanelview.hpp"
#include "ui/sessiontreepanel.hpp"
#include "ui/viewhelpers.hpp"

namespace element {

namespace {
constexpr int minPanelSize = 48;
constexpr int defaultOpenSize = 160;
} // namespace

class NavigationConcertinaPanel::Header : public Component
{
public:
    Header (NavigationConcertinaPanel& _parent, Component& _panel)
        : parent (_parent), panel (_panel)
    {
        setRepaintsOnMouseActivity (true);
    }

    virtual ~Header() {}

    virtual void resized() override {}

    virtual void paint (Graphics& g) override
    {
        const auto area = getLocalBounds();
        g.fillAll (Colour (0xff212121));

        const Colour bkg (Colors::widgetBackgroundColor.brighter (0.1f));
        g.setColour (bkg.contrasting().withAlpha (0.04f));
        g.fillRect (area.withHeight (1));
        g.fillRect (area.withTop (area.getBottom() - 1));

        g.setColour (findColour (Label::textColourId));
        const float h = (float) area.getHeight();
        const float s = h * 0.22f;
        Path arrow;
        if (panel.isVisible())
            arrow.addTriangle (-s, -s * 0.5f, s, -s * 0.5f, 0.f, s * 0.6f);
        else
            arrow.addTriangle (-s * 0.5f, -s, -s * 0.5f, s, s * 0.6f, 0.f);
        g.fillPath (arrow, AffineTransform::translation (h * 0.5f, h * 0.5f));

        g.setFont (Font (FontOptions (h * 0.62f).withStyle ("Bold")));
        g.drawFittedText (panel.getName().toUpperCase(),
                          (int) h,
                          0,
                          area.getWidth() - (int) h,
                          area.getHeight(),
                          Justification::centredLeft,
                          1);
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (! dragging && e.getDistanceFromDragStart() < 5)
            return;
        dragging = true;
        parent.headerDragged (&panel, e.getEventRelativeTo (&parent).getPosition().y);
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (dragging)
        {
            dragging = false;
            parent.headerDropped (&panel);
        }
        else if (e.getNumberOfClicks() == 1)
        {
            parent.togglePanel (&panel);
        }
    }

protected:
    NavigationConcertinaPanel& parent;
    Component& panel;
    bool dragging = false;
};

//====
/** The divider below a panel. Dragging it moves the boundary between this
    panel and the next open one, and nothing else. */
class NavigationConcertinaPanel::Sash : public Component
{
public:
    Sash (NavigationConcertinaPanel& o, Component& p) : owner (o), panel (p)
    {
        setMouseCursor (MouseCursor::UpDownResizeCursor);
        setRepaintsOnMouseActivity (true);
    }

    void paint (Graphics& g) override
    {
        if (isMouseOverOrDragging())
            g.fillAll (Colors::elemental.withAlpha (0.4f));
    }

    void mouseDown (const MouseEvent&) override
    {
        upper = owner.indexOfEntry (&panel);
        lower = upper >= 0 ? owner.nextOpenAfter (upper) : -1;
        if (lower < 0)
            return;
        startUpper = owner.entries[(size_t) upper]->size;
        startLower = owner.entries[(size_t) lower]->size;
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (lower >= 0)
            owner.resizeAround (upper, lower, e.getDistanceFromDragStartY(), startUpper, startLower);
    }

private:
    NavigationConcertinaPanel& owner;
    Component& panel;
    int upper = -1, lower = -1, startUpper = 0, startLower = 0;
};

class NavigationConcertinaPanel::ElementsHeader : public Header,
                                                  public Button::Listener
{
public:
    ElementsHeader (NavigationConcertinaPanel& _parent, Component& _panel)
        : Header (_parent, _panel)
    {
        addAndMakeVisible (addButton);
        addButton.setIcon (Icon (getIcons().falBars, Colours::white));
        addButton.setTriggeredOnMouseDown (true);
        addButton.addListener (this);
    }

    void resized() override
    {
        const int padding = 2;
        const int buttonSize = getHeight() - (padding * 2);
        addButton.setBounds (getWidth() - padding - buttonSize,
                             padding,
                             buttonSize,
                             buttonSize);
    }

    static void menuInvocationCallback (int chosenItemID, ElementsHeader* header)
    {
        if (chosenItemID > 0 && header)
            header->menuCallback (chosenItemID);
    }

    void menuCallback (int menuId)
    {
        if (1 == menuId)
        {
            ViewHelpers::invokeDirectly (this, Commands::showSessionConfig, true);
        }
        else if (2 == menuId)
        {
            ViewHelpers::invokeDirectly (this, Commands::sessionAddGraph, true);
        }
    }

    void buttonClicked (Button*) override
    {
        PopupMenu menu;
        menu.addItem (1, "Session Settings...");
        menu.addSeparator();
        menu.addItem (2, "Add Graph");
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&addButton),
                            ModalCallbackFunction::forComponent (menuInvocationCallback, this));
    }

private:
    IconButton addButton;
};

//====
class NavigationConcertinaPanel::UserDataPathHeader : public Header,
                                                      public Button::Listener
{
public:
    UserDataPathHeader (NavigationConcertinaPanel& _parent, DataPathTreeComponent& _panel)
        : Header (_parent, _panel), tree (_panel)
    {
        addAndMakeVisible (addButton);
        addButton.setButtonText ("+");
        addButton.addListener (this);
        addButton.setTriggeredOnMouseDown (true);
    }

    void resized() override
    {
        const int padding = 4;
        const int buttonSize = getHeight() - (padding * 2);
        addButton.setBounds (getWidth() - padding - buttonSize,
                             padding,
                             buttonSize,
                             buttonSize);
    }

    void buttonClicked (Button*) override
    {
        PopupMenu menu;
        menu.addItem (1, "Refresh...");
        menu.addSeparator();
#if JUCE_MAC
        String name = "Show in Finder";
#else
        String name = "Show in Explorer";
#endif
        menu.addItem (2, name);
        const int res = menu.show();
        if (res == 1)
        {
            tree.refresh();
        }
        else if (res == 2)
        {
            File file = tree.getSelectedFile();
            if (! file.exists())
                file = file.getParentDirectory();
            if (! file.exists())
                file = tree.getDirectory();
            if (file.exists())
                file.revealToUser();
        }
    }

private:
    DataPathTreeComponent& tree;
    TextButton addButton;
};

//====
NavigationConcertinaPanel::NavigationConcertinaPanel (Context& g)
    : globals (g), headerHeight (22), defaultPanelHeight (80)
{
    registerBuiltInPanels();
}

NavigationConcertinaPanel::~NavigationConcertinaPanel()
{
    clearPanels();
    setLookAndFeel (nullptr);
}

Component* NavigationConcertinaPanel::findPanelByName (const String& name)
{
    for (int i = 0; i < getNumPanels(); ++i)
        if (getPanel (i)->getName() == name)
            return getPanel (i);
    return 0;
}

void NavigationConcertinaPanel::saveState (PropertiesFile* props)
{
    ValueTree state (tags::state);

    for (int i = 0; i < getNumPanels(); ++i)
    {
        ValueTree item ("item");
        auto* const panel = getPanel (i);
        item.setProperty ("index", i, 0)
            .setProperty ("name", panel->getName(), 0)
            .setProperty ("h", entries[(size_t) i]->size, 0);

        if (auto* ned = dynamic_cast<NodeEditorView*> (panel))
            item.setProperty ("sticky", ned->isSticky(), nullptr);

        state.addChild (item, -1, 0);
    }

    if (auto xml = state.createXml())
        props->setValue ("ccNavPanel", xml.get());
}

void NavigationConcertinaPanel::restoreState (PropertiesFile* props)
{
    if (auto xml = props->getXmlValue ("ccNavPanel"))
    {
        ValueTree state = ValueTree::fromXml (*xml);
        size_t position = 0;
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            auto item (state.getChild (i));
            auto* const c = findPanelByName (item["name"].toString().trim());
            if (c == nullptr)
                continue;

            if (auto* ned = dynamic_cast<NodeEditorView*> (c))
                ned->setSticky ((bool) item.getProperty ("sticky", ned->isSticky()));

            const auto from = (size_t) indexOfEntry (c);
            std::rotate (entries.begin() + (std::ptrdiff_t) position,
                         entries.begin() + (std::ptrdiff_t) from,
                         entries.begin() + (std::ptrdiff_t) from + 1);

            auto& entry = *entries[position++];
            entry.size = std::max (0, (int) item["h"]);
            if (entry.size > 0)
            {
                entry.size = std::max (minPanelSize, entry.size);
                entry.lastSize = entry.size;
            }
        }

        syncRegisteredOrder();
        layoutPanels();
    }
}

int NavigationConcertinaPanel::getIndexOfPanel (Component* panel)
{
    return indexOfEntry (panel);
}

int NavigationConcertinaPanel::indexOfEntry (const Component* panel) const noexcept
{
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i]->panel.get() == panel || entries[i]->header.get() == panel)
            return (int) i;
    return -1;
}

Component* NavigationConcertinaPanel::getPanel (int index) const noexcept
{
    return isPositiveAndBelow (index, getNumPanels()) ? entries[(size_t) index]->panel.get() : nullptr;
}

void NavigationConcertinaPanel::setPanelSize (Component* panel, int height, bool)
{
    const auto index = indexOfEntry (panel);
    if (index < 0)
        return;

    auto& entry = *entries[(size_t) index];
    if (entry.size > 0)
        entry.lastSize = entry.size;
    entry.size = height > 0 ? std::max (minPanelSize, height) : 0;
    if (entry.size > 0)
        entry.lastSize = entry.size;
    layoutPanels();
}

void NavigationConcertinaPanel::expandPanel (Component* panel)
{
    const auto index = indexOfEntry (panel);
    if (index >= 0 && entries[(size_t) index]->size == 0)
        setPanelSize (panel, std::max (entries[(size_t) index]->lastSize, defaultOpenSize));
}

void NavigationConcertinaPanel::togglePanel (Component* panel)
{
    const auto index = indexOfEntry (panel);
    if (index < 0)
        return;
    if (entries[(size_t) index]->size > 0)
        setPanelSize (panel, 0);
    else
        expandPanel (panel);
}

int NavigationConcertinaPanel::nextOpenAfter (int index) const noexcept
{
    for (int i = index + 1; i < getNumPanels(); ++i)
        if (entries[(size_t) i]->size > 0)
            return i;
    return -1;
}

void NavigationConcertinaPanel::resizeAround (int upper, int lower, int delta, int startUpper, int startLower)
{
    const int total = startUpper + startLower;
    const int newUpper = jlimit (minPanelSize, std::max (minPanelSize, total - minPanelSize), startUpper + delta);
    entries[(size_t) upper]->size = newUpper;
    entries[(size_t) lower]->size = total - newUpper;
    entries[(size_t) upper]->lastSize = entries[(size_t) upper]->size;
    entries[(size_t) lower]->lastSize = entries[(size_t) lower]->size;
    layoutPanels();
}

void NavigationConcertinaPanel::layoutPanels()
{
    const int num = getNumPanels();
    if (num == 0)
        return;

    int total = 0, open = 0;
    for (const auto& e : entries)
    {
        if (e->size > 0)
        {
            total += e->size;
            ++open;
        }
    }

    // Open panels share what is left after the headers, in proportion to their sizes.
    const int available = std::max (0, getHeight() - num * headerHeight);
    std::vector<int> heights ((size_t) num, 0);
    if (open > 0 && available > 0)
    {
        int used = 0, last = -1;
        for (int i = 0; i < num; ++i)
        {
            if (entries[(size_t) i]->size > 0)
            {
                heights[(size_t) i] = std::max (minPanelSize, roundToInt (entries[(size_t) i]->size * (double) available / total));
                used += heights[(size_t) i];
                last = i;
            }
        }

        heights[(size_t) last] = std::max (minPanelSize, heights[(size_t) last] + available - used);

        // Only remember the fitted sizes when there is room for every open panel.
        if (available >= open * minPanelSize)
            for (int i = 0; i < num; ++i)
                if (heights[(size_t) i] > 0)
                    entries[(size_t) i]->size = heights[(size_t) i];
    }

    const int width = getWidth();
    int y = 0;
    for (int i = 0; i < num; ++i)
    {
        auto& entry = *entries[(size_t) i];
        entry.header->setBounds (0, y, width, headerHeight);
        y += headerHeight;

        const int h = heights[(size_t) i];
        entry.panel->setVisible (h > 0);
        entry.panel->setBounds (0, y, width, h);
        y += h;

        const bool hasSash = h > 0 && nextOpenAfter (i) >= 0;
        entry.sash->setVisible (hasSash);
        if (hasSash)
        {
            entry.sash->setBounds (0, y - 3, width, 6);
            entry.sash->toFront (false);
        }
        entry.header->repaint();
    }
}

void NavigationConcertinaPanel::headerDragged (Component* panel, int y)
{
    if (indexOfEntry (panel) < 0)
        return;

    int target = getNumPanels();
    for (int i = 0; i < getNumPanels(); ++i)
    {
        if (y < entries[(size_t) i]->header->getBounds().getCentreY())
        {
            target = i;
            break;
        }
    }

    if (target != dropIndex)
    {
        dropIndex = target;
        repaint();
    }
}

void NavigationConcertinaPanel::headerDropped (Component* panel)
{
    const auto from = indexOfEntry (panel);
    auto to = dropIndex;
    dropIndex = -1;

    if (from >= 0 && to >= 0)
    {
        if (to > from)
            --to;
        if (to != from)
        {
            auto moved = std::move (entries[(size_t) from]);
            entries.erase (entries.begin() + from);
            entries.insert (entries.begin() + to, std::move (moved));
            syncRegisteredOrder();
        }
    }

    layoutPanels();
    repaint();
}

void NavigationConcertinaPanel::syncRegisteredOrder()
{
    std::vector<RegisteredPanel> ordered;
    for (const auto& entry : entries)
    {
        const auto it = std::find_if (registered.begin(), registered.end(), [&entry] (const RegisteredPanel& r) {
            return r.desc.name == entry->panel->getName();
        });
        if (it != registered.end())
        {
            ordered.push_back (std::move (*it));
            registered.erase (it);
        }
    }

    for (auto& r : registered)
        ordered.push_back (std::move (r));
    registered = std::move (ordered);
    for (auto& r : registered)
        r.desc.index = -1;
}

//====

void NavigationConcertinaPanel::insertPanel (juce::Component* comp, int index)
{
    addPanelInternal (index, comp);
}

void NavigationConcertinaPanel::showPanel (const juce::String& name)
{
    setPanelHidden (name, false);
}

void NavigationConcertinaPanel::hidePanel (const juce::String& name)
{
    setPanelHidden (name, true);
}

void NavigationConcertinaPanel::setPanelHidden (const juce::String& name, bool hidden)
{
    bool changed = false;
    for (auto& entry : registered)
    {
        if (entry.desc.name == name && entry.desc.hidden != hidden)
        {
            entry.desc.hidden = hidden;
            changed = true;
        }
    }

    if (changed)
        updateContent();
}

void NavigationConcertinaPanel::setPanelName (const String& panel, const String& newName)
{
    for (auto& entry : registered)
        if (entry.desc.name == panel)
            entry.desc.name = newName;

    if (auto ptr = findPanelByName (panel))
    {
        ptr->setName (newName);
        repaint();
    }
}

void NavigationConcertinaPanel::clearPanels()
{
    entries.clear();
}

void NavigationConcertinaPanel::registerBuiltInPanels()
{
    // Register only; updateContent() creates them. Going through addPanel() here
    // would construct each panel eagerly, only for the first updateContent() to
    // clear and rebuild them.
    auto reg = [this] (PanelDescription desc, PanelFactory factory, PanelHeaderFactory header = nullptr) {
        registered.push_back ({ std::move (desc), std::move (factory), std::move (header) });
    };

    reg (
        { "Session" },
        [] { return new SessionTreePanel(); },
        [this] (Component& panel) { return new ElementsHeader (*this, panel); });

    reg ({ "Graph" }, [] {
        auto* gv = new GraphSettingsView();
        gv->setGraphButtonVisible (false);
        gv->setUpdateOnActiveGraphChange (true);
        gv->setPropertyPanelHeaderVisible (false);
        return gv;
    });

    reg ({ "Node" }, [] { return new NodePropertiesView(); });

    reg ({ "Editor" }, [] { return new NodeEditorView(); });

    reg ({ "Plugins" }, [this] { return new PluginsPanelView (globals.plugins()); });

    reg (
        { "Data Path", "UserDataPath" },
        [] {
            auto* dp = new DataPathTreeComponent();
            dp->getFileTree().setDragAndDropDescription ("ccNavConcertinaPanel");
            return dp;
        },
        [this] (Component& panel) {
            return new UserDataPathHeader (*this, static_cast<DataPathTreeComponent&> (panel));
        });
}

void NavigationConcertinaPanel::updateContent()
{
    clearPanels();
    createRegisteredPanels();
}

void NavigationConcertinaPanel::createRegisteredPanels()
{
    for (const auto& entry : registered)
        createPanel (entry);
}

void NavigationConcertinaPanel::createPanel (const RegisteredPanel& entry)
{
    if (entry.desc.hidden || ! entry.factory)
        return;

    auto* comp = entry.factory();
    if (comp == nullptr)
        return;

    comp->setComponentID (entry.desc.componentID.isNotEmpty() ? entry.desc.componentID
                                                              : entry.desc.name);

    Component* header = entry.header ? entry.header (*comp) : nullptr;
    addPanelInternal (entry.desc.index, comp, entry.desc.name, header);

    if (entry.desc.preferredSize > 0)
        setPanelSize (comp, entry.desc.preferredSize, false);
}

void NavigationConcertinaPanel::addPanel (PanelDescription desc, PanelFactory factory, PanelHeaderFactory header)
{
    if (! factory || findPanelByName (desc.name) != nullptr)
        return;

    registered.push_back ({ std::move (desc), std::move (factory), std::move (header) });
    createPanel (registered.back());
}

void NavigationConcertinaPanel::removePanel (const String& name)
{
    std::erase_if (registered, [&name] (const RegisteredPanel& p) { return p.desc.name == name; });
    if (const auto index = indexOfEntry (findPanelByName (name)); index >= 0)
    {
        entries.erase (entries.begin() + index);
        layoutPanels();
    }
}

const StringArray& NavigationConcertinaPanel::getNames() const { return names; }
const int NavigationConcertinaPanel::getHeaderHeight() const { return headerHeight; }

void NavigationConcertinaPanel::setHeaderHeight (const int newHeight)
{
    if (newHeight == headerHeight)
        return;
    jassert (newHeight > 0);
    headerHeight = newHeight;
    layoutPanels();
}

void NavigationConcertinaPanel::addPanelInternal (const int index,
                                                  Component* comp,
                                                  const String& name,
                                                  Component* header)
{
    jassert (comp);
    if (name.isNotEmpty())
        comp->setName (name);

    auto entry = std::make_unique<Entry>();
    entry->panel.reset (comp);
    if (nullptr == header)
        header = new Header (*this, *comp);
    entry->header.reset (header);
    entry->sash = std::make_unique<Sash> (*this, *comp);

    addChildComponent (comp);
    addAndMakeVisible (header);
    addChildComponent (entry->sash.get());

    const auto position = isPositiveAndBelow (index, getNumPanels()) ? index : getNumPanels();
    entries.insert (entries.begin() + position, std::move (entry));
    layoutPanels();
}

void NavigationConcertinaPanel::paint (juce::Graphics& g)
{
    g.fillAll (element::Colors::backgroundColor);
}

void NavigationConcertinaPanel::paintOverChildren (juce::Graphics& g)
{
    if (dropIndex < 0)
        return;

    const int y = dropIndex < getNumPanels()
                      ? entries[(size_t) dropIndex]->header->getY()
                      : (entries.empty() ? 0 : entries.back()->panel->getBottom());
    g.setColour (element::Colors::elemental);
    g.fillRect (0, y - 1, getWidth(), 2);
}

void NavigationConcertinaPanel::resized()
{
    layoutPanels();
}

} // namespace element
