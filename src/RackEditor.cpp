#include "RackEditor.h"
#include "NativeEffects.h"
#include "Racks.h"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace halftrack
{
namespace
{
using Kind = Session::EffectKind;
const juce::Colour background { 0xff1d1d1d }, panel { 0xff2d2d2d }, raised { 0xff3e3e3e }, line { 0xff141414 },
                   ink { 0xffdadada }, muted { 0xff8e8e8e }, orange { 0xffffab3d }, green { 0xff48d17e }, blue { 0xff6fa8dc };
juce::Font font (float size = 11.0f, bool bold = false) { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)); }
juce::String noteName (int note)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[note % 12]) + juce::String (note / 12 - 2);
}
juce::String sendName (int index) { return "Send " + juce::String::charToString ((juce::juce_wchar) ('A' + index)); }
juce::String volumeText (double v) { return v <= -100 ? juce::String ("-inf") : juce::String (v, 1) + " dB"; }
juce::String panText (double v) { return std::abs (v) < .005 ? juce::String ("C") : juce::String ((int) std::round (std::abs (v) * 50)) + (v < 0 ? "L" : "R"); }
void quiet (juce::Component& c) { c.setWantsKeyboardFocus (false); c.setMouseClickGrabsKeyboardFocus (false); }

class RackEditor;

// One row of the chain list: activator, name, solo, volume and pan.
class ChainRow final : public juce::Component
{
public:
    ChainRow (RackEditor& o, juce::String chain) : owner (o), chainId (std::move (chain))
    {
        active.setName ("Chain active"); active.setTooltip ("Chain activator: switch this chain on or off");
        active.setClickingTogglesState (true); active.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffcf4a));
        solo.setName ("Chain solo"); solo.setTooltip ("Solo this chain. Cmd/Ctrl-click to solo several.");
        solo.setClickingTogglesState (true); solo.setColour (juce::TextButton::buttonOnColourId, blue);
        name.setName ("Chain name"); name.setEditable (false, true, false); name.setFont (font (10.5f)); name.setColour (juce::Label::textColourId, ink);
        name.setTooltip ("Double-click to rename");
        volume.setName ("Chain volume"); volume.setSliderStyle (juce::Slider::LinearBar); volume.setRange (-100, 6, .1); volume.setSkewFactorFromMidPoint (-12);
        volume.setDoubleClickReturnValue (true, 0); volume.textFromValueFunction = volumeText;
        volume.setColour (juce::Slider::trackColourId, juce::Colour (0xff4f6d57));
        pan.setName ("Chain pan"); pan.setSliderStyle (juce::Slider::LinearBar); pan.setRange (-1, 1, .01); pan.setDoubleClickReturnValue (true, 0);
        pan.textFromValueFunction = panText; pan.setColour (juce::Slider::trackColourId, juce::Colour (0xff4a4a4a));
        for (auto* c : std::initializer_list<juce::Component*> { &active, &solo, &name, &volume, &pan }) { quiet (*c); addAndMakeVisible (*c); }
        active.onClick = [this] { change ([this] (auto& s) { s.active = active.getToggleState(); }); };
        solo.onClick = [this] { soloClicked(); };
        name.onTextChange = [this] { const auto text = name.getText().trim(); if (text.isNotEmpty()) change ([text] (auto& s) { s.name = text; }); };
        for (auto* s : { &volume, &pan })
        {
            s->onDragStart = [this] { begin ("Chain mix"); };
            s->onDragEnd = [this] { end(); };
        }
        volume.onValueChange = [this] { change ([this] (auto& s) { s.volumeDb = (float) volume.getValue(); }); };
        pan.onValueChange = [this] { change ([this] (auto& s) { s.pan = (float) pan.getValue(); }); };
    }
    ~ChainRow() override { end(); }
    void sync (const Session::RackChainInfo& info, bool selectedRow, bool isReturn)
    {
        settings = info.settings; selected = selectedRow; returnChain = isReturn;
        active.setToggleState (settings.active, juce::dontSendNotification);
        solo.setToggleState (settings.solo, juce::dontSendNotification); solo.setVisible (! isReturn);
        if (! name.isBeingEdited()) name.setText (settings.name, juce::dontSendNotification);
        if (! volume.isMouseButtonDown()) volume.setValue (settings.volumeDb, juce::dontSendNotification);
        if (! pan.isMouseButtonDown()) pan.setValue (settings.pan, juce::dontSendNotification);
        volume.updateText(); pan.updateText();
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (2, 1);
        active.setBounds (r.removeFromLeft (14).withSizeKeepingCentre (14, 14)); r.removeFromLeft (3);
        pan.setBounds (r.removeFromRight (34)); r.removeFromRight (2);
        volume.setBounds (r.removeFromRight (48)); r.removeFromRight (2);
        solo.setBounds (r.removeFromRight (16).withSizeKeepingCentre (16, 14)); r.removeFromRight (2);
        name.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (selected ? raised : panel);
        if (selected) { g.setColour (orange); g.fillRect (0, 0, 2, getHeight()); }
        if (returnChain) { g.setColour (muted.withAlpha (.4f)); g.drawHorizontalLine (0, 0.0f, (float) getWidth()); }
    }
    void mouseDown (const juce::MouseEvent& e) override;
    const juce::String chainId;
    juce::TextButton active { "" }, solo { "S" };
    juce::Label name;
    juce::Slider volume, pan;
private:
    void change (const std::function<void (Session::RackChainSettings&)>& edit);
    void soloClicked();
    void begin (const juce::String&);
    void end();
    RackEditor& owner;
    Session::RackChainSettings settings;
    bool selected = false, returnChain = false, gesture = false;
};

// Key, velocity and chain select zones for every chain, with fade strips and the chain selector.
class ZoneEditor final : public juce::Component
{
public:
    enum class Mode { key, velocity, select };
    explicit ZoneEditor (RackEditor& o) : owner (o)
    {
        setName ("Zone editor");
        for (auto* b : { &keyButton, &velocityButton, &selectButton })
        {
            quiet (*b); addAndMakeVisible (*b); b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, orange.withAlpha (.55f));
        }
        keyButton.setName ("Key zones"); velocityButton.setName ("Velocity zones"); selectButton.setName ("Chain select zones");
        keyButton.onClick = [this] { setMode (Mode::key); };
        velocityButton.onClick = [this] { setMode (Mode::velocity); };
        selectButton.onClick = [this] { setMode (Mode::select); };
        selector.setName ("Chain selector"); selector.setSliderStyle (juce::Slider::LinearBar); selector.setRange (0, 127, 1);
        selector.setTooltip ("Chain selector: chains whose select zone holds this value receive input");
        selector.setColour (juce::Slider::trackColourId, orange.withAlpha (.45f));
        quiet (selector); addChildComponent (selector);
    }
    void setMode (Mode next) { mode = next; resized(); repaint(); }
    Mode getMode() const noexcept { return mode; }
    void sync (const std::vector<Session::RackChainInfo>& chains, Kind kind, int selectorValue, bool selectorMapped);
    void resized() override
    {
        auto top = getLocalBounds().removeFromTop (18).reduced (1);
        const bool zones = rackKind != Kind::audioEffectRack;
        keyButton.setVisible (zones); velocityButton.setVisible (zones);
        if (zones) { keyButton.setBounds (top.removeFromLeft (40)); velocityButton.setBounds (top.removeFromLeft (40)); }
        selectButton.setBounds (top.removeFromLeft (zones ? 52 : 92));
        keyButton.setToggleState (mode == Mode::key, juce::dontSendNotification);
        velocityButton.setToggleState (mode == Mode::velocity, juce::dontSendNotification);
        selectButton.setToggleState (mode == Mode::select, juce::dontSendNotification);
        selector.setVisible (mode == Mode::select);
        selector.setBounds (area().withHeight (14).translated (0, -15));
    }
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    // Where a value sits on a row: the zone bar, or its fade strip.
    juce::Point<int> pointFor (int row, int value, bool fade) const
    {
        const auto a = area();
        const int x = juce::roundToInt (a.getX() + (value - minimum() + .5) * a.getWidth() / (double) span());
        const int y = a.getY() + row * rowHeight + (fade ? 2 : rowHeight - 5);
        return { x, y };
    }
    int xOf (int value) const { const auto a = area(); return a.getX() + juce::roundToInt ((value - minimum()) * a.getWidth() / (double) span()); }
    juce::TextButton keyButton { "Key" }, velocityButton { "Vel" }, selectButton { "Chain" };
    juce::Slider selector;
