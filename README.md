# BodySlide and OutfitStudio Improvements

Five interface improvements for **BodySlide and Outfit Studio 5.6.3**, shipped as a
standalone DLL. No original file is modified or redistributed.

## What it does

### BodySlide

**Search in the "Choose Groups" dialog.** With hundreds of groups installed, finding one
in that list is painful. There is now a search box that filters the list as you type,
plus **Check visible** and **Clear all** buttons and a counter.

Groups that get hidden by the filter **stay checked**. The filter is display-only, so
you can search "3BA", check a few, clear the search, look for "HIMBO", check more, and
hit OK with all of them selected.

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

**`Shift+E` and `Shift+I`** only fire while a slider is in **Edit mode** — the same
condition Outfit Studio itself uses to enable those menu entries. Outside edit mode the
key passes through untouched.

**`F` resizes the brush like Blender does:** press `F`, move the mouse horizontally to
grow or shrink the circle, left click to confirm. `Esc` or right click cancels and
restores the previous size. The cursor stays pinned while you drag, so the brush changes
size without moving. Needs an active brush (keys `1`–`9`); with the Select tool it does
nothing.

No shortcut fires while a text field has focus — typing "B" into a filter types a "b".

## Installation

**`msimg32.dll` has to sit in the same folder as `BodySlide x64.exe`.** This is not a
tidiness preference: it is a static import of the executables, and Windows resolves
static imports from the `.exe`'s own directory, before any virtualisation layer gets a
say.

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

Requires the **x64** executables (`BodySlide x64.exe` and `OutfitStudio x64.exe`). The
32-bit ones still launch fine, just without the improvements.

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

## Compatibility

- **BodySlide and Outfit Studio 5.6.3.** Nothing here depends on memory addresses or
  byte signatures, only on standard Windows messages, so nearby versions should work. If
  something cannot be found on a newer build, that piece disables itself silently instead
  of breaking the program.
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

```
build.bat              builds dist\msimg32.dll
tests\build_tests.bat  builds and runs the tests
package.bat            builds dist-package\, the contents of the Nexus archive
install.bat            installs into a BodySlide folder for testing
```

## License

MIT — see [LICENSE](LICENSE).

This is an independent add-on. It contains no code or assets from BodySlide and Outfit
Studio, which is a separate GPL-3.0 project, and it links against nothing of theirs. It
interacts with those programs only through standard Windows messages, so the two
licenses do not interact.

`tests/data/OutfitStudio.xrc` is deliberately not committed: it belongs to the BodySlide
project. Copy it from your own install to run the test that uses it.

## Credits

BodySlide and Outfit Studio are by **ousnius**. This mod contains none of their code or
files; it only talks to the program through standard Windows messages.
