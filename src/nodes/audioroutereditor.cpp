// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <element/ui/style.hpp>

#include "nodes/audiorouter.hpp"
#include "nodes/audioroutereditor.hpp"
#include "ui/patchmatrix.hpp"
#include "common.hpp"

using namespace juce;

namespace element {

class AudioRouterMatrix : public PatchMatrixComponent
{
public:
    AudioRouterMatrix (AudioRouterEditor& ed)
        : editor (ed)
    {
        setMatrixCellSize (48);
        setSize (getRowThickness() * 4,
                 getColumnThickness() * 4);
        setRepaintsOnMouseActivity (true);
    }

    int getNumColumns() override { return editor.getMatrixState().getNumColumns(); }
    int getNumRows() override { return editor.getMatrixState().getNumRows(); }

    void paintMatrixCell (Graphics& g, const int width, const int height, const int row, const int column) override
    {
        auto& matrix = editor.getMatrixState();
        const int gridPadding = 1;
        bool useHighlighting = true;

        if (useHighlighting && (mouseIsOverCell (row, column) && ! matrix.connected (row, column)))
        {
            g.setColour (Colors::elemental.withAlpha (0.4f));
            g.fillRect (0, 0, width - gridPadding, height - gridPadding);
        }
        else if ((mouseIsOverRow (row) || mouseIsOverColumn (column)) && ! matrix.connected (row, column))
        {
            g.setColour (Colors::elemental.withAlpha (0.3f));
            g.fillRect (0, 0, width - gridPadding, height - gridPadding);
        }
        else
        {
            g.setColour (matrix.connected (row, column) ? Colour (Colors::elemental.brighter()) : Colour (LookAndFeel_E1::defaultMatrixCellOffColor));

            g.fillRect (0, 0, width - gridPadding, height - gridPadding);
        }

        if (matrix.connected (row, column))
        {
            const auto db = editor.getGainDb (row, column);
            if (std::abs (db) >= 0.05f)
            {
                g.setColour (Colours::white);
                g.setFont (11.0f);
                g.drawText (String (db, 1), 0, 0, width - gridPadding, height - gridPadding, Justification::centred, false);
            }
        }
    }

    void matrixCellClicked (const int row, const int col, const MouseEvent& ev) override
    {
        auto& matrix = editor.getMatrixState();

        if (ev.mods.isPopupMenu())
        {
            if (! matrix.connected (row, col))
                return;

            PopupMenu menu;
            const float levels[] = { 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -18.0f };
            for (int i = 0; i < (int) std::size (levels); ++i)
                menu.addItem (i + 1, String (levels[i], 0) + " dB", true, std::abs (editor.getGainDb (row, col) - levels[i]) < 0.05f);

            menu.showMenuAsync (PopupMenu::Options(), [safe = Component::SafePointer<AudioRouterMatrix> (this), row, col, levels] (int result) {
                if (safe != nullptr && result > 0)
                {
                    safe->editor.setGainDb (row, col, levels[result - 1]);
                    safe->repaint();
                }
            });
            return;
        }

        matrix.toggleCell (row, col);
        editor.applyMatrix();
        repaint();
    }

    void mouseWheelMove (const MouseEvent& ev, const MouseWheelDetails& wheel) override
    {
        const int row = getRowForPixel (ev.y);
        const int col = getColumnForPixel (ev.x);
        if (row < 0 || col < 0 || row >= getNumRows() || col >= getNumColumns()
            || ! editor.getMatrixState().connected (row, col))
            return;

        const float step = ev.mods.isShiftDown() ? 0.1f : 0.5f;
        editor.setGainDb (row, col, editor.getGainDb (row, col) + (wheel.deltaY > 0 ? step : -step));
        repaint();
    }

    void matrixBackgroundClicked (const MouseEvent& ev) override {}