private:
    static constexpr int rowHeight = 16;
    juce::Rectangle<int> area() const { return getLocalBounds().withTrimmedTop (36).reduced (3, 0); }
    int minimum() const noexcept { return mode == Mode::velocity ? 1 : 0; }
    int span() const noexcept { return 128 - minimum(); }
    int valueAt (int x) const { const auto a = area(); return juce::jlimit (minimum(), 127, minimum() + (int) std::floor ((x - a.getX()) * span() / (double) juce::jmax (1, a.getWidth()))); }
    Session::RackZone zoneOf (const Session::RackChainSettings& s) const { return mode == Mode::key ? s.key : mode == Mode::velocity ? s.velocity : s.select; }
    RackEditor& owner;
    Mode mode = Mode::key;
    Kind rackKind = Kind::instrumentRack;
    std::vector<Session::RackChainInfo> rows;
    // Drag state: which chain, which handle and the zone and value at the press.
    enum class Handle { none, low, high, fadeLow, fadeHigh, move };
    Handle handle = Handle::none;
    int dragRow = -1, pressValue = 0;
    Session::RackZone pressZone;
    bool gesture = false;
};

// A Drum Rack's pads: 16 of the 128 notes, bottom-left first, shifted in banks of 16.
class PadGrid final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    static constexpr int padWidth = 44, padHeight = 34;
    explicit PadGrid (RackEditor& o) : owner (o)
    {
        setName ("Drum pads");
        for (auto* b : { &up, &down }) { quiet (*b); addAndMakeVisible (*b); }
        up.setName ("Pads up"); down.setName ("Pads down");
        up.setTooltip ("Show the next 16 pads"); down.setTooltip ("Show the previous 16 pads");
        up.onClick = [this] { shift (16); }; down.onClick = [this] { shift (-16); };
    }
    void shift (int amount);
    juce::Rectangle<int> padBounds (int note) const
    {
        const int index = note - base;
        if (index < 0 || index >= 16) return {};
        return { 2 + (index % 4) * padWidth, 2 + (3 - index / 4) * padHeight, padWidth - 2, padHeight - 2 };
    }
    int noteAt (juce::Point<int> p) const
    {
        for (int note = base; note < base + 16 && note < 128; ++note) if (padBounds (note).contains (p)) return note;
        return -1;
    }
    void resized() override
    {
        auto strip = getLocalBounds().withTrimmedTop (4 * padHeight + 4).reduced (2, 1);
        down.setBounds (strip.removeFromLeft (strip.getWidth() / 2).reduced (1, 0)); up.setBounds (strip.reduced (1, 0));
    }
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override { pressed = noteAt (e.getPosition()); }
    void mouseUp (const juce::MouseEvent& e) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    int base = 36;
private:
    RackEditor& owner;
    juce::TextButton up { "Up" }, down { "Down" };
    int pressed = -1;
};

// The selected pad's chain: its received and played notes, choke group, mute, solo and sends.
class PadControls final : public juce::Component
{
public:
    explicit PadControls (RackEditor& o);
    void sync (const Session::RackChainInfo* chain, int pad, int returns);
    void resized() override;
    void paint (juce::Graphics& g) override;
    juce::ComboBox receive, play, choke;
    juce::TextButton mute { "Mute" }, solo { "Solo" }, addReturn { "+ Return" };
    std::array<juce::Slider, Session::maximumRackReturns> sends;
private:
    void change (const std::function<void (Session::RackChainSettings&)>&);
    RackEditor& owner;
    juce::String chainId;
    int padNote = 36, visibleSends = 0;
    bool gesture = false;
};

// The macro mappings with editable limits, and a row to add one.
class MappingList final : public juce::Component
{
public:
    explicit MappingList (RackEditor& o);
    void sync (const Session::PluginInfo& rack);
    void resized() override;
    void paint (juce::Graphics& g) override;
    struct Target { juce::String id; int parameter; juce::String label; float minimum, maximum; };
    juce::ComboBox macro, target;
    juce::TextButton add { "Map" };
    juce::OwnedArray<juce::Slider> minimums, maximums;
    juce::OwnedArray<juce::TextButton> removes;
    std::vector<Target> targets;
private:
    RackEditor& owner;
    std::vector<Session::MacroMapping> shown;
    juce::StringArray labels;
    bool gesture = false;
};

// One device of the selected chain: on/off, name and remove over its compact controls.
class ChainDevice final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    ChainDevice (RackEditor& o, const Session::PluginInfo& info);
    const juce::String deviceId;
    void sync (const Session::PluginInfo& info);
    int preferredWidth() const { return body ? body->getWidth() + 2 : 120; }
    int preferredHeight() const { return body ? body->getHeight() + 20 : 60; }
    void resized() override
    {
        auto title = getLocalBounds().removeFromTop (18).reduced (1);
        power.setBounds (title.removeFromLeft (16)); remove.setBounds (title.removeFromRight (16));
        if (body) body->setTopLeftPosition (1, 18);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (panel);
        g.setColour (enabled ? raised : panel.brighter (.05f)); g.fillRect (0, 0, getWidth(), 18);
        g.setColour (enabled ? ink : muted); g.setFont (font (10.5f, true));
        g.drawText (name, 20, 0, getWidth() - 40, 18, juce::Justification::centredLeft, true);
        g.setColour (line); g.drawRect (getLocalBounds());
    }
    void mouseDown (const juce::MouseEvent& e) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    juce::TextButton power { "" }, remove { "x" };
private:
    RackEditor& owner;
    juce::String name;
    bool enabled = true, sampler = false;
    std::unique_ptr<juce::Component> body;
};

