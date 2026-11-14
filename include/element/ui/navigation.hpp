// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <vector>

#include <element/juce/gui_basics.hpp>

class AudioIOPanelView; // FIXME: this class isn't in element namespace.

namespace element {

class Context;
class PluginsPanelView;
class DataPathTreeComponent;
class SessionTreePanel;

/** The navigation sidebar: a vertical stack of collapsible panels.

    Any number of panels can be open at once. Clicking a header opens or closes
    its panel, dragging the divider between two open panels resizes only those
    two, and dragging a header reorders the panels. */
class NavigationConcertinaPanel : public juce::Component {
public:
    NavigationConcertinaPanel (Context& g);
    ~NavigationConcertinaPanel();

    void saveState (juce::PropertiesFile* props);
    void restoreState (juce::PropertiesFile* props);

    int getIndexOfPanel (juce::Component* panel);
    Component* findPanelByName (const juce::String& name);
    void insertPanel (juce::Component* comp, int index = -1);
    void showPanel (const juce::String& name);
    void hidePanel (const juce::String& name);
    void setPanelName (const juce::String& panel, const juce::String& newName);

    int getNumPanels() const noexcept { return (int) entries.size(); }
    juce::Component* getPanel (int index) const noexcept;

    template <class T>
    inline T* findPanel()
    {
        for (int i = getNumPanels(); --i >= 0;)
            if (T* panel = dynamic_cast<T*> (getPanel (i)))
                return panel;
        return nullptr;
    }

    /** Sets the content height of a panel; 0 collapses it to its header.
        The panels share the available height, so the final size can differ. */
    void setPanelSize (juce::Component* panel, int height, bool animate = false);

    /** Opens a panel, leaving the other panels as they are. */
    void expandPanel (juce::Component* panel);

    /** Opens the panel if it is collapsed, collapses it otherwise. */
    void togglePanel (juce::Component* panel);

    /** Describes a navigation panel registered with the concertina. Every panel,
        including the built-ins, is created from one of these. */
    struct PanelDescription {
        juce::String name;        // identity, header label, and persistence key
        juce::String componentID; // component id; empty means "same as name"
        int index { -1 };         // position among panels; -1 appends in registration order
        int preferredSize { 0 };  // initial expanded height in px; 0 = collapsed to header
        bool hidden { false };    // initial visibility
    };

    /** Builds a fresh panel component on demand. */
    using PanelFactory = std::function<juce::Component*()>;

    /** Builds a custom header for a panel. Return nullptr to use the default header. */
    using PanelHeaderFactory = std::function<juce::Component*(juce::Component& panel)>;

    /** Register a navigation panel.

        A registered panel is (re)created by updateContent(), so it survives
        showPanel()/hidePanel() and other rebuilds, and persists by name. The panel
        is also created immediately, so no manual updateContent() call is required.

        @param desc     Identity, placement, and initial size/visibility for the panel.
        @param factory  Called to construct the panel component (Navigation takes ownership).
        @param header   Optional; builds a custom header for the panel (Navigation takes ownership).
    */
    void addPanel (PanelDescription desc, PanelFactory factory, PanelHeaderFactory header = nullptr);

    /** Unregister and remove a panel previously added with addPanel().

        @param name The name the panel was registered with.
    */
    void removePanel (const juce::String& name);

    void clearPanels();

    void updateContent();

    const juce::StringArray& getNames() const;
    const int getHeaderHeight() const;

    void setHeaderHeight (const int newHeight);

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;

private:
    Context& globals;
    int headerHeight;
    int defaultPanelHeight;

    juce::StringArray names;

    class Sash;
    struct Entry {
        std::unique_ptr<juce::Component> panel;
        std::unique_ptr<juce::Component> header;
        std::unique_ptr<Sash> sash;
        int size { 0 };     // content height in px, 0 when collapsed
        int lastSize { 0 }; // height to restore when opened again
    };
    std::vector<std::unique_ptr<Entry>> entries;
    int dropIndex { -1 }; // insert position shown while a header is dragged

    int indexOfEntry (const juce::Component* panel) const noexcept;
    void layoutPanels();
    void resizeAround (int upper, int lower, int delta, int startUpper, int startLower);
    int nextOpenAfter (int index) const noexcept;
    void headerDragged (juce::Component* panel, int yInThis);
    void headerDropped (juce::Component* panel);
    void syncRegisteredOrder();
    void addPanelInternal (const int index,
                           juce::Component* comp,
                           const juce::String& name = juce::String(),
                           juce::Component* header = nullptr);

    struct RegisteredPanel {
        PanelDescription desc;
        PanelFactory factory;
        PanelHeaderFactory header;
    };
    std::vector<RegisteredPanel> registered;
    void registerBuiltInPanels();
    void createRegisteredPanels();
    void createPanel (const RegisteredPanel& panel);
    void setPanelHidden (const juce::String& name, bool hidden);

    class Header;
    class ElementsHeader;
    class UserDataPathHeader;
    class LookAndFeel;
};

} // namespace element
