# BodySlide and OutfitStudio Improvements

Interface improvements for **BodySlide and Outfit Studio**, shipped as a standalone DLL.
No original file is modified or redistributed.

## What it does

### BodySlide

**Search in the "Choose Groups" dialog.** With hundreds of groups installed, finding one
in that list is painful. There is now a search box that filters the list as you type,
plus **Check visible** and **Clear all** buttons and a counter.

Groups that get hidden by the filter **stay checked**. The filter is display-only, so
you can search "3BA", check a few, clear the search, look for "HIMBO", check more, and
hit OK with all of them selected.

**Search in the "Batch Build" dialog.** A search box above the outfit list jumps to the
first match as you type; `Enter` cycles through the rest, wrapping around at the end.

This one *jumps* rather than filtering, and that is deliberate: Batch Build reads which
outfits to build straight from the list control, so hiding rows would change what gets
built. Nothing is added or removed — only scrolled to — so your checkboxes are exactly
what you set them to.

**`Z` zeroes every slider.** One key puts the whole panel back to zero, instead of
dragging each slider down by hand. There is no undo for it in BodySlide, so if you find
yourself hitting it by accident, `Alt+Z` is one line away in the INI.

### Outfit Studio

**The reference comes pre-selected.** When you load an outfit, Outfit Studio selects the
first mesh in the list. Now it selects the *reference* shape instead — the green one in
bold. This also works when the project is opened from BodySlide or by double-clicking an
`.osp` file.

It only acts on a fresh load. Deleting, renaming or adding a shape in an already open
project **does not** touch your selection.

| Shortcut | What it does |
|---|---|
| `B` | Select the reference shape |
| `Shift+E` | Export Slider Data ▸ Export OBJ |
| `Shift+I` | Import Slider Data ▸ Import OBJ |
| `F` | Blender-style brush resize |
| `K` | Transform (moved off `F`) |
| `Z` | Zero every slider in the panel |

**`Shift+E` and `Shift+I`** only fire while a slider is in **Edit mode** — the same
condition Outfit Studio itself uses to enable those menu entries. Outside edit mode the
key passes through untouched.

**`F` resizes the brush like Blender does:** press `F`, move the mouse horizontally to
grow or shrink the circle, left click to confirm. `Esc` or right click cancels and
restores the previous size. The cursor stays pinned while you drag, so the brush changes
size without moving. Needs an active brush (keys `1`–`9`); with the Select tool it does
nothing.

No shortcut fires while a text field has focus — typing "B" into a filter types a "b".

**Tooltips say the shortcut.** Hovering a toolbar button now shows the key in
parentheses — *"Shows a transform tool. **(K)**"*. The key is not hardcoded: it is read
from the menu entry of the same command, which is why it survives translations and why
it says `K` and not `F` once the `[Remap]` below has moved Transform out of the way.

Tools whose key lives only inside Outfit Studio's own code — the digits that switch
brushes — have nothing to read, so they show nothing. If you want them labelled anyway,
list them under `[TooltipShortcuts]` in the INI. Turn the log on and the mod prints how
many tools it found with and without a shortcut.