class RackEditor final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    RackEditor (Session& s, juce::String id, std::function<void (const juce::String&, int)> automation)
        : session (s), rackId (std::move (id)), showAutomation (std::move (automation)), zones (*this), pads (*this), padControls (*this), mappings (*this)
    {
        setName ("Rack editor");
        for (int i = 0; i < Session::rackMacroCount; ++i)
        {
            auto& knob = macros[(size_t) i];
            knob.setName ("Macro " + juce::String (i + 1));
            knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); knob.setRange (0, 127, 0);
            knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 44, 12);
            knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            knob.setColour (juce::Slider::rotarySliderFillColourId, orange);
            knob.setNumDecimalPlacesToDisplay (0);
            knob.onDragStart = [this] { session.beginEditGesture ("Macro"); macroGesture = true; };
            knob.onDragEnd = [this] { finishMacroGesture(); };
            knob.onValueChange = [this, i] { setMacro (i, (float) macros[(size_t) i].getValue()); };
            knob.addMouseListener (this, false);
            quiet (knob); addChildComponent (knob);
            auto& label = macroLabels[(size_t) i];
            label.setFont (font (9.5f)); label.setJustificationType (juce::Justification::centred); label.setMinimumHorizontalScale (.6f);
            label.setInterceptsMouseClicks (false, false); addChildComponent (label);
        }
        fewer.setName ("Fewer macros"); more.setName ("More macros"); map.setName ("Map macros");
        fewer.setTooltip ("Show fewer macros"); more.setTooltip ("Show more macros (up to 16)");
        map.setTooltip ("Show macro mappings: map device controls, chain mixers or the chain selector and set their ranges");
        map.setClickingTogglesState (true); map.setColour (juce::TextButton::buttonOnColourId, green.withAlpha (.6f));
        fewer.onClick = [this] { report (session.setRackVisibleMacros (rackId, juce::jmax (1, rack.visibleMacros - 1))); };
        more.onClick = [this] { report (session.setRackVisibleMacros (rackId, juce::jmin (Session::rackMacroCount, rack.visibleMacros + 1))); };
        map.onClick = [this] { resized(); repaint(); };
        addChain.setName ("Add chain"); addChain.setTooltip ("Add an empty chain");
        addChain.onClick = [this] { juce::String id; if (report (session.addRackChain (rackId, {}, id))) selectChain (id); };
        addDevice.setName ("Add device to chain"); addDevice.setTooltip ("Add a device to the selected chain");
        addDevice.onClick = [this] { deviceMenu(); };
        for (auto* c : std::initializer_list<juce::Component*> { &fewer, &more, &map, &addChain, &addDevice }) { quiet (*c); addAndMakeVisible (*c); }
        chainView.setViewedComponent (&chainContent, false); chainView.setScrollBarsShown (true, false); chainView.setScrollBarThickness (6);
        chainView.setName ("Chain list"); addAndMakeVisible (chainView);
        deviceView.setViewedComponent (&deviceContent, false); deviceView.setScrollBarsShown (true, true); deviceView.setScrollBarThickness (6);
        deviceContent.setName ("Chain devices"); addAndMakeVisible (deviceView);
        addChildComponent (zones); addChildComponent (pads); addChildComponent (padControls); addChildComponent (mappings);
        error.setColour (juce::Label::textColourId, juce::Colour (0xfff4ab84)); error.setFont (font (9.5f)); addAndMakeVisible (error);
        session.addChangeListener (this);
        sync(); startTimerHz (15);
    }
    ~RackEditor() override { stopTimer(); session.removeChangeListener (this); finishMacroGesture(); chainView.setViewedComponent (nullptr, false); deviceView.setViewedComponent (nullptr, false); }

    void sync()
    {
        const auto snapshot = session.snapshot();
        const auto where = racks::locate (snapshot, rackId);
        available = where.device != nullptr && where.device->isRack && ! session.isExporting();
        if (where.device == nullptr || ! where.device->isRack) { setEnabled (false); return; }
        rack = *where.device; setEnabled (! session.isExporting());
        drum = rack.effect.kind == Kind::drumRack;
        // Keep the selected chain, or fall back to the selected pad's chain or the first chain.
        const auto exists = [this] (const juce::String& id) { for (const auto* list : { &rack.chains, &rack.returns }) for (const auto& c : *list) if (c.id == id) return true; return false; };
        if (! exists (selectedChain))
        {
            selectedChain.clear();
            if (drum) { for (const auto& c : rack.chains) if (c.settings.receiveNote == selectedPad) { selectedChain = c.id; break; } }
            else if (! rack.chains.empty()) selectedChain = rack.chains.front().id;
        }
        std::vector<juce::String> ids;
        for (const auto* list : { &rack.chains, &rack.returns }) for (const auto& c : *list) ids.push_back (c.id);
        bool same = ids.size() == rows.size();
        for (size_t i = 0; same && i < ids.size(); ++i) same = rows[i]->chainId == ids[i];
        if (! same)
        {
            rows.clear();
            for (const auto& id : ids) { rows.push_back (std::make_unique<ChainRow> (*this, id)); chainContent.addAndMakeVisible (*rows.back()); }
        }
        for (size_t i = 0; i < rows.size(); ++i)
            rows[i]->sync (i < rack.chains.size() ? rack.chains[i] : rack.returns[i - rack.chains.size()], rows[i]->chainId == selectedChain, i >= rack.chains.size());
        for (int i = 0; i < Session::rackMacroCount; ++i)
        {
            auto& knob = macros[(size_t) i];
            knob.setVisible (i < rack.visibleMacros); macroLabels[(size_t) i].setVisible (i < rack.visibleMacros);
            if (! knob.isMouseButtonDown()) knob.setValue (rack.effect.values[(size_t) i], juce::dontSendNotification);
            bool mapped = false; for (const auto& m : rack.mappings) mapped = mapped || m.macro == i;
            macroLabels[(size_t) i].setText (racks::macroName (rack, i), juce::dontSendNotification);
            macroLabels[(size_t) i].setColour (juce::Label::textColourId, mapped ? ink : muted);
        }
        fewer.setEnabled (rack.visibleMacros > 1); more.setEnabled (rack.visibleMacros < Session::rackMacroCount);
        const auto selector = racks::macroValue (rack, rack.id, Session::rackChainSelector);
        zones.sync (rack.chains, rack.effect.kind, (int) (selector ? *selector : rack.effect.values[(size_t) Session::rackChainSelector]), selector.has_value());
        pads.repaint();
        padControls.sync (selectedChainInfo(), selectedPad, (int) rack.returns.size());
        mappings.sync (rack);
        syncDevices();
        addDevice.setEnabled (selectedChainInfo() != nullptr);
        // The device view lays the rack out at its own width.
        if (const auto width = preferredWidth(); width != getWidth()) setSize (width, juce::jmax (getHeight(), editorHeight));
        resized(); repaint();
    }
    // Runs a command after the current event, for commands that delete the component handling it.
    void later (std::function<void (RackEditor&)> command)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RackEditor> (this), command = std::move (command)]
        { if (safe != nullptr) command (*safe); });
    }
    const Session::RackChainInfo* selectedChainInfo() const
    {
        for (const auto* list : { &rack.chains, &rack.returns }) for (const auto& c : *list) if (c.id == selectedChain) return &c;
        return nullptr;
    }
    bool isReturn (const juce::String& id) const { for (const auto& c : rack.returns) if (c.id == id) return true; return false; }
    void selectChain (const juce::String& id)
    {
        selectedChain = id;
        if (drum) for (const auto& c : rack.chains) if (c.id == id && c.settings.receiveNote >= 0) { selectedPad = c.settings.receiveNote; pads.base = juce::jlimit (0, 112, pads.base + 16 * (int) std::floor ((selectedPad - pads.base) / 16.0)); }
        sync();
    }
    void selectPad (int note)
    {
        selectedPad = note; selectedChain.clear();
        for (const auto& c : rack.chains) if (c.settings.receiveNote == note) { selectedChain = c.id; break; }
        sync();
    }
    int preferredWidth() const { return macroWidth() + (drum ? 4 * PadGrid::padWidth + 8 : 0) + listWidth() + middleWidth() + deviceWidth() + 12; }
    static constexpr int editorHeight = 236; // Room for a chain device's title over its compact controls.
    void resized() override
    {
        auto r = getLocalBounds().reduced (2);
        error.setBounds (r.removeFromBottom (12));
        auto macroArea = r.removeFromLeft (macroWidth());
        auto header = macroArea.removeFromTop (18);
        map.setBounds (header.removeFromLeft (34).reduced (1)); more.setBounds (header.removeFromRight (22).reduced (1)); fewer.setBounds (header.removeFromRight (22).reduced (1));
        const int columns = macroColumns();
        for (int i = 0; i < rack.visibleMacros; ++i)
        {
            const int column = i % columns, row = i / columns;
            juce::Rectangle<int> cell (macroArea.getX() + column * 50, macroArea.getY() + row * (macroArea.getHeight() / 2), 50, macroArea.getHeight() / 2);
            macroLabels[(size_t) i].setBounds (cell.removeFromTop (12));
            macros[(size_t) i].setBounds (cell.reduced (2, 0));
        }
        r.removeFromLeft (4);
        pads.setVisible (drum);
        if (drum) { pads.setBounds (r.removeFromLeft (4 * PadGrid::padWidth + 4).withHeight (4 * PadGrid::padHeight + 4 + 20)); r.removeFromLeft (4); }
        auto list = r.removeFromLeft (listWidth());
        addChain.setBounds (list.removeFromBottom (18).reduced (1, 0));
        chainView.setBounds (list);
        chainContent.setSize (juce::jmax (10, list.getWidth() - (list.getHeight() < (int) rows.size() * 20 ? 6 : 0)), juce::jmax (list.getHeight(), (int) rows.size() * 20));
        for (size_t i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, (int) i * 20, chainContent.getWidth(), 19);
        r.removeFromLeft (4);
        auto middle = r.removeFromLeft (middleWidth());
        const bool mapping = map.getToggleState();
        mappings.setVisible (mapping); zones.setVisible (! mapping && ! drum); padControls.setVisible (! mapping && drum);
        mappings.setBounds (middle); zones.setBounds (middle); padControls.setBounds (middle);
        r.removeFromLeft (4);
        addDevice.setBounds (r.removeFromLeft (20).withHeight (40));
        r.removeFromLeft (2);
        deviceView.setBounds (r);
        int x = 0, height = juce::jmax (40, r.getHeight() - 8);
        for (auto& d : devices) height = juce::jmax (height, d->preferredHeight());
        for (auto& d : devices) { d->setBounds (x, 0, d->preferredWidth(), height); x += d->preferredWidth() + 3; }
        deviceContent.setSize (juce::jmax (x, r.getWidth()), height);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (background);
        auto r = getLocalBounds().reduced (2);
        g.setColour (panel); g.fillRect (r.removeFromLeft (macroWidth()));
        if (! available) { g.setColour (muted); g.setFont (font (11.0f)); g.drawText ("Rack unavailable", getLocalBounds(), juce::Justification::centred); }
        if (selectedChainInfo() == nullptr && available)
        {
            g.setColour (muted); g.setFont (font (10.5f));
            g.drawFittedText (drum ? "Drop a sample on a pad, or add a chain" : "Add a chain, then devices to it", deviceView.getBounds().reduced (4), juce::Justification::centred, 2);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        for (int i = 0; i < Session::rackMacroCount; ++i)
            if (e.eventComponent == &macros[(size_t) i]) { macroMenu (i); return; }
    }
    bool report (const juce::Result& r)
    {
        error.setText (r.failed() ? r.getErrorMessage() : juce::String(), juce::dontSendNotification);
        if (r.wasOk()) sync();
        return r.wasOk();
    }
    void macroMenu (int macro)
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Show Automation for " + racks::macroName (rack, macro), (bool) showAutomation);
        int item = 100;
        std::vector<Session::MacroMapping> listed;
        for (const auto& m : rack.mappings)
            if (m.macro == macro) { listed.push_back (m); menu.addItem (item++, "Remove mapping " + juce::String ((int) listed.size())); }
        auto safe = juce::Component::SafePointer<RackEditor> (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&macros[(size_t) macro]), [safe, macro, listed] (int choice)
        {
            if (safe == nullptr || choice == 0) return;
            if (choice == 1 && safe->showAutomation) safe->showAutomation (safe->rackId, macro);
            else if (choice >= 100 && choice - 100 < (int) listed.size())
                safe->report (safe->session.removeRackMacroMapping (safe->rackId, macro, listed[(size_t) choice - 100].targetId, listed[(size_t) choice - 100].parameter));
        });
    }
    // Devices the selected chain accepts, grouped by browser category.
    void deviceMenu()
    {
        const auto* chain = selectedChainInfo(); if (chain == nullptr) return;
        const auto accepts = isReturn (chain->id) ? Kind::audioEffectRack : rack.effect.kind;
        juce::PopupMenu menu; juce::StringArray categories;
        for (int k = 0; k < Session::deviceKindCount; ++k)
            if (racks::chainAccepts (accepts, (Kind) k)) categories.addIfNotAlreadyThere (effectCategory ((Kind) k));
        for (const auto& category : categories)
        {
            juce::PopupMenu group;
            for (int k = 0; k < Session::deviceKindCount; ++k)
                if (racks::chainAccepts (accepts, (Kind) k) && category == effectCategory ((Kind) k)) group.addItem (k + 1, effectName ((Kind) k));
            menu.addSubMenu (category, group);
        }
        auto safe = juce::Component::SafePointer<RackEditor> (this); const auto id = chain->id;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addDevice), [safe, id] (int choice)
        { if (safe != nullptr && choice > 0) safe->addDeviceToChain (id, (Kind) (choice - 1)); });
    }
    void addDeviceToChain (const juce::String& chain, Kind kind) { juce::String id; report (session.addRackDevice (chain, kind, id)); }
    void setMacro (int macro, float value)
    {
        auto settings = rack.effect;
        if (settings.values[(size_t) macro] == value) return;
        settings.values[(size_t) macro] = value;
        report (session.setNativeEffectSettings (rackId, settings));
    }
    void setSelector (int value)
    {
        auto settings = rack.effect;
        if (settings.values[(size_t) Session::rackChainSelector] == (float) value) return;
        settings.values[(size_t) Session::rackChainSelector] = (float) value;
        report (session.setNativeEffectSettings (rackId, settings));
    }
    void finishMacroGesture() { if (macroGesture) { macroGesture = false; session.endEditGesture(); } }
    Session& session;
    const juce::String rackId;
    std::function<void (const juce::String&, int)> showAutomation;
    Session::PluginInfo rack;
    juce::String selectedChain;
    int selectedPad = 36;
    bool drum = false, available = false, macroGesture = false;
    std::array<juce::Slider, Session::rackMacroCount> macros;
    std::array<juce::Label, Session::rackMacroCount> macroLabels;
    juce::TextButton fewer { "-" }, more { "+" }, map { "Map" }, addChain { "+ Chain" }, addDevice { "+" };
    juce::Viewport chainView, deviceView;
    juce::Component chainContent, deviceContent;
    std::vector<std::unique_ptr<ChainRow>> rows;
    std::vector<std::unique_ptr<ChainDevice>> devices;
    ZoneEditor zones;
    PadGrid pads;
    PadControls padControls;
    MappingList mappings;
    juce::Label error;
