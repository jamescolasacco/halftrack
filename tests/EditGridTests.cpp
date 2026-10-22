#include "Session.h"
#include <cmath>
#include <limits>
#include <iostream>
#include <stdexcept>

// The Arrangement's editing grid: the reference's defaults, its fixed values and tuplets, the
// adaptive step at a zoom, the names the Arrangement shows, validation, and that it is saved with
// the project without being an undo step or an unsaved change. A damaged setting reads as the default.
namespace halftrack
{
namespace
{
void check (bool v, const juce::String& text)
{ if (! v) throw std::runtime_error (text.toStdString()); std::cout << "PASS edit grid: " << text << std::endl; }
void ok (const juce::Result& r, const juce::String& text) { check (r.wasOk(), text + (r.failed() ? ": " + r.getErrorMessage() : "")); }
bool near (double a, double b) { return std::abs (a - b) < 1e-9; }
Session::EditGrid fixedGrid (int numerator, int denominator, int tuplet = 2)
{
    Session::EditGrid g; g.fixed = true; g.numerator = numerator; g.denominator = denominator; g.tuplet = tuplet;
    return g;
}
}

int runEditGridTests (const juce::File& artifacts)
{
    try
    {
        const auto root = artifacts.getChildFile ("edit-grid-" + juce::Uuid().toString()); ok (root.createDirectory(), "create fixtures");
        const Session::TimeSignature common, waltz { 3, 4 }, compound { 6, 8 };
        const Session::EditGrid defaults;
        check (! defaults.fixed && defaults.numerator == 1 && defaults.denominator == 16 && defaults.intervalPixels == 20 && defaults.tuplet == 2 && defaults.snap,
               "the default is the reference's: adaptive at 20 pixels, a 1/16 fixed value, no tuplets, snapping on");

        // The ladder, and fixed values in quarter-note beats.
        const auto ladder = Session::gridLadder();
        check (ladder.size() == 24 && ladder.front() == Session::GridValue { 512, 1 } && ladder[9] == Session::GridValue { 1, 1 } && ladder[10] == Session::GridValue { 1, 2 }
                   && ladder.back() == Session::GridValue { 1, 16384 }, "the ladder runs from 512 bars through 1 bar and 1/2 to 1/16384");
        const auto step = [&] (const Session::EditGrid& g, double ppb, Session::TimeSignature meter, double visible = std::numeric_limits<double>::infinity())
        { return Session::gridStepBeats (g, ppb, visible, meter); };
        check (near (step (fixedGrid (1, 16), 100, common), .25) && near (step (fixedGrid (1, 2), 100, common), 2) && near (step (fixedGrid (1, 32), 100, common), .125),
               "fixed note values: 1/16 is a quarter beat, 1/2 two beats, 1/32 an eighth");
        check (near (step (fixedGrid (1, 1), 100, common), 4) && near (step (fixedGrid (2, 1), 100, waltz), 6) && near (step (fixedGrid (1, 1), 100, compound), 3),
               "bars follow the meter: 4 beats in 4/4, two bars of 3/4 are 6, a bar of 6/8 is 3");
        check (near (step (fixedGrid (1, 8, 3), 100, common), 1.0 / 3) && near (step (fixedGrid (1, 16, 5), 100, common), .1)
                   && near (step (fixedGrid (1, 4, 7), 100, common), 2.0 / 7) && near (step (fixedGrid (1, 1, 3), 100, common), 4),
               "a tuplet step is 2/n of its note value: 1/8 triplets a third of a beat, 1/16 quintuplets a tenth; bars take none");
        check (Session::gridValueName ({ 1, 16 }, 2) == "1/16" && Session::gridValueName ({ 1, 8 }, 3) == "1/8T" && Session::gridValueName ({ 1, 16 }, 5) == "5 : 1/8"
                   && Session::gridValueName ({ 1, 8 }, 7) == "7 : 1/4" && Session::gridValueName ({ 1, 1 }, 3) == "1/1" && Session::gridValueName ({ 2, 1 }, 2) == "2/1",
               "steps read 1/16, 1/8T, 5 : 1/8 for quintuplets, and bars 1/1 and 2/1");

        // Adaptive: the step nearest the spacing on a log scale, lines more than 8 pixels apart,
        // shorter than half the view.
        check (near (step (defaults, 80, common), .25), "at 80 pixels a beat, 20-pixel spacing gives sixteenths");
        check (near (step (defaults, 112, common), .25) && near (step (defaults, 114, common), .125),
               "the choice changes where the spacing passes the geometric mean of two steps: 1/16 at 112 pixels a beat, 1/32 at 114");
        Session::EditGrid tight; tight.intervalPixels = 1;
        check (near (step (tight, 80, common), .125), "however tight the spacing, lines stay more than 8 pixels apart");
        Session::EditGrid wide; wide.intervalPixels = 1000000;
        check (near (step (wide, 80, common, 16), 4) && near (step (wide, 80, common), 512 * 4), "and a step is shorter than half the view (here 16 beats), up to 512 bars");
        check (near (step (defaults, 1, common), 16), "zoomed out to a pixel a beat the nearest is 4 bars");
        check (near (step (defaults, 4, waltz), 6), "in 3/4 at 4 pixels a beat, the half note is too fine and two bars of three beats are nearest");
        Session::EditGrid triplets; triplets.tuplet = 3;
        check (near (step (triplets, 80, common), 1.0 / 3), "with triplets the nearest triplet value is chosen");
        Session::EditGrid narrowest; narrowest.intervalPixels = Session::narrowestGrid;
        check (near (step (narrowest, 80, common), .125), "the narrowest spacing allows finer lines");

        // Validation, persistence and history.
        Session s (false);
        check (s.editGrid() == defaults, "a new project has the default grid");
        auto bad = defaults; bad.numerator = 3;
        check (s.setEditGrid (bad).failed() && s.setEditGrid (fixedGrid (1, 3)).failed() && s.setEditGrid ([] { Session::EditGrid g; g.intervalPixels = 0; return g; }()).failed()
                   && s.setEditGrid ([] { Session::EditGrid g; g.tuplet = 4; return g; }()).failed() && s.editGrid() == defaults,
               "values off the ladder, a spacing below a pixel and unknown tuplets are refused");
        ok (s.setEditGrid (fixedGrid (1, 64)), "a fixed value finer than the menu's, as narrowing reaches");
        ok (s.setEditGrid (defaults), "and back");
        ok (s.saveProject (root.getChildFile ("Grid").getChildFile ("Grid.halftrack")), "save a project");
        const bool couldUndo = s.canUndo();
        auto chosen = fixedGrid (1, 8, 3); chosen.snap = false; chosen.intervalPixels = 31;
        ok (s.setEditGrid (chosen), "choose a fixed 1/8 triplet grid with snapping off");
        check (s.editGrid() == chosen && s.canUndo() == couldUndo && ! s.snapshot().dirty, "it is not an undo step or an unsaved change");
        ok (s.saveProject (s.snapshot().projectFile), "save it");
        {
            Session reopened (false);
            ok (reopened.loadProject (s.snapshot().projectFile), "reopen");
            check (reopened.editGrid() == chosen, "the grid is saved with the project");
        }
        ok (s.newProject(), "a new project");
        check (s.editGrid() == defaults, "and a new project starts from the default again");
        // A damaged setting reads as the default.
        const auto file = root.getChildFile ("Grid").getChildFile ("Grid.halftrack");
        auto text = file.loadFileAsString();
        check (text.contains ("HALFTRACKGRID"), "the saved project holds the grid");
        ok (file.replaceWithText (text.replace ("tuplet=\"3\"", "tuplet=\"9\"")) ? juce::Result::ok() : juce::Result::fail ("could not write"), "damage its tuplet");
        ok (s.loadProject (file), "open the damaged project");
        check (s.editGrid() == defaults, "a damaged grid reads as the default");
        root.deleteRecursively();
        std::cout << "Edit grid passed" << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL edit grid: " << e.what() << std::endl; return 1; }
}
}