**Search in the "Symmetrize Vertices" dialog.** That dialog (and its twin, "Mask
Symmetric Vertices") lists one row per slider and one per bone, which on a real project
is hundreds of rows. A search box at the top hides the rows that do not match.

It only hides them: a row hidden by the filter **keeps its check mark**, the same
contract as the Choose Groups search. Clearing the box brings everything back. The list
closes its own gaps because the hiding is done in a way wxWidgets understands, so the
scrollbar stays honest.

**Mirror bone pose.** Move the right thigh in pose mode and the left one follows, by the
same amount, with the sign flipped on the axes that mirror.

It fires when you let go of the slider, not while you drag it. Mirroring works by
switching the bone list to the other bone, writing there, and switching back — doing that
on every mouse move would make the sliders jump under the cursor. Bones with no side
(`NPC Root`, `Pelvis`, `NPC Spine`) are left alone, and so is any bone whose mirror is not
in the list.

Which axes flip is a `[MirrorPose]` setting, because there is no universal answer — it
depends on how your skeleton orients each bone's local axes. The defaults are the Skyrim
convention. If a mirrored pose bends the wrong way, flip one line and reload; with the
log on, the mod prints the values it wrote.

**Blender-style camera** *(off by default)*. The **middle** mouse button orbits and
**Shift+middle** pans, like Blender. The right button keeps orbiting the way it always
did, so nothing you already know stops working.

This is the only feature that ships off, because it does not add anything — it swaps
navigation you already have in your hands. Turn it on in the INI and a **Blender camera**
entry appears at the end of the **View** menu, so you can flip it back and forth without
restarting.

## Installation

**`msimg32.dll` has to sit in the same folder as the BodySlide executable.** This is not
a tidiness preference: it is a static import of the executables, and Windows resolves
static imports from the `.exe`'s own directory, before any virtualisation layer gets a
say.

That folder is `CalienteTools\BodySlide\`. On 5.6 and 5.7 the executable there is
`BodySlide x64.exe`; **5.8.0 dropped the suffix**, so it is just `BodySlide.exe`. Either
way, the DLL goes right next to it — host detection matches the name, not the suffix.

**One copy covers both programs.** The Outfit Studio executable lives in that same folder
and has no folder of its own, so there is no second copy to make.

**Mod Organizer 2 — do not install this as a separate mod.** MO2's VFS maps mods into
the game's `Data` folder, never into another mod's folder, so a standalone mod would
never be loaded. Do one of these instead:

- install the archive **over** your "BodySlide and Outfit Studio" mod, choosing to merge
  when MO2 asks; or
- copy the two files straight into
  `mods\BodySlide and Outfit Studio\CalienteTools\BodySlide\`.

**Vortex or manual:** extract the archive contents into the folder containing
`BodySlide x64.exe`, so that `msimg32.dll` ends up beside it.

Launching BodySlide through MO2 keeps working normally — MO2 starts the executable from
its real path, which is exactly where the DLL lives.

Requires a **64-bit** executable. On 5.6 and 5.7 that is the one with the `x64` suffix;
the 32-bit build still launches fine, just without the improvements. 5.8.0 dropped the
suffix, so there the plain name is already the 64-bit build.

To uninstall, delete `msimg32.dll` and `BSOSImprovements.ini`.

> If you update or reinstall BodySlide, redo this step: the files live in its folder.

## Configuration

Everything lives in `BSOSImprovements.ini`, next to the DLL. Every feature can be turned
off individually and every shortcut can be changed.

The `[Remap]` section binds **any** Outfit Studio command to **any** key. That is how
`F` is freed up for the brush resize: Transform is moved to `K`. If you would rather have
`R` select the reference, for example:

```ini
[Hotkeys]
SelectReference=R

[Remap]
btnRecalcNormals=N
```

Command names come from `CalienteTools\BodySlide\res\xrc\OutfitStudio.xrc` — find the
menu text and read the `name=` of the surrounding `<object>`.

Two sections use that same vocabulary. `[TooltipShortcuts]` only *labels* a tooltip
without binding anything, which is what the brush digits need since Outfit Studio handles
those keys internally:

```ini
[TooltipShortcuts]
btnInflateBrush=3
```

`[MirrorPose]` decides which axes flip when a pose is mirrored:

```ini
[MirrorPose]
NegateRotationY=1
NegateOffsetX=1
```

## Compatibility

- **Verified against 5.8.2**, and developed originally against 5.6.3. Nothing here
  depends on memory addresses or byte signatures, only on standard Windows messages, so
  newer versions should keep working — including 5.8.0, which renamed the executables. If
  something cannot be found on a newer build, that piece disables itself silently instead
  of breaking the program.
- **Dark mode is followed.** 5.8.2 added a light/dark setting, and everything this mod
  draws itself follows it: the Choose Groups replacement and the Batch Build and
  Symmetrize search boxes read `AppearanceMode` from BodySlide's own `Config.xml`, so
  they match whatever you picked in Settings — including `System`, which tracks the
  Windows theme.
- **Works alongside other mods that use `version.dll`**, such as draping mods. This one
  deliberately uses the `msimg32.dll` slot instead.
- BodySlide translations are supported: nothing is identified by interface text.

## Reporting a problem

Turn the log on and reproduce the issue:

```ini
[Debug]
LogFile=1
```

The files land in `%TEMP%\BSOSImprovements_BodySlide.log` and
`%TEMP%\BSOSImprovements_OutfitStudio.log`.

## Build

Needs Visual Studio 2022 Build Tools with the C++ workload.

```text
build.bat              builds dist\msimg32.dll
tests\build_tests.bat  builds and runs the tests
```

Packaging and install scripts are not in the repository — they point at local paths.

The tests run on plain Windows: they build real menus, trees and dialogs with the Win32
API and assert against them, so they cover the parts that actually break — menu path
resolution, dialog identification, hotkey matching. One test wants
`tests\data\OutfitStudio.xrc`, which belongs to BodySlide and is not redistributed here;
copy it from your own install to run it, or it reports itself as skipped.

## License

MIT — see [LICENSE](LICENSE).

This is an independent add-on. It contains no code or assets from BodySlide and Outfit
Studio, which is a separate GPL-3.0 project, and it links against nothing of theirs. It
interacts with those programs only through standard Windows messages, so the two
licenses do not interact.

No file belonging to the BodySlide project is committed here — the mod reads the `.xrc`
resources from your own installation at runtime.

## Credits

BodySlide and Outfit Studio are by **ousnius**. This mod contains none of their code or
files; it only talks to the program through standard Windows messages.