private:
    int macroColumns() const noexcept { return juce::jmax (1, (rack.visibleMacros + 1) / 2); }
    int macroWidth() const noexcept { return 4 + macroColumns() * 50; }
    int listWidth() const noexcept { return drum ? 170 : 196; }
    int middleWidth() const noexcept { return map.getToggleState() ? 300 : drum ? 210 : 280; }
    int deviceWidth() const
    {
        int width = 0; for (const auto& d : devices) width += d->preferredWidth() + 3;
        return juce::jmax (160, width + 24);
    }
    void syncDevices()
    {
        const auto* chain = selectedChainInfo();
        std::vector<juce::String> ids;
        if (chain != nullptr) for (const auto& d : chain->devices) ids.push_back (d.id);
        bool same = ids.size() == devices.size();
        for (size_t i = 0; same && i < ids.size(); ++i) same = devices[i]->deviceId == ids[i];
        if (! same)
        {
            devices.clear();
            if (chain != nullptr) for (const auto& d : chain->devices) { devices.push_back (std::make_unique<ChainDevice> (*this, d)); deviceContent.addAndMakeVisible (*devices.back()); }
            if (auto* parent = getParentComponent()) parent->resized();
        }
        else if (chain != nullptr) for (size_t i = 0; i < devices.size(); ++i) devices[i]->sync (chain->devices[i]);
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { sync(); }
    void timerCallback() override { if (! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) sync(); }
};

