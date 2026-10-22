# Interface themes and display zoom

Status: partial, UX-005. Halftrack's own palettes; the reference only sets the expectation that a DAW offers light and dark themes and a display zoom (its Display & Input and Theme & Colors preferences).

## Behaviour

**Choosing.** Preferences has Theme (Dark, Light, High Contrast) and Zoom Display (50-200% in steps of 25). Both are kept in Halftrack's preferences file, with the other preferences, and an out-of-range value marks the file unreadable as any other does. The interface starts in the saved theme. Choosing another applies it at once, without reopening the window or losing the view.

**Themes.** Every interface colour is authored for Dark. `themes::colour` maps it into the current theme:

- **Dark** shows the colours as authored.
- **Light** inverts the gray ramp in luminance, about the contrast formula's offset: (L + 0.05)(L′ + 0.05) = k. Every contrast ratio between two grays is kept, so Dark's dark panels and light text become light panels and dark text, equally readable. Colours keep their hue at half the brightness, so accents and meters stand out from light panels.
- **High Contrast** spreads the dark grays (so separators still show), pushes the light grays toward white, and keeps colours as they are.

Each mapping has an inverse, `themes::toDark`.

**Applying.** Painting reads the theme as it draws. A switch also:

- re-applies the look-and-feel colours;
- recolours every colour a component set on itself, in every window and dialog. Each component keeps those colours' Dark originals, so switching back and forth does not drift, and a colour a control has changed since is followed;
- tells components the look changed, so editors with looks of their own (the clip and automation editors) re-apply them;
- sets the window's background.

**Zoom.** Zoom Display sets the interface's global scale factor. Every window and its text scale together.

**Secondary text.** Dark's secondary text is a little brighter than before (0x969696, was 0x8e8e8e), to reach 4.5:1 on panels.

## Evidence

`tests/ThemeTests.cpp` checks, for each theme:

- that the palette and sample colours map back to their Dark originals, to rounding;
- WCAG 2 contrast ratios: text on backgrounds, panels and buttons at least 7:1; secondary text at least 4.5:1; selected menu text and selection and mapping accents at least 3:1; clip and meter marks at least 3:1; separators visible (at least 1.2:1);
- disabled text at least 2.5:1 and clearly dimmer than enabled text (WCAG sets no minimum for inactive controls);
- Light's light panels with dark text, and High Contrast's darker panels with brighter text.

The measured ratios:

| Pair | Dark | Light | High Contrast |
| --- | --- | --- | --- |
| Text | 7.65 | 7.73 | 9.24 |
| Secondary text | 4.66 | 4.68 | 9.65 |
| Selected menu text | 5.68 | 3.27 | 5.59 |
| Accents | 4.15 | 4.21 | 4.27 |
| Disabled text | 3.68 | 2.82 | 4.32 |
| Meter marks | 4.74 | 5.33 | 4.99 |
| Separators | 1.34 | 1.33 | 1.37 |

The UI suite checks:

- that Preferences offer the three themes and the zoom, every preference fitting in its panel;
- for Light and High Contrast, chosen from Preferences: the theme saved and applied, the look-and-feel and a control's own colour following, and the rendered window painting in it;
- back to Dark, every colour exactly as authored;
- 150% zoom scaling the interface, and 100% restoring it.

Renders of each theme are written for inspection when snapshots are asked for.

## Not yet covered

- The reference's own theme files and named themes.
- Following the system's light or dark appearance.
- Hue, saturation and grid-intensity adjustments.
- A colour-blind-safe palette for clip and track colours.
- Plug-in windows, which keep the plug-in's own look.
- Checking that every layout fits at the smallest logical sizes high zoom leaves.
