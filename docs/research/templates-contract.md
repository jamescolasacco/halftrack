# Templates contract

Status: partial. This follows the reference's manual and static menu research. No reference output was measured.

## Evidence

- Live 12 manual, [Managing Files and Sets](https://www.ableton.com/en/live-manual/12/managing-files-and-sets/), section 5.4.5, consulted 2026-10-24: File > Save Live Set As Template... saves the current Set as a template in the User Library's Templates folder, made the first time one is saved, and templates appear in the browser's Templates label. A template loads with its saved configuration but as an untitled Set, ready to be used as a new one. Any template can be made the default through the Set Default Live Set entry of its context menu.
- Live 12 manual, [Working with the Browser](https://www.ableton.com/en/live-manual/12/working-with-the-browser/), section 4.6, consulted 2026-10-24: the Library lists Grooves, then Templates, then Tunings.
- The reference's File menu builder, read statically for the [menu bar contract](main-menu-contract.md), adds Save as Template (`svtp`) after Save a Copy and Save Self-Contained, before Save as Default Set.

## Halftrack's behaviour

- **Saving.** File > Save as Template... asks for a name, the project's at first, and writes the project to `Templates/<name>.halftrack` in the User Library, making the folder when needed. A template of that name already there is replaced only when the user chooses Replace. A template is written as the default set is: the project as it is, its media where it was.
- **The Templates category.** The Library's Templates, after Grooves, lists the templates in the folder and its folders by name, filtered by the search.
- **Opening.** A double-click or Return opens a template as a new, untitled, unchanged project, asking first to save changes.
- **Set Default Set.** A template's right-click menu has Set Default Set, which makes new projects start from it: the template is copied over the User Library's default set, atomically.
- Templates have no collections, as other projects have none.

## Not yet

- Collecting a template's media into the Templates folder, and templates in packs.
- Renaming and deleting templates from the browser.

See [open questions](../open-questions.md) for the defaults taken where the manual is silent.

## Evidence in Halftrack

The UI suite checks that the File menu offers Save as Template, that the Templates category follows Grooves and starts empty, that a saved template lands in the Templates folder and is listed by name, that opening it gives a new untitled, unchanged project with its tracks, and that Set Default Set makes File > New start from it.