void ChainRow::begin (const juce::String& name) { if (! gesture) { owner.session.beginEditGesture (name); gesture = true; } }
void ChainRow::end() { if (gesture) { gesture = false; owner.session.endEditGesture(); } }
void ChainRow::change (const std::function<void (Session::RackChainSettings&)>& edit)
{
    auto next = settings; edit (next);
    if (next == settings) return;
    settings = next;
    owner.report (owner.session.setRackChainSettings (chainId, next));
}
void ChainRow::soloClicked()
{
    // Like track solo, a plain click solos this chain alone; Cmd/Ctrl adds it to the soloed chains.
    const bool on = solo.getToggleState();
    const bool exclusive = ! juce::ModifierKeys::currentModifiers.isCommandDown();
    if (on && exclusive)
    {
        owner.session.beginEditGesture ("Solo chain");
        for (const auto& c : owner.rack.chains)
            if (c.id != chainId && c.settings.solo) { auto s = c.settings; s.solo = false; owner.session.setRackChainSettings (c.id, s); }
        change ([] (auto& s) { s.solo = true; });
        owner.session.endEditGesture();
        owner.sync(); return;
    }
    change ([on] (auto& s) { s.solo = on; });
}
void ChainRow::mouseDown (const juce::MouseEvent& e)
{
    owner.selectChain (chainId);
    if (! e.mods.isPopupMenu()) return;
    juce::PopupMenu menu;
    const bool isReturn = owner.isReturn (chainId);
    menu.addItem (1, "Duplicate", ! isReturn); menu.addItem (2, "Move up", ! isReturn); menu.addItem (3, "Move down", ! isReturn);
    menu.addSeparator(); menu.addItem (4, "Delete");
    auto safe = juce::Component::SafePointer<RackEditor> (&owner); const auto id = chainId;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, id] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        int index = 0; for (size_t i = 0; i < safe->rack.chains.size(); ++i) if (safe->rack.chains[i].id == id) index = (int) i;
        juce::String copy;
        if (choice == 1) safe->report (safe->session.duplicateRackChain (id, copy));
        else if (choice == 2) safe->report (safe->session.moveRackChain (id, juce::jmax (0, index - 1)));
        else if (choice == 3) safe->report (safe->session.moveRackChain (id, juce::jmin ((int) safe->rack.chains.size() - 1, index + 1)));
        else safe->report (safe->session.removeRackChain (id));
    });
}

void ZoneEditor::sync (const std::vector<Session::RackChainInfo>& chains, Kind kind, int selectorValue, bool selectorMapped)
{
    rows = chains;
    if (kind != rackKind)
    {
        rackKind = kind;
        if (kind == Kind::audioEffectRack) mode = Mode::select;
        resized();
    }
    selector.onValueChange = nullptr;
    if (! selector.isMouseButtonDown()) selector.setValue (selectorValue, juce::dontSendNotification);
    selector.setEnabled (! selectorMapped);
    selector.setTooltip (selectorMapped ? "The chain selector follows its macro" : "Chain selector: chains whose select zone holds this value are reached");
    selector.onDragStart = [this] { if (! gesture) { owner.session.beginEditGesture ("Chain selector"); gesture = true; } };
    selector.onDragEnd = [this] { if (gesture) { gesture = false; owner.session.endEditGesture(); } };
    selector.onValueChange = [this] { owner.setSelector ((int) selector.getValue()); };
    repaint();
}
void ZoneEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    const auto a = area();
    g.setColour (muted); g.setFont (font (8.5f));
    if (mode != Mode::select)
        for (int v = minimum() == 1 ? 1 : 0; v <= 127; v += mode == Mode::key ? 12 : 32)
        {
            g.drawVerticalLine (xOf (v), (float) a.getY() - 14, (float) a.getY() - 2);
            g.drawText (mode == Mode::key ? noteName (v) : juce::String (v), xOf (v) + 2, a.getY() - 15, 30, 12, juce::Justification::centredLeft);
        }
    for (size_t row = 0; row < rows.size(); ++row)
    {
        const auto zone = zoneOf (rows[row].settings);
        const int y = a.getY() + (int) row * rowHeight;
        g.setColour (line); g.drawHorizontalLine (y + rowHeight - 1, (float) a.getX(), (float) a.getRight());
        const int x0 = xOf (zone.low), x1 = xOf (zone.high + 1), f0 = xOf (zone.fadeLow), f1 = xOf (zone.fadeHigh + 1);
        // The full-level range is solid; fade ranges are lighter and its upper strip marks their edges.
        g.setColour (orange.withAlpha (.28f)); g.fillRect (x0, y + 5, juce::jmax (1, x1 - x0), rowHeight - 7);
        g.setColour (orange.withAlpha (.75f)); g.fillRect (f0, y + 5, juce::jmax (1, f1 - f0), rowHeight - 7);
        g.setColour (ink.withAlpha (.65f)); g.fillRect (f0, y + 1, juce::jmax (1, f1 - f0), 3);
    }
    if (mode == Mode::select)
    {
        g.setColour (ink); const int x = xOf ((int) selector.getValue()) + juce::jmax (1, a.getWidth() / 256);
        g.drawVerticalLine (x, (float) a.getY(), (float) a.getY() + (float) rows.size() * rowHeight);
    }
    g.setColour (line); g.drawRect (getLocalBounds());
}
void ZoneEditor::mouseDown (const juce::MouseEvent& e)
{
    handle = Handle::none; dragRow = -1;
    // Edge handles at either end stay reachable just outside the drawing area.
    const auto a = area();
    if (e.y < a.getY() || e.x < a.getX() - 4 || e.x > a.getRight() + 4) return;
    const int row = (e.y - a.getY()) / rowHeight;
    if (row < 0 || row >= (int) rows.size()) return;
    owner.selectChain (rows[(size_t) row].id);
    dragRow = row; pressZone = zoneOf (rows[(size_t) row].settings); pressValue = valueAt (e.x);
    const bool strip = e.y - (a.getY() + row * rowHeight) < 5;
    const auto near = [&] (int x) { return std::abs (e.x - x) <= 3; };
    if (strip) handle = std::abs (e.x - xOf (pressZone.fadeLow)) <= std::abs (e.x - xOf (pressZone.fadeHigh + 1)) ? Handle::fadeLow : Handle::fadeHigh;
    else if (near (xOf (pressZone.low))) handle = Handle::low;
    else if (near (xOf (pressZone.high + 1))) handle = Handle::high;
    else if (pressValue >= pressZone.low && pressValue <= pressZone.high) handle = Handle::move;
    if (handle != Handle::none && ! gesture) { owner.session.beginEditGesture ("Edit zone"); gesture = true; }
}
void ZoneEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (handle == Handle::none || dragRow < 0 || dragRow >= (int) rows.size()) return;
    const int value = valueAt (e.x), lowest = minimum();
    auto z = pressZone;
    switch (handle)
    {
        case Handle::low: z.low = juce::jlimit (lowest, z.high, value); z.fadeLow = juce::jmax (z.fadeLow, z.low); break;
        case Handle::high: z.high = juce::jlimit (z.low, 127, value); z.fadeHigh = juce::jmin (z.fadeHigh, z.high); break;
        case Handle::fadeLow: z.fadeLow = juce::jlimit (z.low, z.fadeHigh, value); break;
        case Handle::fadeHigh: z.fadeHigh = juce::jlimit (z.fadeLow, z.high, value); break;
        case Handle::move:
        {
            const int delta = juce::jlimit (lowest - pressZone.low, 127 - pressZone.high, value - pressValue);
            z.low += delta; z.high += delta; z.fadeLow += delta; z.fadeHigh += delta; break;
        }
        case Handle::none: break;
    }
    auto settings = rows[(size_t) dragRow].settings;
    (mode == Mode::key ? settings.key : mode == Mode::velocity ? settings.velocity : settings.select) = z;
    if (settings == rows[(size_t) dragRow].settings) return;
    rows[(size_t) dragRow].settings = settings;
    owner.report (owner.session.setRackChainSettings (rows[(size_t) dragRow].id, settings));
}
void ZoneEditor::mouseUp (const juce::MouseEvent&) { handle = Handle::none; if (gesture) { gesture = false; owner.session.endEditGesture(); } }

