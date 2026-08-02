BodySlide and OutfitStudio Improvements
=======================================

Five interface improvements for BodySlide and Outfit Studio 5.6.3.
Nothing in the original install is modified or replaced.


WHAT IT DOES
------------

BodySlide
  A search box in the "Choose Groups" dialog. Filters the list as you type,
  with "Check visible" and "Clear all" buttons. Groups hidden by the filter
  stay checked, so you can search, check, search again, and hit OK with all
  of them selected.

Outfit Studio
  Loading a project selects the reference shape (green, bold) instead of the
  first mesh in the list.

  B          select the reference shape
  Shift+E    Export Slider Data > Export OBJ    (needs a slider in Edit mode)
  Shift+I    Import Slider Data > Import OBJ    (needs a slider in Edit mode)
  F          Blender-style brush resize
  K          Transform (moved off F)

  F: press it, move the mouse sideways to grow or shrink the circle, left
  click to confirm. Esc or right click cancels. The circle stays put while
  you drag. Needs an active brush (keys 1-9).


INSTALL
-------

msimg32.dll must sit in the SAME FOLDER as the BodySlide executable. It is a
static import of the executables, so Windows loads it from the .exe's own
directory, before any virtualisation layer gets a say.

That folder is CalienteTools\BodySlide\. On 5.6 and 5.7 the executable there is
"BodySlide x64.exe"; 5.8.0 dropped the suffix, so it is just "BodySlide.exe".
Either way, the DLL goes right next to it.

One copy covers both programs: the Outfit Studio executable lives in that same
folder and has no folder of its own. You do not need a second copy anywhere.

  Mod Organizer 2   Do NOT install as a separate mod -- MO2 maps mods into
                    the game's Data folder, never into another mod's folder,
                    so it would never load. Install this archive OVER your
                    "BodySlide and Outfit Studio" mod and choose to merge,
                    or copy the two files into
                    mods\BodySlide and Outfit Studio\CalienteTools\BodySlide\

  Vortex / manual   Extract into the folder containing the BodySlide
                    executable.

Launching BodySlide through MO2 keeps working normally.

Requires a 64-bit executable. On 5.6 and 5.7 that is the one with the x64
suffix; the 32-bit build still starts, just without the improvements. 5.8.0
dropped the suffix and the plain name is the 64-bit build.

Developed and verified against 5.6.3. Nothing depends on memory addresses or
byte signatures, so newer versions should work, and anything that cannot be
found disables itself rather than breaking the program.

To uninstall, delete msimg32.dll and BSOSImprovements.ini.
If you update BodySlide, redo this: the files live in its folder.


CONFIGURATION
-------------

Everything is in BSOSImprovements.ini, next to the DLL. Every feature can be
switched off and every shortcut changed.

The [Remap] section binds any Outfit Studio command to any key. That is how F
is freed up for the brush resize -- Transform is moved to K. To put the
reference on R instead, for example:

  [Hotkeys]
  SelectReference=R

  [Remap]
  btnRecalcNormals=N

Command names come from CalienteTools\BodySlide\res\xrc\OutfitStudio.xrc.


COMPATIBILITY
-------------

Built for 5.6.3, but nothing depends on memory addresses or byte signatures --
only on standard Windows messages -- so nearby versions should work. Anything
that cannot be found disables itself instead of breaking the program.

Works alongside mods that use version.dll, such as draping mods; this one uses
the msimg32.dll slot. BodySlide translations are supported: nothing is matched
by interface text.


PROBLEMS
--------

Set LogFile=1 under [Debug], reproduce, and attach:
  %TEMP%\BSOSImprovements_BodySlide.log
  %TEMP%\BSOSImprovements_OutfitStudio.log


CREDITS
-------

BodySlide and Outfit Studio are by ousnius. This mod contains none of their
code or files.
