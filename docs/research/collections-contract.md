# Browser collections contract

Halftrack's browser collections follow the reference browser's Collections: a set of coloured tags shown at the top of the browser's sidebar, which any browser item can carry, so favourite devices, presets, samples, grooves and plug-ins are a click away.

## Behaviour

- **Collections.** There are seven: Favorites, Red, Yellow, Green, Blue, Purple and Grey. The colour names are Halftrack's default; the reference's own names and colours are not recorded here.
- **Dragging.** A browser item dropped on a collection in the sidebar joins it ([dragging](browser-places-contract.md#dragging-2026-10-24)).
- **Names and colours.** As the reference's labels can be renamed and given colours ([Working with the Browser](https://www.ableton.com/en/live-manual/12/working-with-the-browser/), section 4.5, consulted 2026-10-24), a collection's right-click menu has Rename... and the colour grid of the tracks' and clips' palette. A name is trimmed and at most 64 characters; an empty one gives the collection its usual name again. Names and colours are saved in `Collections.xml` with the items.
- **Tagging.** With a browser item selected, the keys 1 to 7 toggle it in a collection. A right-click lists the collections, ticked where the item belongs, and toggles the one chosen. A tagged item shows its collections' colours at the right of its row.
- **Items.** Each item is named by a key: `device:<kind>` for built-in devices, `preset:<kind>:<path>` for presets, `groove:<name>` and `groovefile:<path>` for grooves, `sample:<path>` for audio files and `plugin:<identifier>` for plug-ins. An item whose file or plug-in no longer exists is left out of the collection's list until it returns.
- **Listing.** Choosing a collection in the sidebar lists its items in the order they were added, filtered by the search field. Double-click or Return uses an item as it would be used in its own category: devices and presets load on the selected track, samples import or load into a sampler, and plug-ins insert.
- **Storage.** Collections are kept in `Collections.xml` in the user library and written atomically on each change. A change that cannot be saved is refused and undone, and a file that cannot be read is refused without losing the collections. Without a user library they last for the run.

## Evidence

`tests/CollectionsTests.cpp` covers:

- toggling, order, items in several collections, and forgetting an item in none;
- renaming and colouring a collection, the name and colour reloading, an empty name restoring the usual one, and refusals of unknown collections and long names;
- saving and reloading, and refusing an empty key or an eighth collection;
- refusing a malformed file while keeping the collections, and a change that cannot be saved (outside Windows).

The UI suite checks a collection's menu (Rename and its colours), colours one from a swatch and finds a renamed one named in the browser. It also puts the Saturator in Favorites and a preset in Green, lists and loads them from their collections, reloads the saved file, and takes the Saturator out again.

## Not yet covered

Hiding collections in the reference's Edit mode, library-wide search outside the current category, and comparison with the reference's browser behaviour.