void PadGrid::shift (int amount) { base = juce::jlimit (0, 112, base + amount); repaint(); }
void PadGrid::paint (juce::Graphics& g)
{
    g.fillAll (background);
    for (int note = base; note < base + 16; ++note)
    {
        const auto b = padBounds (note);
        if (note > 127) { g.setColour (panel.darker (.3f)); g.fillRect (b); continue; }
        std::vector<const Session::RackChainInfo*> chains;
        for (const auto& c : owner.rack.chains) if (c.settings.receiveNote == note) chains.push_back (&c);
        const bool filled = ! chains.empty();
        bool silenced = filled, soloed = false;
        for (const auto* c : chains) { silenced = silenced && ! c->settings.active; soloed = soloed || c->settings.solo; }
        g.setColour (filled ? (silenced ? raised.darker (.2f) : juce::Colour (0xff6b6152)) : raised); g.fillRect (b);
        if (note == owner.selectedPad) { g.setColour (orange); g.drawRect (b, 2); }
        if (soloed) { g.setColour (blue); g.fillRect (b.getRight() - 6, b.getY() + 2, 4, 4); }
        g.setColour (filled && ! silenced ? ink : muted); g.setFont (font (8.5f, true));
        g.drawText (noteName (note), b.reduced (3, 1).removeFromTop (10), juce::Justification::topLeft);
        g.setFont (font (8.5f));
        g.drawFittedText (chains.size() > 1 ? juce::String ("Multi") : filled ? chains.front()->settings.name : juce::String(),
                          b.reduced (3, 1).withTrimmedTop (10), juce::Justification::centredLeft, 2);
    }
}
void PadGrid::mouseUp (const juce::MouseEvent& e)
{
    const int released = noteAt (e.getPosition());
    if (pressed >= 0 && released >= 0 && released != pressed)
    {
        // Dragging one pad onto another swaps their notes.
        owner.report (owner.session.swapDrumRackPads (owner.rackId, pressed, released));
        owner.selectPad (released);
    }
    else if (pressed >= 0 && released == pressed) owner.selectPad (pressed);
    pressed = -1;
}
bool PadGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (files.size() != 1) return false;
    const juce::File file (files[0]);
    return file.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}
void PadGrid::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int note = noteAt ({ x, y });
    if (note < 0) return;
    juce::String chain;
    if (owner.report (owner.session.setDrumRackPadSample (owner.rackId, note, juce::File (files[0]), chain))) owner.selectPad (note);
}