    void matrixHoveredCellChanged (const int prevRow, const int prevCol, const int newRow, const int newCol) override
    {
        ignoreUnused (prevRow, prevCol, newRow, newCol);
        repaint();
    }

private:
    AudioRouterEditor& editor;
};

class AudioRouterSizeButton : public TextButton
{
public:
    AudioRouterSizeButton (AudioRouterEditor& o)
        : owner (o)
    {
        stabilizeContent();

        onClick = [this]() {
            PopupMenu menu;
            menu.addItem (presetDownmix51, "5.1 to Stereo downmix");
            menu.addItem (presetDownmix51Sub, "5.1 to Stereo + Sub");
            menu.addSeparator();
            // item id encodes the size: ins * 100 + outs
            const std::pair<int, int> sizes[] = { { 2, 2 }, { 4, 4 }, { 6, 2 }, { 6, 6 }, { 8, 2 }, { 8, 8 }, { 10, 10 }, { 12, 12 }, { 16, 16 } };
            for (const auto& [ins, outs] : sizes)
                menu.addItem (ins * 100 + outs, String (ins) + "x" + String (outs));
            menu.showMenuAsync (PopupMenu::Options()
                                    .withTargetComponent (this),
                                ModalCallbackFunction::create (sizeChosen, WeakReference<AudioRouterSizeButton> (this)));
        };
    }

    ~AudioRouterSizeButton()
    {
        masterReference.clear();
    }

    static constexpr int presetDownmix51 = 1;
    static constexpr int presetDownmix51Sub = 2;

    /** Called with ins * 100 + outs, or presetDownmix51. */
    std::function<void (int)> onAudioRouterSizeChanged;

    void stabilizeContent()
    {
        setButtonText ("Size");
        // setButtonText (owner.getSizeString());
    }

private:
    JUCE_DECLARE_WEAK_REFERENCEABLE (AudioRouterSizeButton)

    AudioRouterEditor& owner;
    void handleSizeResult (int r)
    {
        if (r > 0 && onAudioRouterSizeChanged)
            onAudioRouterSizeChanged (r);
        stabilizeContent();
    }

    static void sizeChosen (int code, WeakReference<AudioRouterSizeButton> me)
    {
        if (me != nullptr)
            me->handleSizeResult (code);
    }
};

class AudioRouterEditor::Content : public Component
{
public:
    Content (AudioRouterEditor& o)
        : owner (o)
    {
        setOpaque (true);
        matrix.reset (new AudioRouterMatrix (o));
        addAndMakeVisible (matrix.get());

        sizeButton.reset (new AudioRouterSizeButton (o));
        addAndMakeVisible (sizeButton.get());
        sizeButton->onAudioRouterSizeChanged = [this] (int code) {
            if (code == AudioRouterSizeButton::presetDownmix51)
                owner.applyDownmix51();
            else if (code == AudioRouterSizeButton::presetDownmix51Sub)
                owner.applyDownmix51 (true);
            else
                owner.setRouterSize (code / 100, code % 100);
        };

        // addAndMakeVisible (slider);
        slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (Slider::NoTextBox, true, 1, 1);
        slider.setRange (0.001, 2.0);

        slider.onValueChange = [this] { owner.setFadeLength (slider.getValue()); };

        setSize (padding + labelWidth + matrix->getWidth(),
                 padding + labelWidth + matrix->getHeight());
        matrixArea = { labelWidth, padding, matrix->getWidth(), matrix->getHeight() };
    }

    ~Content()
    {
        slider.onValueChange = nullptr;
    }

    void adjustBoundsToMatrixSize (int cellSize = 0)
    {
        cellSize = cellSize > 0 ? cellSize : 36;
        matrix->setMatrixCellSize (cellSize, cellSize);

        setSize (padding + labelWidth + (matrix->getNumColumns() * matrix->getColumnThickness()),
                 padding + labelWidth + (matrix->getNumRows() * matrix->getRowThickness()));
    }

