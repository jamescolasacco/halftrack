#include "AutomationEditor.h"
#include "Session.h"
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstdlib>
#include <optional>
#if HALFTRACK_TESTS_ENABLED
#include <iostream>
#include <stdexcept>
#endif

namespace halftrack
{
namespace
{
constexpr double maximumBeat = 1000000.0;
const juce::Colour background (0xff222222), panel (0xff2d2d2d), ink (0xffe4e4e4), accent (0xffffa65a);
}

class AutomationEditor::Impl final : public juce::Component, private juce::ChangeListener,
                                     private juce::Timer, private juce::ScrollBar::Listener
{
public:
    class Curve final : public juce::Component
    {
    public:
        explicit Curve (Impl& editor) : owner (editor)
        { setName ("automation-curve"); setWantsKeyboardFocus (true); }
        juce::Rectangle<float> plot() const
        { return getLocalBounds().toFloat().withTrimmedLeft (66).withTrimmedRight (20).withTrimmedTop (25).withTrimmedBottom (36); }
        juce::Point<float> position (const Session::AutomationPoint& point) const
        {
            const auto bounds = plot();
            return { bounds.getX() + static_cast<float> ((point.beat - owner.viewStart) / owner.viewBeats) * bounds.getWidth(),
                     bounds.getBottom() - Session::automationValueToProportion (owner.target(), point.value) * bounds.getHeight() };
        }
        Session::AutomationPoint value (juce::Point<float> point, bool snap) const
        {
            const auto bounds = plot();
            auto beat = owner.viewStart + (point.x - bounds.getX()) / bounds.getWidth() * owner.viewBeats;
            if (snap && owner.snapBeats() > 0) beat = std::round (beat / owner.snapBeats()) * owner.snapBeats();
            return { std::clamp (beat, 0.0, maximumBeat), Session::automationProportionToValue (owner.target(),
                std::clamp ((bounds.getBottom() - point.y) / bounds.getHeight(), 0.0f, 1.0f)) };
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (background);
            const auto bounds = plot();
            if (bounds.isEmpty()) return;
            g.setColour (panel); g.fillRect (bounds);
            g.setFont (juce::FontOptions (11.0f));
            const auto isVolume = owner.target() == Session::AutomationTarget::volume;
            const std::vector<float> ticks = isVolume ? std::vector<float> { -100, -24, -12, -6, 0, 6 }
                                                     : std::vector<float> { -1, -.5f, 0, .5f, 1 };
            for (const auto tick : ticks)
            {
                const auto y = position ({ owner.viewStart, tick }).y;
                g.setColour (tick == 0 ? juce::Colour (0xff676767) : juce::Colour (0xff3e3e3e));
                g.drawHorizontalLine (juce::roundToInt (y), bounds.getX(), bounds.getRight());
                g.setColour (ink.withAlpha (.65f));
                g.drawText (owner.formatValue (tick), 0, juce::roundToInt (y) - 8, 56, 16, juce::Justification::centredRight);
            }
            auto spacing = 1.0;
            while (spacing * bounds.getWidth() / owner.viewBeats < 45) spacing *= 2;
            if (owner.viewBeats <= 4) spacing = .25;
            for (auto beat = std::ceil (owner.viewStart / spacing) * spacing;
                 beat <= std::min (maximumBeat, owner.viewStart + owner.viewBeats); beat += spacing)
            {
                const auto x = position ({ beat, 0 }).x;
                g.setColour (juce::Colour (0xff3e3e3e)); g.drawVerticalLine (juce::roundToInt (x), bounds.getY(), bounds.getBottom());
                g.setColour (ink.withAlpha (.65f));
                g.drawText (juce::String (beat, spacing < 1 ? 2 : 0), juce::roundToInt (x) - 24,
                            juce::roundToInt (bounds.getBottom()) + 9, 48, 18, juce::Justification::centred);
            }
            g.setColour (ink.withAlpha (.65f)); g.drawText ("BEATS", 6, getHeight() - 28, 50, 18, juce::Justification::centredRight);
            if (const auto lane = owner.lane())
            {
                juce::Graphics::ScopedSaveState save (g); g.reduceClipRegion (bounds.expanded (5).toNearestInt());
                const auto colour = lane->enabled ? accent : ink.withAlpha (.4f);
                juce::Path path;
                if (lane->points.empty())
                {
                    const auto y = position ({ 0, owner.manualValue() }).y;
                    path.startNewSubPath (bounds.getX(), y); path.lineTo (bounds.getRight(), y);
                }
                else
                {
                    path.startNewSubPath (bounds.getX(), position (lane->points.front()).y);
                    for (const auto& point : lane->points) path.lineTo (position (point));
                    path.lineTo (bounds.getRight(), position (lane->points.back()).y);
                }
                g.setColour (colour.withAlpha (owner.available() ? 1.0f : .35f));
                g.strokePath (path, juce::PathStrokeType (1.8f));
                for (size_t i = 0; i < lane->points.size(); ++i)
                {
                    const auto p = position (lane->points[i]);
                    if (p.x < bounds.getX() - 5 || p.x > bounds.getRight() + 5) continue;
                    g.setColour (static_cast<int> (i) == owner.selected ? ink : colour);
                    g.fillEllipse (p.x - 4, p.y - 4, 8, 8);
                    g.setColour (background); g.drawEllipse (p.x - 4, p.y - 4, 8, 8, 1);
                }
            }
            if (owner.snapshot.playing)
            {
                const auto x = position ({ owner.snapshot.position * owner.snapshot.tempo / 60, 0 }).x;
                if (x >= bounds.getX() && x <= bounds.getRight())
                { g.setColour (ink.withAlpha (.75f)); g.drawVerticalLine (juce::roundToInt (x), bounds.getY(), bounds.getBottom()); }
            }
        }
        void mouseDown (const juce::MouseEvent& event) override
        {
            owner.numericDirty = false; owner.finishGesture(); owner.synchronise();
            if (! owner.available() || ! plot().expanded (5).contains (event.position)) return;
            if (isShowing()) grabKeyboardFocus();
            const auto current = owner.lane(); if (! current) return;
            owner.selected = hit (event.position, *current);
            if (event.mods.isPopupMenu()) { owner.removeSelected(); return; }
            if (! event.mods.isLeftButtonDown()) return;
            owner.pointerActive = true;
            if (owner.selected < 0)
            {
                auto updated = *current;
                const auto point = value (event.position, true);
                const auto nearby = std::find_if (updated.points.begin(), updated.points.end(),
                    [point] (const auto& existing) { return std::abs (existing.beat - point.beat) < 1e-6; });
                const auto found = nearby != updated.points.end() ? nearby : std::lower_bound (updated.points.begin(), updated.points.end(), point.beat,
                    [] (const auto& existing, double beat) { return existing.beat < beat; });
                owner.selected = static_cast<int> (std::distance (updated.points.begin(), found));
                if (nearby != updated.points.end()) found->value = point.value;
                else if (updated.points.size() < 4096) updated.points.insert (found, point);
                else { owner.selected = -1; owner.pointerActive = false; owner.setError ("A lane can contain at most 4096 points."); return; }
                owner.apply (updated, true);
            }
            owner.synchronise(); repaint();
        }
        void mouseDrag (const juce::MouseEvent& event) override
        {
            if (! owner.pointerActive) return;
            owner.synchronise();
            if (! owner.available() || ! owner.pointerActive) return;
            auto current = owner.lane();
            if (! current || owner.selected < 0 || static_cast<size_t> (owner.selected) >= current->points.size()) return;
            auto point = value (event.position, ! event.mods.isAltDown());
            const auto index = static_cast<size_t> (owner.selected);
            // Leave a beat-space margin that survives native beat/time conversion.
            // Existing exceptionally close points may retain their original time.
            const auto originalBeat = current->points[index].beat;
            const auto minimum = index == 0 ? 0.0 : std::min (originalBeat, current->points[index - 1].beat + 1e-6);
            const auto maximum = index + 1 == current->points.size() ? maximumBeat : std::max (originalBeat, current->points[index + 1].beat - 1e-6);
            point.beat = std::clamp (point.beat, minimum, maximum);
            current->points[index] = point; owner.apply (*current, true);
        }
        void mouseUp (const juce::MouseEvent&) override { owner.finishGesture(); }
        bool keyPressed (const juce::KeyPress& key) override { return owner.handleKey (key); }
        void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
        {
            if (event.mods.isCommandDown() || event.mods.isCtrlDown()) owner.zoom (wheel.deltaY > 0 ? .5 : 2.0);
            else owner.setView (owner.viewStart - static_cast<double> (wheel.deltaX != 0 ? wheel.deltaX : wheel.deltaY) * owner.viewBeats,
                                owner.viewBeats);
        }
    private:
        int hit (juce::Point<float> at, const Session::AutomationLane& lane) const
        {
            int found = -1; auto distance = 10.0f;
            for (size_t i = 0; i < lane.points.size(); ++i)
            {
                const auto next = position (lane.points[i]).getDistanceFrom (at);
                if (next <= distance) { found = static_cast<int> (i); distance = next; }
            }
            return found;
        }
        Impl& owner;
    };

    Impl (Session& s, juce::String id, bool master) : session (s), trackId (std::move (id)), editingMaster (master), curve (*this)
    {
        generation = session.snapshot().projectGeneration;
        look.setColour (juce::ComboBox::backgroundColourId, panel);
        look.setColour (juce::ComboBox::textColourId, ink);
        look.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff585858));
        look.setColour (juce::TextButton::buttonColourId, panel);
        look.setColour (juce::TextButton::textColourOffId, ink);
        look.setColour (juce::ToggleButton::textColourId, ink);
        look.setColour (juce::ToggleButton::tickColourId, accent);
        look.setColour (juce::ScrollBar::thumbColourId, accent.withAlpha (.6f));
        setLookAndFeel (&look); setWantsKeyboardFocus (true);
        trackChoice.setName ("automation-track"); trackChoice.setTitle ("Automation destination");
        targetChoice.setName ("automation-target"); snapChoice.setName ("automation-snap");
        targetChoice.addItem ("Volume", 1); targetChoice.addItem ("Pan", 2); targetChoice.setSelectedId (1, juce::dontSendNotification);
        snapChoice.addItem ("Snap off", 1); snapChoice.addItem ("1 beat", 2); snapChoice.addItem ("1/2 beat", 3);
        snapChoice.addItem ("1/4 beat", 4); snapChoice.addItem ("1/8 beat", 5); snapChoice.addItem ("1/3 beat", 6);
        snapChoice.setSelectedId (4, juce::dontSendNotification);
        read.setButtonText ("Read"); read.setName ("automation-read");
        clear.setButtonText ("Clear"); clear.setName ("automation-clear");
        fit.setButtonText ("Fit"); fit.setName ("automation-fit");
        zoomIn.setButtonText ("+"); zoomIn.setName ("automation-zoom-in");
        zoomOut.setButtonText (juce::CharPointer_UTF8 ("−")); zoomOut.setName ("automation-zoom-out");
        trackChoice.onChange = [this]
        {
            numericDirty = false; finishGesture();
            editingMaster = trackChoice.getSelectedId() == 1;
            const auto index = trackChoice.getSelectedId() - 2;
            if (editingMaster) trackId.clear();
            else if (index >= 0 && static_cast<size_t> (index) < trackIds.size()) trackId = trackIds[static_cast<size_t> (index)];
            selected = -1; error.clear(); synchronise();
        };
        targetChoice.onChange = [this] { numericDirty = false; finishGesture(); selected = -1; error.clear(); synchronise(); };
        read.onClick = [this] { const auto enabled = read.getToggleState(); finishGesture(); synchronise(); if (auto next = lane()) { next->enabled = enabled; apply (*next, false); } };
        clear.onClick = [this] { finishGesture(); synchronise(); if (auto next = lane()) { next->points.clear(); selected = -1; apply (*next, false); } };
        fit.onClick = [this] { fitView(); }; zoomIn.onClick = [this] { zoom (.5); }; zoomOut.onClick = [this] { zoom (2); };
        beatLabel.setText ("Beat", juce::dontSendNotification);
        beatInput.setName ("automation-point-beat"); valueInput.setName ("automation-point-value");
        for (auto* input : { &beatInput, &valueInput })
        {
            input->setColour (juce::TextEditor::backgroundColourId, panel);
            input->setColour (juce::TextEditor::textColourId, ink);
            input->setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff585858));
            input->setSelectAllWhenFocused (true); input->onReturnKey = [this] { commitSelected(); };
            input->onTextChange = [this] { numericDirty = true; };
        }
        setPoint.setButtonText ("Set point"); setPoint.setName ("automation-set-point");
        setPoint.onClick = [this] { commitSelected(); };
        for (auto* label : { &beatLabel, &valueLabel }) label->setColour (juce::Label::textColourId, ink.withAlpha (.65f));
        read.setTooltip ("Read the stored lane. When off, play the retained manual setting. Clear the points to restore manual fader editing.");
        for (auto* control : std::initializer_list<juce::Component*> { &trackChoice, &targetChoice, &snapChoice, &read, &clear, &fit, &zoomIn, &zoomOut, &curve, &scroll, &status, &beatLabel, &beatInput, &valueLabel, &valueInput, &setPoint })
            addAndMakeVisible (control);
        status.setColour (juce::Label::textColourId, ink.withAlpha (.65f));
        scroll.addListener (this); scroll.setAutoHide (false);
        session.addChangeListener (this); synchronise(); startTimerHz (15);
    }
    ~Impl() override
    { stopTimer(); session.removeChangeListener (this); scroll.removeListener (this); finishGesture(); setLookAndFeel (nullptr); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (background); g.setColour (accent); g.fillRect (20, 18, 4, 18);
        g.setColour (ink); g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText (editingMaster ? "MASTER AUTOMATION" : "TRACK AUTOMATION", 36, 10, getWidth() - 56, 34, juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto toolbar = getLocalBounds().reduced (20).withTrimmedTop (34).withHeight (30);
        trackChoice.setBounds (toolbar.removeFromLeft (std::clamp (getWidth() / 4 - 30, 130, 220))); toolbar.removeFromLeft (8);
        targetChoice.setBounds (toolbar.removeFromLeft (100)); toolbar.removeFromLeft (8);
        read.setBounds (toolbar.removeFromLeft (65)); toolbar.removeFromLeft (6);
        snapChoice.setBounds (toolbar.removeFromLeft (110)); toolbar.removeFromLeft (8);
        clear.setBounds (toolbar.removeFromLeft (58)); toolbar.removeFromLeft (8);
        zoomOut.setBounds (toolbar.removeFromRight (30)); toolbar.removeFromRight (5);
        zoomIn.setBounds (toolbar.removeFromRight (30)); toolbar.removeFromRight (5);
        fit.setBounds (toolbar.removeFromRight (42));
        beatLabel.setBounds (24, 92, 40, 26); beatInput.setBounds (66, 92, 110, 26);
        valueLabel.setBounds (194, 92, 80, 26); valueInput.setBounds (276, 92, 94, 26); setPoint.setBounds (382, 92, 82, 26);
        curve.setBounds (20, 128, std::max (100, getWidth() - 40), std::max (90, getHeight() - 194));
        scroll.setBounds (86, getHeight() - 64, std::max (40, getWidth() - 126), 12);
        status.setBounds (20, getHeight() - 39, getWidth() - 40, 26);
    }
    Session::AutomationTarget target() const
    { return ! editingMaster && targetChoice.getSelectedId() == 2 ? Session::AutomationTarget::pan : Session::AutomationTarget::volume; }
    double snapBeats() const
    { constexpr double grid[] = { 0, 0, 1, .5, .25, .125, 1.0 / 3 }; return grid[std::clamp (snapChoice.getSelectedId(), 0, 6)]; }
    const Session::TrackInfo* track() const
    {
        const auto found = std::find_if (snapshot.tracks.begin(), snapshot.tracks.end(), [this] (const auto& t) { return t.id == trackId; });
        return found != snapshot.tracks.end() ? &*found : nullptr;
    }
    float manualValue() const
    {
        if (editingMaster) return snapshot.masterGainDb;
        if (const auto* current = track()) return target() == Session::AutomationTarget::volume ? current->gainDb : current->pan;
        return 0;
    }
    std::optional<Session::AutomationLane> lane() const
    {
        if (editingMaster) return snapshot.masterVolumeAutomation;
        if (const auto* current = track()) return target() == Session::AutomationTarget::volume ? current->volumeAutomation : current->panAutomation;
        return {};
    }
    bool available() const
    { return snapshot.projectGeneration == generation && (editingMaster || track() != nullptr) && ! snapshot.recording && ! session.isExporting(); }
    void synchronise()
    {
        snapshot = session.snapshot();
        if (! available()) finishGesture();
        juce::StringArray names, ids;
        for (const auto& current : snapshot.tracks) { names.add (current.name); ids.add (current.id); }
        if (names != trackNames || ids != trackIds || trackChoice.getNumItems() == 0)
        {
            trackNames = names; trackIds = ids; trackChoice.clear (juce::dontSendNotification);
            trackChoice.addItem ("Master", 1);
            trackChoice.addItemList (trackNames, 2);
        }
        if (! editingMaster && trackId.isEmpty() && ! trackIds.isEmpty()) trackId = trackIds[0];
        const auto index = trackIds.indexOf (trackId);
        trackChoice.setSelectedId (editingMaster ? 1 : index >= 0 ? index + 2 : 0, juce::dontSendNotification);
        if (editingMaster)
        {
            if (targetChoice.getNumItems() > 1)
            { targetChoice.clear (juce::dontSendNotification); targetChoice.addItem ("Volume", 1); }
            targetChoice.setSelectedId (1, juce::dontSendNotification);
        }
        else if (targetChoice.getNumItems() == 1) targetChoice.addItem ("Pan", 2);
        trackChoice.setEnabled (snapshot.projectGeneration == generation);
        for (auto* control : std::initializer_list<juce::Component*> { &targetChoice, &read, &clear, &curve }) control->setEnabled (available());
        if (const auto current = lane())
        {
            read.setToggleState (current->enabled, juce::dontSendNotification);
            if (selected >= static_cast<int> (current->points.size())) selected = -1;
            clear.setEnabled (available() && ! current->points.empty());
        }
        const auto current = lane();
        const bool hasPoint = available() && current && selected >= 0 && selected < static_cast<int> (current->points.size());
        for (auto* control : std::initializer_list<juce::Component*> { &beatInput, &valueInput, &setPoint }) control->setEnabled (hasPoint);
        valueLabel.setText (target() == Session::AutomationTarget::volume ? "Level (dB)" : "Pan (%)", juce::dontSendNotification);
        const bool samePoint = hasPoint && numericPoint && numericTrackId == trackId && numericTarget == target()
            && numericSelection == selected && *numericPoint == current->points[static_cast<size_t> (selected)];
        if (! samePoint) numericDirty = false;
        else if (beatInput.getText() != displayedBeat || valueInput.getText() != displayedValue)
            numericDirty = true; // Text-change callbacks are queued; a timer may arrive first.
        if (! numericDirty)
        {
            displayedBeat = hasPoint ? juce::String (current->points[static_cast<size_t> (selected)].beat, 6) : juce::String();
            displayedValue = hasPoint ? juce::String (current->points[static_cast<size_t> (selected)].value *
                (target() == Session::AutomationTarget::pan ? 100 : 1), 3) : juce::String();
            beatInput.setText (displayedBeat, false); valueInput.setText (displayedValue, false);
            numericPoint = hasPoint ? std::optional<Session::AutomationPoint> (current->points[static_cast<size_t> (selected)]) : std::nullopt;
            numericTrackId = trackId; numericTarget = target(); numericSelection = selected;
        }
        updateScroll(); updateStatus(); curve.repaint(); repaint();
    }
    void apply (const Session::AutomationLane& next, bool pointerGesture)
    {
        if (! available()) return;
        if (const auto previous = lane(); previous && *previous == next) return;
        if (! gesture) { session.beginEditGesture (target() == Session::AutomationTarget::volume ? "Edit volume automation" : "Edit pan automation"); gesture = true; }
        const auto result = editingMaster ? session.setMasterVolumeAutomation (next) : session.setTrackAutomation (trackId, target(), next);
        error = result.failed() ? result.getErrorMessage() : juce::String();
        if (! pointerGesture || result.failed()) finishGesture();
        synchronise();
    }
    void finishGesture()
    {
        pointerActive = false;
        if (gesture)
        {
            gesture = false;
            if (session.snapshot().projectGeneration == generation) session.endEditGesture();
        }
    }
    void removeSelected()
    {
        finishGesture(); synchronise(); auto current = lane();
        if (! available() || ! current || selected < 0 || selected >= static_cast<int> (current->points.size())) return;
        current->points.erase (current->points.begin() + selected); selected = -1; apply (*current, false);
    }
    void commitSelected()
    {
        const auto beatText = beatInput.getText().trim(), valueText = valueInput.getText().trim();
        const auto expectedPoint = numericPoint;
        const bool beatUnchanged = beatText == displayedBeat, valueUnchanged = valueText == displayedValue;
        finishGesture(); synchronise(); auto current = lane();
        if (! available() || ! current || selected < 0 || selected >= static_cast<int> (current->points.size())) return;
        auto number = [] (const juce::String& text) -> std::optional<double>
        {
            if (text.isEmpty()) return {};
            const auto utf8 = text.toRawUTF8(); char* end = nullptr; errno = 0;
            const auto result = std::strtod (utf8, &end);
            if (errno == ERANGE || end == utf8 || *end != 0 || ! std::isfinite (result)) return {};
            return result;
        };
        const bool volume = target() == Session::AutomationTarget::volume;
        if (expectedPoint && *expectedPoint != current->points[static_cast<size_t> (selected)])
        { setError ("The selected point changed. Select it again before editing."); return; }
        const auto beat = beatUnchanged && expectedPoint ? std::optional<double> (expectedPoint->beat) : number (beatText);
        const auto enteredValue = valueUnchanged && expectedPoint ? std::optional<double> (static_cast<double> (expectedPoint->value) * (volume ? 1 : 100)) : number (valueText);
        if (! beat || ! enteredValue || *beat < 0 || *beat > maximumBeat
            || *enteredValue < -100 || *enteredValue > (volume ? 6 : 100))
        { setError (volume ? "Enter a beat from 0 to 1000000 and a level from -100 to +6 dB." : "Enter a beat from 0 to 1000000 and pan from -100% (left) to +100% (right)."); return; }
        const auto index = static_cast<size_t> (selected);
        for (size_t i = 0; i < current->points.size(); ++i)
            if (i != index && std::abs (current->points[i].beat - *beat) < 1e-6)
            { setError ("Another point already occupies that beat."); return; }
        current->points.erase (current->points.begin() + selected);
        const auto insert = std::lower_bound (current->points.begin(), current->points.end(), *beat,
            [] (const auto& point, double position) { return point.beat < position; });
        selected = static_cast<int> (std::distance (current->points.begin(), insert));
        current->points.insert (insert, { *beat, static_cast<float> (*enteredValue / (volume ? 1 : 100)) });
        numericDirty = false; apply (*current, false);
    }
    bool handleKey (const juce::KeyPress& key)
    {
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent(); dynamic_cast<juce::TextEditor*> (focused) != nullptr) return false;
        if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
        { removeSelected(); return true; }
        if ((key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown()) && juce::CharacterFunctions::toLowerCase (key.getTextCharacter()) == 'z')
        {
            finishGesture(); synchronise();
            if (snapshot.projectGeneration == generation) { if (key.getModifiers().isShiftDown()) session.redo(); else session.undo(); }
            synchronise(); return true;
        }
        if (key.getKeyCode() == juce::KeyPress::spaceKey)
        {
            finishGesture(); synchronise();
            if (snapshot.projectGeneration == generation)
            { if (snapshot.playing) session.stop(); else { const auto result = session.play(); if (result.failed()) setError (result.getErrorMessage()); } }
            synchronise(); return true;
        }
        if (key.getKeyCode() == juce::KeyPress::escapeKey)
        {
            finishGesture(); selected = -1;
            if (session.snapshot().projectGeneration == generation) session.stop();
            synchronise(); return true;
        }
        return false;
    }
    bool keyPressed (const juce::KeyPress& key) override { return handleKey (key); }
    juce::String formatValue (float value) const
    {
        if (target() == Session::AutomationTarget::volume) return value <= -99.99f ? juce::String ("Mute") : juce::String (value, 1) + " dB";
        if (std::abs (value) < .001f) return "Centre";
        return juce::String (std::abs (value) * 100, 0) + (value < 0 ? "% L" : "% R");
    }
    void setError (juce::String message) { error = std::move (message); updateStatus(); }
    void updateStatus()
    {
        juce::String text;
        if (snapshot.projectGeneration != generation) text = "Project changed. Reopen automation from the current project.";
        else if (! editingMaster && ! track()) text = "This track is unavailable. Select another destination, or undo its removal.";
        else if (session.isExporting()) text = "Automation editing is unavailable during export.";
        else if (snapshot.recording) text = "Stop recording to edit automation.";
        else if (error.isNotEmpty()) text = error;
        else if (const auto current = lane(); current && selected >= 0 && selected < static_cast<int> (current->points.size()))
        {
            const auto& point = current->points[static_cast<size_t> (selected)];
            text = "Beat " + juce::String (point.beat, 3) + "   " + formatValue (point.value) + "   •   Drag to move · Alt bypasses snap · Delete removes";
        }
        else text = "Click to add · Drag to move · Right-click to remove · Scroll to navigate · Volume ramps use fader scale";
        status.setText (text, juce::dontSendNotification);
    }
    void setView (double start, double length)
    {
        viewBeats = std::clamp (length, 1.0, maximumBeat);
        viewStart = std::clamp (start, 0.0, maximumBeat - viewBeats); updateScroll(); curve.repaint();
    }
    void zoom (double factor) { const auto next = std::clamp (viewBeats * factor, 1.0, maximumBeat); setView (viewStart + (viewBeats - next) / 2, next); }
    void fitView()
    {
        synchronise(); double end = snapshot.duration * snapshot.tempo / 60;
        if (const auto current = lane(); current && ! current->points.empty()) end = std::max (end, current->points.back().beat + 1);
        setView (0, std::max (4.0, std::ceil (end / 4) * 4));
    }
    void updateScroll()
    {
        double end = std::max ({ 16.0, snapshot.duration * snapshot.tempo / 60 + 4, viewStart + viewBeats });
        if (const auto current = lane(); current && ! current->points.empty()) end = std::max (end, current->points.back().beat + 4);
        scroll.setRangeLimits (0, std::min (maximumBeat, end), juce::dontSendNotification);
        scroll.setCurrentRange (viewStart, viewBeats, juce::dontSendNotification);
        scroll.setSingleStepSize (std::max (.25, viewBeats / 16));
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { synchronise(); }
    void timerCallback() override { synchronise(); }
    void scrollBarMoved (juce::ScrollBar*, double start) override { setView (start, viewBeats); }

    Session& session;
    juce::String trackId, error;
    bool editingMaster = false;
    uint64_t generation = 0;
    Session::Snapshot snapshot;
    juce::StringArray trackIds, trackNames;
    juce::LookAndFeel_V4 look;
    juce::ComboBox trackChoice, targetChoice, snapChoice;
    juce::ToggleButton read;
    juce::TextButton clear, fit, zoomIn, zoomOut, setPoint;
    juce::TextEditor beatInput, valueInput;
    juce::Label beatLabel, valueLabel;
    Curve curve;
    juce::ScrollBar scroll { false };
    juce::Label status;
    double viewStart = 0, viewBeats = 16;
    int selected = -1;
    bool gesture = false, pointerActive = false, numericDirty = false;
    std::optional<Session::AutomationPoint> numericPoint;
    juce::String numericTrackId, displayedBeat, displayedValue;
    Session::AutomationTarget numericTarget = Session::AutomationTarget::volume;
    int numericSelection = -1;
};