PadControls::PadControls (RackEditor& o) : owner (o)
{
    setName ("Pad I/O");
    receive.setName ("Pad receive"); play.setName ("Pad play note"); choke.setName ("Pad choke");
    receive.setTooltip ("The note this pad's chain receives"); play.setTooltip ("The note the chain's devices play");
    choke.setTooltip ("Pads in the same choke group silence each other");
    receive.addItem ("All Notes", 1);
    for (int note = 0; note < 128; ++note) { receive.addItem (noteName (note), note + 2); play.addItem (noteName (note), note + 1); }
    choke.addItem ("None", 1); for (int group = 1; group <= 16; ++group) choke.addItem (juce::String (group), group + 1);
    receive.onChange = [this] { const int note = receive.getSelectedId() - 2; change ([note] (auto& s) { s.receiveNote = note; }); };
    play.onChange = [this] { const int note = play.getSelectedId() - 1; change ([note] (auto& s) { s.playNote = note; }); };
    choke.onChange = [this] { const int group = choke.getSelectedId() - 1; change ([group] (auto& s) { s.chokeGroup = group; }); };
    mute.setName ("Pad mute"); solo.setName ("Pad solo"); addReturn.setName ("Add return");
    mute.setClickingTogglesState (true); solo.setClickingTogglesState (true);
    mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffcf4a)); solo.setColour (juce::TextButton::buttonOnColourId, blue);
    mute.onClick = [this] { const bool off = mute.getToggleState(); change ([off] (auto& s) { s.active = ! off; }); };
    solo.onClick = [this] { const bool on = solo.getToggleState(); change ([on] (auto& s) { s.solo = on; }); };
    addReturn.setTooltip ("Add a return chain (up to six) that pads can send to");
    addReturn.onClick = [this] { juce::String id; owner.report (owner.session.addRackReturn (owner.rackId, {}, id)); };
    for (int i = 0; i < Session::maximumRackReturns; ++i)
    {
        auto& s = sends[(size_t) i];
        s.setName (sendName (i)); s.setSliderStyle (juce::Slider::LinearBar); s.setRange (-100, 0, .1); s.setSkewFactorFromMidPoint (-18);
        s.setDoubleClickReturnValue (true, -100); s.textFromValueFunction = [i] (double v) { return sendName (i) + "  " + (v <= -100 ? juce::String ("Off") : juce::String (v, 1) + " dB"); };
        s.onDragStart = [this] { if (! gesture) { owner.session.beginEditGesture ("Pad send"); gesture = true; } };
        s.onDragEnd = [this] { if (gesture) { gesture = false; owner.session.endEditGesture(); } };
        s.onValueChange = [this, i] { const auto v = (float) sends[(size_t) i].getValue(); change ([i, v] (auto& c) { c.sendsDb[(size_t) i] = v; }); };
        quiet (s); addChildComponent (s);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &receive, &play, &choke, &mute, &solo, &addReturn }) { quiet (*c); addAndMakeVisible (*c); }
}
void PadControls::sync (const Session::RackChainInfo* chain, int pad, int returns)
{
    padNote = pad; visibleSends = returns;
    const bool isPadChain = chain != nullptr && ! owner.isReturn (chain->id);
    chainId = isPadChain ? chain->id : juce::String();
    for (auto* c : std::initializer_list<juce::Component*> { &receive, &play, &choke, &mute, &solo }) c->setVisible (isPadChain);
    addReturn.setEnabled (returns < Session::maximumRackReturns);
    if (isPadChain)
    {
        const auto& s = chain->settings;
        receive.setSelectedId (s.receiveNote + 2, juce::dontSendNotification);
        play.setSelectedId (s.playNote + 1, juce::dontSendNotification); play.setEnabled (s.receiveNote >= 0);
        choke.setSelectedId (s.chokeGroup + 1, juce::dontSendNotification); choke.setEnabled (s.receiveNote >= 0);
        mute.setToggleState (! s.active, juce::dontSendNotification); solo.setToggleState (s.solo, juce::dontSendNotification);
    }
    for (int i = 0; i < Session::maximumRackReturns; ++i)
    {
        auto& slider = sends[(size_t) i];
        slider.setVisible (isPadChain && i < returns);
        if (isPadChain && ! slider.isMouseButtonDown()) slider.setValue (chain->settings.sendsDb[(size_t) i], juce::dontSendNotification);
        slider.updateText();
    }
    resized(); repaint();
}
void PadControls::change (const std::function<void (Session::RackChainSettings&)>& edit)
{
    if (chainId.isEmpty()) return;
    const auto* chain = owner.selectedChainInfo(); if (chain == nullptr || chain->id != chainId) return;
    auto next = chain->settings; edit (next);
    if (next == chain->settings) return;
    owner.report (owner.session.setRackChainSettings (chainId, next));
}
void PadControls::resized()
{
    auto r = getLocalBounds().reduced (4).withTrimmedTop (16);
    auto first = r.removeFromTop (20);
    receive.setBounds (first.removeFromLeft (first.getWidth() / 2).reduced (1, 0)); play.setBounds (first.reduced (1, 0));
    r.removeFromTop (3);
    auto second = r.removeFromTop (20);
    choke.setBounds (second.removeFromLeft (70).reduced (1, 0)); mute.setBounds (second.removeFromLeft (44).reduced (1, 0)); solo.setBounds (second.removeFromLeft (44).reduced (1, 0));
    r.removeFromTop (3);
    addReturn.setBounds (r.removeFromBottom (18).removeFromRight (70));
    for (int i = 0; i < visibleSends; ++i) sends[(size_t) i].setBounds (r.removeFromTop (14).withTrimmedBottom (1));
}
void PadControls::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (ink); g.setFont (font (10.5f, true));
    g.drawText ("Pad " + noteName (padNote), 6, 2, getWidth() - 12, 14, juce::Justification::centredLeft);
    if (chainId.isEmpty())
    {
        g.setColour (muted); g.setFont (font (10.0f));
        g.drawFittedText ("Empty pad. Drop a sample on it to make a chain.", getLocalBounds().reduced (8).withTrimmedTop (16).withTrimmedBottom (22), juce::Justification::centred, 3);
    }
    g.setColour (line); g.drawRect (getLocalBounds());
}

MappingList::MappingList (RackEditor& o) : owner (o)
{
    setName ("Macro mappings");
    macro.setName ("Mapping macro"); target.setName ("Mapping target"); add.setName ("Add mapping");
    for (int i = 0; i < Session::rackMacroCount; ++i) macro.addItem ("Macro " + juce::String (i + 1), i + 1);
    macro.setSelectedId (1, juce::dontSendNotification);
    add.setTooltip ("Map the chosen macro to the chosen target over the target's whole range");
    add.onClick = [this]
    {
        const int index = target.getSelectedId() - 1;
        if (index < 0 || index >= (int) targets.size()) return;
        const auto& t = targets[(size_t) index];
        owner.report (owner.session.setRackMacroMapping (owner.rackId, { macro.getSelectedId() - 1, t.id, t.parameter, t.minimum, t.maximum }));
    };
    for (auto* c : std::initializer_list<juce::Component*> { &macro, &target, &add }) { quiet (*c); addAndMakeVisible (*c); }
}
void MappingList::sync (const Session::PluginInfo& rack)
{
    // Every control a macro can drive: the selector, chain mixers and devices directly in chains.
    std::vector<Target> next;
    next.push_back ({ rack.id, Session::rackChainSelector, "Chain Selector", 0, 127 });
    for (const auto* list : { &rack.chains, &rack.returns })
        for (const auto& c : *list)
        {
            const bool isReturn = list == &rack.returns;
            next.push_back ({ c.id, 0, c.settings.name + " Volume", -100, 6 });
            next.push_back ({ c.id, 1, c.settings.name + " Pan", -1, 1 });
            if (! isReturn) for (size_t k = 0; k < rack.returns.size(); ++k) next.push_back ({ c.id, 2 + (int) k, c.settings.name + " " + sendName ((int) k), -100, 0 });
            for (const auto& d : c.devices)
            {
                const auto fields = effectParameters (d.effect.kind);
                for (size_t p = 0; p < fields.size(); ++p)
                    next.push_back ({ d.id, (int) p, c.settings.name + " / " + d.name + " " + fields[p].label, fields[p].minimum, fields[p].maximum });
            }
        }
    juce::StringArray nextLabels; for (const auto& t : next) nextLabels.add (t.label);
    if (nextLabels != labels)
    {
        const auto previous = target.getSelectedId();
        labels = nextLabels; targets = std::move (next);
        target.clear (juce::dontSendNotification);
        for (int i = 0; i < labels.size(); ++i) target.addItem (labels[i], i + 1);
        target.setSelectedId (previous > 0 && previous <= labels.size() ? previous : 1, juce::dontSendNotification);
    }
    else targets = std::move (next);
    const auto key = [] (const std::vector<Session::MacroMapping>& list)
    {
        std::vector<std::tuple<int, juce::String, int>> keys;
        for (const auto& m : list) keys.emplace_back (m.macro, m.targetId, m.parameter);
        return keys;
    };
    if (key (rack.mappings) == key (shown) && minimums.size() == (int) shown.size())
    {
        // Same mappings: follow their limits without replacing a slider that may be dragging.
        shown = rack.mappings;
        for (size_t i = 0; i < shown.size(); ++i)
        {
            if (! minimums[(int) i]->isMouseButtonDown()) minimums[(int) i]->setValue (shown[i].minimum, juce::dontSendNotification);
            if (! maximums[(int) i]->isMouseButtonDown()) maximums[(int) i]->setValue (shown[i].maximum, juce::dontSendNotification);
        }
    }
    else
    {
        shown = rack.mappings;
        minimums.clear(); maximums.clear(); removes.clear();
        for (size_t i = 0; i < shown.size(); ++i)
        {
            const auto m = shown[i];
            const auto range = [&]
            {
                for (const auto& t : targets) if (t.id == m.targetId && t.parameter == m.parameter) return std::pair { t.minimum, t.maximum };
                return std::pair { 0.0f, 1.0f };
            }();
            for (auto* list : { &minimums, &maximums })
            {
                auto* s = list->add (new juce::Slider());
                const bool isMinimum = list == &minimums;
                s->setName ((isMinimum ? "Mapping minimum " : "Mapping maximum ") + juce::String ((int) i + 1));
                s->setSliderStyle (juce::Slider::LinearBar); s->setRange (range.first, range.second, 0);
                s->setValue (isMinimum ? m.minimum : m.maximum, juce::dontSendNotification);
                s->setNumDecimalPlacesToDisplay (range.second - range.first > 20 ? 1 : 2);
                s->onDragStart = [this] { if (! gesture) { owner.session.beginEditGesture ("Mapping range"); gesture = true; } };
                s->onDragEnd = [this] { if (gesture) { gesture = false; owner.session.endEditGesture(); } };
                s->onValueChange = [this, i, isMinimum, s]
                {
                    if (i >= shown.size()) return;
                    auto changed = shown[i]; (isMinimum ? changed.minimum : changed.maximum) = (float) s->getValue();
                    owner.report (owner.session.setRackMacroMapping (owner.rackId, changed));
                };
                quiet (*s); addAndMakeVisible (*s);
            }
            auto* remove = removes.add (new juce::TextButton ("x"));
            remove->setName ("Remove mapping " + juce::String ((int) i + 1)); quiet (*remove); addAndMakeVisible (*remove);
            remove->onClick = [this, m] { owner.later ([m] (RackEditor& editor) { editor.report (editor.session.removeRackMacroMapping (editor.rackId, m.macro, m.targetId, m.parameter)); }); };
        }
        resized();
    }
    repaint();
}
void MappingList::resized()
{
    auto r = getLocalBounds().reduced (3);
    auto top = r.removeFromTop (18);
    macro.setBounds (top.removeFromLeft (70)); add.setBounds (top.removeFromRight (34)); target.setBounds (top.reduced (2, 0));
    r.removeFromTop (16);
    for (int i = 0; i < minimums.size(); ++i)
    {
        auto row = r.removeFromTop (14).withTrimmedBottom (1);
        removes[i]->setBounds (row.removeFromRight (14));
        minimums[i]->setBounds (row.removeFromRight (56)); row.removeFromRight (2); maximums[i]->setBounds (row.removeFromRight (56));
        r.removeFromTop (10);
    }
}
void MappingList::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (muted); g.setFont (font (9.0f));
    auto r = getLocalBounds().reduced (3).withTrimmedTop (20);
    g.drawText ("Max            Min", r.removeFromTop (14).withTrimmedRight (16), juce::Justification::centredRight);
    for (size_t i = 0; i < shown.size(); ++i)
    {
        juce::String label = "Macro " + juce::String (shown[i].macro + 1) + ": ";
        for (const auto& t : targets) if (t.id == shown[i].targetId && t.parameter == shown[i].parameter) label += t.label;
        g.setColour (ink); g.drawText (label, r.removeFromTop (10), juce::Justification::centredLeft, true);
        r.removeFromTop (14);
    }
    if (shown.empty()) { g.setColour (muted); g.drawFittedText ("Choose a macro and a target, then Map.", r, juce::Justification::centred, 2); }
    g.setColour (line); g.drawRect (getLocalBounds());
}