    void resized() override
    {
        auto size = jlimit (24, 36, roundToInt ((double) (getWidth() - labelWidth - 32) / (double) matrix->getNumRows()));
        matrix->setMatrixCellSize (size, size);

        matrixArea = { labelWidth, padding, matrix->getRowThickness() * matrix->getNumRows(), matrix->getColumnThickness() * matrix->getNumColumns() };

        matrix->setBounds (matrixArea);
        if (slider.isVisible())
            slider.setBounds (matrixArea.getX() - size + 2, matrixArea.getBottom() + 4, size - 2, size - 2);

        int btnH = 24;
        int btnW = 36;
        sizeButton->changeWidthToFitText (btnH);
        sizeButton->setBounds ((labelWidth / 2) - (btnW / 2),
                               matrixArea.getBottom() + (labelWidth / 2) - (btnH / 2),
                               btnW,
                               btnH);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colors::contentBackgroundColor);
        Rectangle<int> box (0, padding, labelWidth - padding, matrix->getHeight());
        auto rowThickness = matrix->getRowThickness();
        auto colThickness = matrix->getColumnThickness();

        g.setColour (Colors::textColor);
        for (int row = 0; row < owner.getMatrixState().getNumRows(); ++row)
            g.drawText (String ("Ch. ") + String (row + 1), box.removeFromTop (rowThickness), Justification::centredRight, false);

        box = { matrix->getX(), matrix->getBottom() + 10, matrix->getWidth(), 50 };

        for (int col = 0; col < owner.getMatrixState().getNumColumns(); ++col)
        {
            auto r = box.removeFromLeft (colThickness);
            g.setColour (Colors::textColor);
            Style::drawVerticalText (g, String ("Ch. ") + String (col + 1), r, Justification::centredRight);
        }
    }

private:
    friend class AudioRouterEditor;
    AudioRouterEditor& owner;
    int padding = 10;
    int labelWidth = 60;
    Rectangle<int> matrixArea;
    Slider slider;
    std::unique_ptr<AudioRouterSizeButton> sizeButton;
    std::unique_ptr<AudioRouterMatrix> matrix;
};

AudioRouterEditor::AudioRouterEditor (const Node& node)
    : NodeEditor (node)
{
    setOpaque (true);
    content.reset (new Content (*this));
    addAndMakeVisible (content.get());
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
    {
        changeListenerCallback (node); // initial gui state
        node->addChangeListener (this);
    }

    adjustBoundsToMatrixSize (32);
}

AudioRouterEditor::~AudioRouterEditor()
{
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
        node->removeChangeListener (this);
    content.reset();
}

void AudioRouterEditor::setRouterSize (int ins, int outs)
{
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
    {
        node->setSize (ins, outs);
        if (autoResize)
        {
            // matrix needs to be correct before adjusting overall size
            matrix = node->getMatrixState();
            adjustBoundsToMatrixSize (32);
        }
    }
}

float AudioRouterEditor::getGainDb (int row, int col) const
{
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
        return node->getGainDb (row, col);
    return 0.0f;
}

void AudioRouterEditor::setGainDb (int row, int col, float db)
{
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
        node->setGainDb (row, col, db);
}

void AudioRouterEditor::applyDownmix51 (bool withSub)
{
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
    {
        node->setDownmix51ToStereo (withSub);
        matrix = node->getMatrixState();
        if (autoResize)
            adjustBoundsToMatrixSize (32);
    }
}

void AudioRouterEditor::setFadeLength (double length)
{
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
        node->setFadeLength (length);
}

void AudioRouterEditor::applyMatrix()
{
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
        node->setMatrixState (matrix);
}

String AudioRouterEditor::getSizeString() const
{
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
        return node->getSizeString();
    return {};
}

void AudioRouterEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto* const node = getNodeObjectOfType<AudioRouterNode>())
    {
        matrix = node->getMatrixState();
        content->resized();
        content->repaint();
        content->matrix->repaint();
        content->sizeButton->stabilizeContent();
        resized();
    }
}

void AudioRouterEditor::adjustBoundsToMatrixSize (int cellSize)
{
    content->adjustBoundsToMatrixSize (cellSize);
    setSize (content->getWidth(), content->getHeight());
}

void AudioRouterEditor::resized()
{
    content->setBounds (getLocalBounds());
}

void AudioRouterEditor::paint (Graphics& g)
{
    g.fillAll (Colours::black);
}

} // namespace element