AutomationEditor::AutomationEditor (Session& session) : impl (std::make_unique<Impl> (session, juce::String(), true))
{ setName ("Automation"); addAndMakeVisible (*impl); setSize (900, 460); }
AutomationEditor::AutomationEditor (Session& session, const juce::String& trackId) : impl (std::make_unique<Impl> (session, trackId, false))
{ setName ("Automation"); addAndMakeVisible (*impl); setSize (900, 460); }
AutomationEditor::~AutomationEditor() = default;
void AutomationEditor::resized() { impl->setBounds (getLocalBounds()); }
bool AutomationEditor::keyPressed (const juce::KeyPress& key) { return impl->handleKey (key); }

#if HALFTRACK_TESTS_ENABLED
int runAutomationEditorTests (const juce::File& root)
{
    try
    {
        auto check = [] (bool condition, const char* message)
        { if (! condition) throw std::runtime_error (message); std::cout << "PASS automation editor: " << message << std::endl; };
        Session session (false);
        check (session.addAudioTrack ("Automation A").wasOk() && session.addAudioTrack ("Automation B").wasOk(), "create editor tracks");
        const auto firstId = session.snapshot().tracks[0].id;
        AutomationEditor editor (session, firstId);
        auto& ui = *editor.impl;
        auto& curve = ui.curve;
        auto event = [&] (juce::Point<float> at, juce::Point<float> down, bool dragged = false, bool right = false)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                juce::ModifierKeys (right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier),
                1.0f, 0, 0, 0, 0, &curve, &curve, now, down, now, 1, dragged);
        };
        auto click = [&] (double beat, float value)
        {
            const auto at = curve.position ({ beat, value }); curve.mouseDown (event (at, at)); curve.mouseUp (event (at, at));
        };
        auto lane = [&] { return session.snapshot().tracks[0].volumeAutomation; };
        click (1.12, -6);
        check (lane().points.size() == 1 && lane().points[0].beat == 1 && std::abs (lane().points[0].value + 6) < .001f,
               "click inserts a snapped point into the native lane");
        session.undo(); ui.synchronise(); check (lane().points.empty(), "one undo removes added point");
        session.redo(); ui.synchronise(); click (4, 0);
        const auto original = lane();
        const auto down = curve.position (original.points[0]); curve.mouseDown (event (down, down));
        auto at = curve.position ({ 2, -12 }); curve.mouseDrag (event (at, down, true));
        at = curve.position ({ 3, -18 }); curve.mouseDrag (event (at, down, true)); curve.mouseUp (event (at, down, true));
        check (lane().points[0].beat == 3 && std::abs (lane().points[0].value + 18) < .001f, "pointer drag moves point time and value");
        session.undo(); ui.synchronise(); check (lane() == original, "one undo restores the entire multi-event drag");
        session.redo(); ui.synchronise();
        const auto neighbourDown = curve.position (lane().points[0]); curve.mouseDown (event (neighbourDown, neighbourDown));
        const auto beyond = curve.position ({ 9, 6 }); curve.mouseDrag (event (beyond, neighbourDown, true)); curve.mouseUp (event (beyond, neighbourDown, true));
        check (lane().points[0].beat < lane().points[1].beat, "drag cannot cross or duplicate its neighbour time");
        session.undo(); ui.synchronise();
        auto remove = curve.position (lane().points[0]); curve.mouseDown (event (remove, remove, false, true));
        check (lane().points.size() == 1 && lane().points[0].beat == 4, "right-click deletes the addressed point");
        session.undo(); ui.synchronise();
        remove = curve.position (lane().points[0]); curve.mouseDown (event (remove, remove)); curve.mouseUp (event (remove, remove));
        check (curve.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)) && lane().points.size() == 1, "Delete removes the selected point");
        session.undo(); ui.synchronise();
        for (const auto tempo : { 20.0, 999.0 })
        {
            session.setTempo (tempo); ui.synchronise(); const auto baseline = lane();
            const auto pointDown = curve.position (baseline.points[0]); curve.mouseDown (event (pointDown, pointDown));
            const auto pointMove = curve.position ({ baseline.points[1].beat + 2, -100 });
            curve.mouseDrag (event (pointMove, pointDown, true)); curve.mouseUp (event (pointMove, pointDown, true));
            const auto moved = lane();
            check (moved.points[0].beat < moved.points[1].beat && moved.points[0].value == -100,
                   "neighbour clamp survives low/high tempo conversion and reaches the mute boundary");
            const auto bottom = curve.position (moved.points[0]); curve.mouseDown (event (bottom, bottom)); curve.mouseUp (event (bottom, bottom));
            check (ui.selected == 0, "bottom-edge mute point remains selectable");
            session.undo(); ui.synchronise(); check (lane() == baseline, "boundary gesture restores with one undo");
        }
        session.setTempo (120); ui.synchronise();
        const auto beforeRead = lane(); ui.read.setToggleState (false, juce::dontSendNotification); ui.read.onClick();
        check (! lane().enabled && lane().points == beforeRead.points, "Read toggle bypasses playback without losing points");
        session.undo(); ui.synchronise(); check (lane() == beforeRead, "Read toggle is undoable");
        ui.clear.onClick(); check (lane().points.empty(), "Clear removes the lane points");
        session.undo(); ui.synchronise(); check (lane() == beforeRead, "Clear restores all points with one undo");
        ui.targetChoice.setSelectedId (2, juce::sendNotificationSync); click (2, -.75f);
        check (session.snapshot().tracks[0].panAutomation.points.size() == 1 && lane() == beforeRead,
               "Pan target edits its independent native lane");
        ui.trackChoice.setSelectedId (3, juce::sendNotificationSync); click (4, .5f);
        check (session.snapshot().tracks[1].panAutomation.points.size() == 1
            && session.snapshot().tracks[0].panAutomation.points.size() == 1, "track selector edits only the selected stable track");
        ui.trackChoice.setSelectedId (2, juce::sendNotificationSync); ui.targetChoice.setSelectedId (1, juce::sendNotificationSync);
        const auto beforeTrack = lane(); const auto changeDown = curve.position (beforeTrack.points[0]);
        curve.mouseDown (event (changeDown, changeDown));
        const auto changeMove = curve.position ({ 2, -12 }); curve.mouseDrag (event (changeMove, changeDown, true));
        ui.trackChoice.setSelectedId (3, juce::sendNotificationSync);
        curve.mouseDrag (event (changeMove, changeDown, true)); curve.mouseUp (event (changeMove, changeDown, true));
        check (session.snapshot().tracks[1].volumeAutomation.points.empty(), "switching tracks ends the pending pointer gesture");
        session.undo(); ui.synchronise(); check (lane() == beforeTrack, "track switch does not split or contaminate gesture undo");
        ui.trackChoice.setSelectedId (2, juce::sendNotificationSync);
        const auto beforeTarget = lane(), panBeforeTarget = session.snapshot().tracks[0].panAutomation;
        const auto targetDown = curve.position (beforeTarget.points[0]); curve.mouseDown (event (targetDown, targetDown));
        const auto targetMove = curve.position ({ 1.5, -9 }); curve.mouseDrag (event (targetMove, targetDown, true));
        ui.targetChoice.setSelectedId (2, juce::sendNotificationSync);
        curve.mouseDrag (event (targetMove, targetDown, true)); curve.mouseUp (event (targetMove, targetDown, true));
        check (session.snapshot().tracks[0].panAutomation == panBeforeTarget, "changing targets ends the pending pointer gesture");
        session.undo(); ui.synchronise(); check (lane() == beforeTarget, "target switch preserves one-step gesture undo");
        ui.targetChoice.setSelectedId (1, juce::sendNotificationSync);
        const auto beforeRepeatedBeat = lane(); click (beforeRepeatedBeat.points[1].beat, -24);
        check (lane().points.size() == beforeRepeatedBeat.points.size() && lane().points[1].beat == beforeRepeatedBeat.points[1].beat,
               "clicking an occupied snapped beat changes its value without duplicating native time");
        session.undo(); ui.synchronise(); check (lane() == beforeRepeatedBeat, "occupied-beat change restores with one undo");
        ui.fit.onClick(); const auto fitted = ui.viewBeats; ui.zoomIn.onClick();
        check (ui.viewBeats < fitted, "zoom increases graph detail"); ui.zoomOut.onClick();
        check (ui.viewBeats == fitted, "zoom out restores the visible range");
        ui.selected = 0; ui.synchronise(); const auto beforeTyped = lane();
        ui.beatInput.setText ("2.375", false); ui.valueInput.setText ("-13.25", false);
        ui.synchronise();
        check (ui.beatInput.getText() == "2.375" && ui.valueInput.getText() == "-13.25",
            "timer preserves edited text before its queued change callback");
        ui.beatInput.onTextChange(); ui.valueInput.onTextChange(); ui.synchronise();
        check (! ui.beatInput.hasKeyboardFocus (false) && ! ui.valueInput.hasKeyboardFocus (false)
            && ui.beatInput.getText() == "2.375" && ui.valueInput.getText() == "-13.25",
            "pending precise input survives focus loss and periodic synchronization");
        ui.setPoint.onClick();
        check (lane().points[0].beat == 2.375 && lane().points[0].value == -13.25f, "typed fields set exact point time and dB value");
        session.undo(); ui.synchronise(); check (lane() == beforeTyped, "typed point changes are one undo transaction");
        for (const auto* invalid : { "nan", "inf", "nonsense", "1xyz", "-1", "1000001" })
        {
            ui.beatInput.setText (invalid, false); ui.valueInput.setText ("-12", false); ui.beatInput.onReturnKey();
            check (lane() == beforeTyped, "invalid typed time leaves the lane unchanged");
        }
        ui.beatInput.setText (juce::String (beforeTyped.points[1].beat), false); ui.valueInput.setText ("-12", false); ui.setPoint.onClick();
        check (lane() == beforeTyped, "typed duplicate times are rejected without replacing another point");
        ui.beatInput.setText ("2", false); ui.valueInput.setText ("7", false); ui.setPoint.onClick();
        check (lane() == beforeTyped, "typed volume above native ceiling is rejected");
        ui.targetChoice.setSelectedId (2, juce::sendNotificationSync); ui.selected = 0; ui.synchronise();
        ui.beatInput.setText ("3.125", false); ui.valueInput.setText ("-37.5", false); ui.setPoint.onClick();
        check (session.snapshot().tracks[0].panAutomation.points[0].value == -.375f, "typed pan percent maps to the native pan range");
        ui.targetChoice.setSelectedId (1, juce::sendNotificationSync); ui.selected = 0; ui.error.clear(); ui.synchronise();
        check (root.createDirectory().wasOk(), "create editor artifact directory");
        editor.setSize (820, 420);
        check (ui.clear.getRight() <= ui.fit.getX() && ui.snapChoice.getRight() < ui.clear.getX(),
               "controls remain separated at the minimum editor size");
        editor.setSize (900, 460);
        const auto screenshot = editor.createComponentSnapshot (editor.getLocalBounds());
        juce::FileOutputStream imageOutput (root.getChildFile ("automation-editor.png"));
        check (juce::PNGImageFormat().writeImageToStream (screenshot, imageOutput), "save actual editor screenshot");
        const auto file = root.getChildFile ("automation-editor.halftrack");
        check (session.saveProject (file).wasOk(), "save automated editor project");
        const auto staleDown = curve.position (lane().points[0]); curve.mouseDown (event (staleDown, staleDown));
        const auto staleMove = curve.position ({ 2, -9 }); curve.mouseDrag (event (staleMove, staleDown, true));
        check (session.loadProject (file).wasOk(), "replace edit with identical track IDs");
        const auto loaded = session.snapshot().tracks[0].volumeAutomation;
        curve.mouseDrag (event (staleMove, staleDown, true)); curve.mouseUp (event (staleMove, staleDown, true)); ui.clear.onClick();
        check (session.snapshot().tracks[0].volumeAutomation == loaded && ! curve.isEnabled(), "reloaded project rejects retained pointer and button callbacks");
        AutomationEditor fresh (session, firstId); auto& next = *fresh.impl;
        const auto freshPoint = next.curve.position (loaded.points[0]);
        auto freshEvent = [&] (juce::Point<float> position, bool dragged)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position,
                juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                &next.curve, &next.curve, now, freshPoint, now, 1, dragged);
        };
        next.curve.mouseDown (freshEvent (freshPoint, false));
        const auto freshMoved = next.curve.position ({ 2, -15 }); next.curve.mouseDrag (freshEvent (freshMoved, true));
        check (session.newProject().wasOk() && session.addAudioTrack ("Replacement").wasOk(), "replace project during active drag");
        next.curve.mouseDrag (freshEvent (freshMoved, true)); next.curve.mouseUp (freshEvent (freshMoved, true));
        check (session.snapshot().tracks[0].volumeAutomation.points.empty() && ! next.curve.isEnabled(), "New cannot redirect a stale gesture onto a reused track ID");
        check (session.setMasterGain (-9).wasOk(), "set retained manual master level");
        AutomationEditor masterEditor (session); auto& master = *masterEditor.impl; auto& masterCurve = master.curve;
        auto masterEvent = [&] (juce::Point<float> position, juce::Point<float> downAt, bool dragged = false, bool right = false)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position,
                juce::ModifierKeys (right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier),
                1.0f, 0, 0, 0, 0, &masterCurve, &masterCurve, now, downAt, now, 1, dragged);
        };
        auto masterClick = [&] (double beat, float value)
        {
            const auto point = masterCurve.position ({ beat, value });
            masterCurve.mouseDown (masterEvent (point, point)); masterCurve.mouseUp (masterEvent (point, point));
        };
        auto masterLane = [&] { return session.snapshot().masterVolumeAutomation; };
        check (master.editingMaster && master.trackChoice.getText() == "Master" && master.targetChoice.getNumItems() == 1
            && master.target() == Session::AutomationTarget::volume && std::abs (master.manualValue() + 9) < .001f,
            "explicit master destination exposes only volume and its real manual baseline");
        check (masterEditor.createComponentSnapshot (masterEditor.getLocalBounds()).isValid(), "empty master lane paints without a track pointer");
        masterClick (1.12, -6);
        check (masterLane().points.size() == 1 && masterLane().points[0].beat == 1
            && std::abs (masterLane().points[0].value + 6) < .001f && session.snapshot().tracks[0].volumeAutomation.points.empty(),
            "master pointer insertion edits only master volume");
        session.undo(); master.synchronise(); check (masterLane().points.empty(), "one undo removes the master point");
        session.redo(); master.synchronise(); masterClick (4, 0);
        const auto masterOriginal = masterLane();
        auto masterDown = masterCurve.position (masterOriginal.points[0]); masterCurve.mouseDown (masterEvent (masterDown, masterDown));
        auto masterMove = masterCurve.position ({ 2, -12 }); masterCurve.mouseDrag (masterEvent (masterMove, masterDown, true));
        masterMove = masterCurve.position ({ 3, -18 }); masterCurve.mouseDrag (masterEvent (masterMove, masterDown, true));
        masterCurve.mouseUp (masterEvent (masterMove, masterDown, true));
        check (masterLane().points[0].beat == 3 && std::abs (masterLane().points[0].value + 18) < .001f,
            "master curve drag changes time and level");
        session.undo(); master.synchronise(); check (masterLane() == masterOriginal, "one undo restores the entire master drag");
        session.redo(); master.synchronise();
        const auto beforeMasterTyped = masterLane(); master.selected = 0; master.synchronise();
        master.beatInput.setText ("2.375", false); master.valueInput.setText ("-13.25", false); master.setPoint.onClick();
        check (masterLane().points[0].beat == 2.375 && std::abs (masterLane().points[0].value + 13.25f) < .001f,
            "master point accepts exact beat and decibel input");
        session.undo(); master.synchronise(); check (masterLane() == beforeMasterTyped, "exact master point editing undoes once");
        master.trackChoice.setSelectedId (2, juce::sendNotificationSync); master.targetChoice.setSelectedId (2, juce::sendNotificationSync);
        masterClick (2, -.5f);
        const auto firstTrackPan = session.snapshot().tracks[0].panAutomation;
        check (firstTrackPan.points.size() == 1 && masterLane() == beforeMasterTyped, "master window can independently edit a track pan lane");
        master.trackChoice.setSelectedId (1, juce::sendNotificationSync);
        check (master.targetChoice.getNumItems() == 1 && master.targetChoice.getSelectedId() == 1
            && master.target() == Session::AutomationTarget::volume, "switching track pan to Master removes unsupported pan");
        masterDown = masterCurve.position (masterLane().points[0]); masterCurve.mouseDown (masterEvent (masterDown, masterDown));
        masterMove = masterCurve.position ({ 2, -12 }); masterCurve.mouseDrag (masterEvent (masterMove, masterDown, true));
        master.trackChoice.setSelectedId (2, juce::sendNotificationSync);
        masterCurve.mouseDrag (masterEvent (masterMove, masterDown, true)); masterCurve.mouseUp (masterEvent (masterMove, masterDown, true));
        check (session.snapshot().tracks[0].volumeAutomation.points.empty() && session.snapshot().tracks[0].panAutomation == firstTrackPan,
            "switching away from Master ends its gesture without changing the destination track");
        session.undo(); master.synchronise(); check (masterLane() == beforeMasterTyped, "destination change preserves one-step master gesture undo");
        master.trackChoice.setSelectedId (1, juce::sendNotificationSync);
        master.read.setToggleState (false, juce::dontSendNotification); master.read.onClick();
        check (! masterLane().enabled && masterLane().points == beforeMasterTyped.points, "master Read bypass retains every point");
        session.undo(); master.synchronise(); check (masterLane() == beforeMasterTyped, "master Read bypass undoes once");
        master.clear.onClick();
        check (masterLane().points.empty() && std::abs (session.snapshot().masterGainDb + 9) < .001f, "master Clear restores retained manual level");
        session.undo(); master.synchronise(); check (masterLane() == beforeMasterTyped, "master Clear undoes once");
        masterDown = masterCurve.position (masterLane().points[0]); masterCurve.mouseDown (masterEvent (masterDown, masterDown, false, true));
        check (masterLane().points.size() == 1, "right-click removes a master point");
        session.undo(); master.synchronise();
        masterDown = masterCurve.position (masterLane().points[0]); masterCurve.mouseDown (masterEvent (masterDown, masterDown));
        masterCurve.mouseUp (masterEvent (masterDown, masterDown)); masterCurve.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
        check (masterLane().points.size() == 1, "Delete removes the selected master point");
        session.undo(); master.synchronise();
        const auto beforeTrackRemoval = masterLane();
        check (session.moveTrack (session.snapshot().tracks[0].id, 1).wasOk(), "reorder a track while master editor is open"); master.synchronise();
        check (master.available() && masterLane() == beforeTrackRemoval && master.trackChoice.getText() == "Master",
            "track reordering never redirects the master destination");
        while (! session.snapshot().tracks.empty()) check (session.removeTrack (0).wasOk(), "remove an unrelated track while Master remains selected");
        master.synchronise();
        check (master.available() && master.trackChoice.getNumItems() == 1 && masterLane() == beforeTrackRemoval,
            "master editor remains available with no tracks");
        masterClick (8, -3); check (masterLane().points.size() == 3, "master pointer editing works in an empty track list");
        {
            auto stream = root.getChildFile ("master-automation-editor.png").createOutputStream();
            check (stream && stream->setPosition (0) && stream->truncate().wasOk()
                && juce::PNGImageFormat().writeImageToStream (masterEditor.createComponentSnapshot (masterEditor.getLocalBounds()), *stream),
                "save actual master automation editor screenshot");
        }
        const auto masterFile = root.getChildFile ("master-automation-editor.halftrack");
        check (session.saveProject (masterFile).wasOk(), "save the master editor fixture");
        masterDown = masterCurve.position (masterLane().points[0]); masterCurve.mouseDown (masterEvent (masterDown, masterDown));
        masterMove = masterCurve.position ({ 2, -15 }); masterCurve.mouseDrag (masterEvent (masterMove, masterDown, true));
        check (session.loadProject (masterFile).wasOk(), "load project during a master curve gesture");
        const auto loadedMaster = masterLane();
        masterCurve.mouseDrag (masterEvent (masterMove, masterDown, true)); masterCurve.mouseUp (masterEvent (masterMove, masterDown, true));
        master.clear.onClick(); master.read.setToggleState (false, juce::dontSendNotification); master.read.onClick();
        check (masterLane() == loadedMaster && ! master.available() && ! masterCurve.isEnabled() && ! session.canUndo(),
            "Load rejects retained master pointer, Clear and Read callbacks");
        AutomationEditor newMasterEditor (session); auto& freshMaster = *newMasterEditor.impl;
        const auto freshMasterDown = freshMaster.curve.position (loadedMaster.points[0]);
        auto freshMasterEvent = [&] (juce::Point<float> point, bool dragged)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), point,
                juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                &freshMaster.curve, &freshMaster.curve, now, freshMasterDown, now, 1, dragged);
        };
        freshMaster.curve.mouseDown (freshMasterEvent (freshMasterDown, false));
        const auto freshMasterMove = freshMaster.curve.position ({ 1, -24 }); freshMaster.curve.mouseDrag (freshMasterEvent (freshMasterMove, true));
        check (session.newProject().wasOk(), "replace project during a master gesture");
        freshMaster.curve.mouseDrag (freshMasterEvent (freshMasterMove, true)); freshMaster.curve.mouseUp (freshMasterEvent (freshMasterMove, true));
        freshMaster.clear.onClick();
        check (session.snapshot().masterVolumeAutomation.points.empty() && ! freshMaster.available() && ! session.canUndo(),
            "New cannot redirect a stale master gesture into its replacement project");
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL automation editor: " << exception.what() << std::endl; return 1;
    }
}
#endif
}