ChainDevice::ChainDevice (RackEditor& o, const Session::PluginInfo& info) : deviceId (info.id), owner (o)
{
    setName ("Chain device " + info.name);
    power.setName ("Chain device on"); power.setClickingTogglesState (true); power.setTooltip ("Turn this device on or off");
    power.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffcf4a));
    remove.setName ("Remove chain device"); remove.setTooltip ("Remove this device from the chain");
    power.onClick = [this] { owner.report (owner.session.setRackDeviceEnabled (deviceId, power.getToggleState())); };
    remove.onClick = [this] { owner.later ([id = deviceId] (RackEditor& editor) { editor.report (editor.session.removeRackDevice (id)); }); };
    for (auto* b : { &power, &remove }) { quiet (*b); addAndMakeVisible (*b); }
    auto automation = [safe = juce::Component::SafePointer<RackEditor> (&o), id = info.id] (int parameter)
    { if (safe != nullptr && safe->showAutomation) safe->showAutomation (id, parameter); };
    body = info.isRack ? makeRackEditor (o.session, info.id, o.showAutomation) : makeNativeEffectEditor (o.session, info.id, true, automation);
    if (info.isRack) body->setSize (static_cast<RackEditor&> (*body).preferredWidth(), RackEditor::editorHeight);
    addAndMakeVisible (*body);
    sync (info);
}
void ChainDevice::sync (const Session::PluginInfo& info)
{
    name = info.name; enabled = info.enabled; sampler = info.effect.kind == Kind::sampler;
    power.setToggleState (enabled, juce::dontSendNotification);
    if (body != nullptr && info.isRack)
        if (const auto width = static_cast<RackEditor&> (*body).preferredWidth(); width != body->getWidth()) body->setSize (width, body->getHeight());
    repaint();
}
void ChainDevice::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || e.y > 18) return;
    const auto* chain = owner.selectedChainInfo(); if (chain == nullptr) return;
    int index = 0; for (size_t i = 0; i < chain->devices.size(); ++i) if (chain->devices[i].id == deviceId) index = (int) i;
    juce::PopupMenu menu;
    menu.addItem (1, "Move left", index > 0); menu.addItem (2, "Move right", index + 1 < (int) chain->devices.size());
    menu.addSeparator(); menu.addItem (3, enabled ? "Deactivate" : "Activate"); menu.addItem (4, "Delete");
    auto safe = juce::Component::SafePointer<RackEditor> (&owner); const auto id = deviceId; const bool on = enabled;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, id, index, on] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice <= 2) safe->report (safe->session.moveRackDevice (id, index + (choice == 1 ? -1 : 1)));
        else if (choice == 3) safe->report (safe->session.setRackDeviceEnabled (id, ! on));
        else safe->report (safe->session.removeRackDevice (id));
    });
}
bool ChainDevice::isInterestedInFileDrag (const juce::StringArray& files)
{ return sampler && files.size() == 1 && juce::File (files[0]).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"); }
void ChainDevice::filesDropped (const juce::StringArray& files, int, int)
{ owner.report (owner.session.setRackDeviceSample (deviceId, juce::File (files[0]))); }
}

#if HALFTRACK_TESTS_ENABLED
juce::Point<int> rackZonePoint (juce::Component& zoneEditor, int row, int value, bool fadeStrip)
{
    auto* zones = dynamic_cast<ZoneEditor*> (&zoneEditor);
    return zones != nullptr ? zones->pointFor (row, value, fadeStrip) : juce::Point<int>();
}
juce::Rectangle<int> rackPadBounds (juce::Component& padGrid, int note)
{
    auto* pads = dynamic_cast<PadGrid*> (&padGrid);
    return pads != nullptr ? pads->padBounds (note) : juce::Rectangle<int>();
}
#endif
std::unique_ptr<juce::Component> makeRackEditor (Session& session, const juce::String& rackId, std::function<void (const juce::String&, int)> showAutomation)
{
    auto editor = std::make_unique<RackEditor> (session, rackId, std::move (showAutomation));
    editor->setSize (editor->preferredWidth(), RackEditor::editorHeight);
    return editor;
}
}
