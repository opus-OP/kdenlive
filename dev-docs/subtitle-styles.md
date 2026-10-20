# Shared Subtitle Styles and Animations

Kdenlive subtitles are stored in an ASS-compatible subtitle file that is rendered
through MLT's [`avfilter.subtitles`][subtitle-mlt-doc] filter (see the
[file format](fileformat.md#subtitle-track) description). This document describes
two related features built on top of that:

* a **shared (cross-project) subtitle style library**, so a style can be reused in
  every project instead of being copied around, and
* **animations attached to a style**, which are turned into ASS override tags when
  the subtitle file is written, so they show up identically in the monitor preview
  and in the final render.

## Scopes and precedence

A subtitle style lives in exactly one *scope*. From lowest to highest precedence:

| Scope | UI label | Storage |
| --- | --- | --- |
| Built-in | *(listed under Shared as “(built-in)”)* | read-only sets shipped with Kdenlive |
| Shared | **Shared** | user style sets in the application data directory |
| Project | **Project** | `globalSubtitleStyles` property of the project file |
| Subtitle file | the subtitle file itself | `[V4+ Styles]` of the current `.ass`/subtitle file |

Styles are resolved by name, searching from the highest scope to the lowest, so a
project style **overrides** a shared style with the same name (and a style written
in the subtitle file overrides both). The project style list marks such styles with
*“overrides shared style”*. Built-in styles cannot be edited or deleted; saving a
style with a built-in name creates a user copy that overrides it.

The `Manage Subtitles` dialog (Subtitles menu) exposes the three editable scopes as
sidebar entries and lets styles be dragged or copied between them.

> The project property is still called `globalSubtitleStyles` for backward
> compatibility; only the user-visible label changed from “Global” to “Project”.

## Storage locations

* **Built-in sets** are installed with Kdenlive and found through
  `QStandardPaths::AppLocalDataLocation` with the `subtitle-styles/` subdirectory,
  e.g. `/usr/share/kdenlive/subtitle-styles/` (or `<prefix>/share/kdenlive/subtitle-styles/`).
  The set shipped with Kdenlive is `default.ass`.
* **Shared user sets** live in
  `QStandardPaths::writableLocation(AppLocalDataLocation) + "/subtitle-styles/"`, on Linux
  `~/.local/share/kdenlive/subtitle-styles/`. `AppLocalDataLocation` is used (like for the title
  templates) because it is the directory KNewStuff's `TargetDir` resolves to on every platform;
  `AppDataLocation` differs from it on Windows and would turn downloaded sets into read-only
  “built-in” ones.
* **Project styles** are stored in the `.kdenlive` project file, not on their own.

One file is one *style set*; the file's base name is the set name. The library is
implemented by `SubtitleStyleLibrary` (`src/bin/model/subtitlestylelibrary.{h,cpp}`)
and reached through `SubtitleModel::styleLibrary()`.

## Style set file format

A style set is a regular ASS file, so other tools can read it. Kdenlive only uses
the `[V4+ Styles]` section to build the styles, but the whole file is round-tripped:
unknown keys in the `[Kdenlive Extradata]` section are preserved when the set is
rewritten. Animations are stored in that section: `Animation:` lines hold the
entrance animation and, since the exit animation feature, `AnimationOut:` lines
hold the exit animation of the same style:

```
[Script Info]
; Kdenlive subtitle style library
ScriptType: v4.00+

[Kdenlive Extradata]
LibraryVersion: 1
Animation: News|kind=fade;dur=400;delay=0;dir=0;intensity=1;ease=1
AnimationOut: News|kind=fade;dur=400;delay=0;dir=0;intensity=1;ease=1
Animation: Lower third|kind=move;dur=600;delay=0;dir=0;intensity=1;ease=1
AnimationOut: Lower third|kind=erase;dur=400;delay=0;dir=0;intensity=1;ease=1

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: News,Arial,36,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100.00,100.00,0.00,0.00,1,2.00,1.00,2,10,10,20,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
```

Each line has the form `Animation: <StyleName>|<params>`, where `<params>` is a
semicolon-separated `key=value` list:

| Key | Meaning | Default |
| --- | --- | --- |
| `kind` | `none`, `fade`, `move`, `scale`, `rotate`, `typewriter` or `erase` | `none` |
| `dur` | length of the animation in milliseconds | `500` |
| `delay` | distance from the event edge, in milliseconds (entrance: after the start; exit: before the end) | `0` |
| `dir` | direction, meaning depends on `kind` (see below) | `0` |
| `intensity` | strength, currently only used by `scale` | `1` |
| `ease` | `1`/`0`, whether `\t` animations decelerate towards their end (`scale`, `rotate`); ASS has no true ease-in-out curve | `1` |

Parsing is tolerant: unknown keys and values that cannot be parsed are ignored,
negative durations/delays are clamped, and an `Animation:` line for an unknown style
is left untouched when the set is rewritten. `AnimationOut:` uses exactly the same
parameter format; a style without an `AnimationOut:` line simply has no exit
animation. The per-character kinds `typewriter` (entrance) and `erase` (exit) are
independent: any combination of an entrance and an exit kind is allowed.

The animations of a **project** style are serialized in the project file as
`animation` and `animationOut` fields next to the style, using the same
`kind=...;dur=...` encoding.

## Animation kinds

A style carries two independent animations: an **entrance** (`Animation:`) and an
**exit** (`AnimationOut:`). Each one is translated into ASS override tags placed in a
single override block prepended to the event text; the per-character kinds
(`typewriter`, `erase`) wrap every character in its own block instead. All times are
**milliseconds relative to the start of the event** and are clamped to the event
duration; the exit window is anchored at the **end** of the event, so the event
disappears exactly when it leaves the timeline (see *Timing* below).

| `kind` | Side | Generated tags | `dir` | `intensity` | `delay` |
| --- | --- | --- | --- | --- | --- |
| `fade` | in | `\fad(in,0)` | unused | unused | **ignored** (`\fad` has no delay argument) |
| `fade` | out | `\fad(0,out)` | unused | unused | **ignored** |
| `move` | in | `{\anN\move(x1,y1,x2,y2,t1,t2)}` | `0` from bottom, `1` from top, `2` from left, `3` from right | unused | shifts the whole move |
| `move` | out | `{\anN\move(x1,y1,x2,y2,t1,t2)}` | same, the text leaves towards `dir` | unused | shifts the whole move |
| `scale` | both | `\pos(x,y)\fscxS\fscyS\t(t1,t2,\fscxT\fscyT)` | unused | target scale (`>1` grows, `<1` shrinks) | shifts `t1`/`t2` |
| `rotate` | both | `\pos(x,y)\org(x,y)\frz0\t(t1,t2,\frzA)` | `0` = 360°, `1` = -90°, `2` = +90°, `3` = 180° | unused | shifts `t1`/`t2` |
| `typewriter` | in | `{\alpha&HFF&\t(a,b,\alpha&H00&)}<char>` per character | unused | unused | shifts the first character; `dur` is the total typing time |
| `erase` | out | `{\alpha&H00&\t(a,b,\alpha&HFF&)}<char>` per character | `0` = left to right, `1` = right to left | unused | shifts the first erased character; `dur` is the total erasing time |

When **both** sides are a `fade` the generator emits a single `\fad(in,out)`. When
one side is a `fade` and the other is `typewriter`/`erase`, the `\fad` cannot be
combined with the per-character `\t(\alpha)` (see *Known limitations*), so the fade
is generated per character as well. `\move` and `\pos` are mutually exclusive and
only one is emitted; two slides cannot coexist in one event, so a `move` exit after a
`move` entrance degrades to a `fade` out. Coordinates are derived from the style
alignment (`\anN`), the project frame size and the style's margins, using the
formulas below (the horizontal anchor honours the margins, slides start outside of the frame).

### Timing

The entrance window is `[delay, delay + dur]` from the event start. The exit window
is `[duration - delay - dur, duration - delay]`, i.e. it ends at the end of the event
minus its own `delay`. If `dur` does not fit (a very short event) the entrance and the
exit are **shrunk proportionally** instead of one of them being dropped; if the
delays alone already fill the event they are dropped first.

## Presets

The style editor (`src/dialogs/subtitlestyleedit.{h,cpp}`) presents the two
animations as two tabs, **Appearance** and **Disappearance**, each with its own set
of presets and timing fields. Only the fields relevant to the selected preset are
enabled:

| Preset | `kind` | `dir` | Notes |
| --- | --- | --- | --- |
| None | `none` | — | animation disabled |
| Fade | `fade` | — | delay field disabled; an entrance fade fades in, an exit fade fades out |
| Slide from bottom / top / left / right | `move` | `0`/`1`/`2`/`3` | direction fixed by the preset |
| Zoom in | `scale` | — | default `intensity` 1.5 |
| Zoom out | `scale` | — | default `intensity` 0.5 |
| Rotate | `rotate` | configurable | for a full turn set `dir` to `0` |
| Typewriter | `typewriter` | — | entrance tab only |
| Erase | `erase` | `0`/`1` | exit tab only |

The style list in `Manage Subtitles` shows the active animations next to the style
name and in its tooltip.

## Import and export

The style list's context menu offers:

* **Import Style Set…** — copies a `.ass` file into the shared user directory
  (a numeric suffix is appended if a set with that name already exists) and reloads
  the library.
* **Export Style Set…** — when the selection is a shared style, exports its whole
  set; otherwise exports the styles of the currently selected scope as a new set.

`Export Subtitle` (Manage Subtitles → file menu) exports the persisted *editing* file, not the file
read by the subtitles filter: the latter carries generated animation tags, which would be applied a
second time if the exported file was imported again.

## KNewStuff downloads

The KDE store integration is a small configuration file,
`data/knewstuff/kdenlive_subtitle_styles.knsrc`, registered as a Qt resource in
`src/uiresources.qrc` (alias `kdenlive_subtitle_styles.knsrc`) and installed next to
the other `.knsrc` files by `data/knewstuff/CMakeLists.txt`. Its key settings are:

```ini
TargetDir=kdenlive/subtitle-styles
Uncompress=archive
```

`TargetDir` is relative to the generic data location, so downloaded archives are
extracted exactly into `~/.local/share/kdenlive/subtitle-styles/` — the same
directory the shared library reads (this is why the target is `kdenlive/subtitle-styles`
and not just `subtitle-styles`).

`Manage Subtitles` creates a `KNSWidgets::Action` for this config file and adds it to
the style list's context menu as **“Download New Subtitle Styles…”**. The action hides
itself when KNewStuff is disabled via `KAuthorized`. When the download dialog is
closed, the handler reloads the shared library and emits `libraryChanged()` (which is
wired to `SubtitleModel::modelChanged()`), then repopulates the style list, so
newly downloaded sets are usable immediately without restarting Kdenlive.

## Known limitations

These were established empirically against the libass version shipped with MLT and are
reflected in the generated tags:

* **The `Effect` column wins.** A `Banner;…`/`Scroll up;…` effect completely overrides
  `\move`/`\pos`. For events that already carry an effect, no animation tag is emitted.
* **Two slides cannot coexist.** libass allows a single `\move` per event, so when both
  the entrance and the exit are `move` the exit degrades to a `fade`.
* **`\move` and `\pos` are mutually exclusive.** Exactly one of them is generated.
* **`\t` needs an explicit start value.** The generator always emits the initial
  `\fscx`/`\fscy`/`\frz`/`\alpha` before the `\t(...)` transition.
* **`\fad` and `\t(\alpha)` must not be mixed**, and `\fad` has no delay argument, so
  **`delay` is ignored for fades**. When one side of an event is `typewriter`/`erase`,
  every fade of that event is generated per character instead of with `\fad`.
* **Per-character soft cap.** A `typewriter` entrance or an `erase` exit is degraded to
  a `fade` when the text is longer than 500 characters or contains user-authored
  override blocks (`{…}`), which the per-character generator cannot animate safely.
* **Emoji render monochrome** with the libass version shipped with MLT; do not rely on
  colour emoji in animations.
* **User ASS tags are not escaped or rewritten**, so author-written override blocks are
  preserved (and, as noted, suppress the per-character animations).
* **SRT/VTT export loses styling and animation** (soft degradation: text and timings
  are kept).

## Tests

Unit tests live in `tests/subtitlestest.cpp` and cover the style cascade, the set
round-trip, the `Animation:`/`AnimationOut:` serialization, the generated ASS tags
for each kind (entrance, exit and both together) and the render/editing file split.
Run them with:

```
ctest --test-dir <build-dir> -R subtitle
```

## Further reading

* [Kdenlive file format](fileformat.md)
* [Subtitle track in the file format](fileformat.md#subtitle-track)
* [FFmpeg subtitles filter documentation][subtile-ffmpeg-doc]
* [MLT `avfilter.subtitles` documentation][subtitle-mlt-doc]

[subtitle-mlt-doc]: https://www.mltframework.org/plugins/FilterAvfilter-subtitles/
[subtile-ffmpeg-doc]: https://ffmpeg.org/ffmpeg-filters.html#subtitles-1
