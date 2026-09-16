BodySlide and OutfitStudio Improvements
=======================================

Drop-in interface improvements for BodySlide and Outfit Studio, shipped as a
standalone DLL. It adds faster list searching, safer pose editing, practical
shortcuts and optional Blender-style navigation without replacing or redistributing
the original executables.

The distribution contains msimg32.dll, BSOSImprovements.ini, README.txt and
LICENSE.txt at its root.


WHAT IT DOES
------------

BodySlide
  A search box in the "Choose Groups" dialog. Filters the list as you type,
  with "Check visible" and "Clear all" buttons. Groups hidden by the filter
  stay checked, so you can search, check, search again, and hit OK with all
  of them selected.

  A search box in the "Batch Build" dialog. This one jumps to the first match
  as you type instead of filtering -- Enter cycles through the rest. "Toggle
  matching" flips the check mark for every outfit that matches. It jumps
  rather than hides because Batch Build reads what to build from the list
  itself, so hiding rows would change the result. Your checkboxes are left
  exactly as you set them unless you press that button.

  Z zeroes every slider in the panel at once. There is no undo for this in
  BodySlide -- if you keep hitting it by accident, set ZeroSliders=Alt+Z.

Outfit Studio
  Loading a project selects the reference shape (green, bold) instead of the
  first mesh in the list.

  B          select the reference shape
  Shift+E    Export Slider Data > Export OBJ    (needs a slider in Edit mode)
  Shift+I    Import Slider Data > Import OBJ    (needs a slider in Edit mode)
  F          Blender-style brush resize
  Shift+F    Blender-style brush strength
  K          Transform (moved off F)
  Z          zero every slider in the panel

  F: press it, move the mouse sideways to grow or shrink the circle, left
  click to confirm. Esc or right click cancels. The circle stays put while
  you drag. Needs an active brush (keys 1-9).

  Shift+F: the same drag for strength. A small native-style strength bar
  appears above the brush cursor while dragging and disappears when the drag
  is confirmed or cancelled.

  How many steps strength has is a guess in [Tuning] BrushStrengthSteps,
  default 100, because that number is not written in any file the mod can
  read. Too high is the harmful direction: steps past the end are ignored by
  the program but still counted, so cancelling would leave the brush weaker
  than it started. Turn the log on and press Shift+F once -- the mod dumps
  the status bar panels and one of them carries the strength.

  Toolbar tooltips now say the shortcut in parentheses -- "Shows a transform
  tool. (K)". The key is read from the matching menu entry, so it follows
  translations and stays right after a [Remap]. Buttons whose key lives only
  inside Outfit Studio's code -- the brush digits -- are labelled in the
  shipped INI: 1 Select, 2 Mask, 3 Inflate, 4 Deflate, 5 Move, 6 Smooth.
  You can change or remove those labels under [TooltipShortcuts].

  A search box in the "Symmetrize Vertices" dialog (and in its twin, "Mask
  Symmetric Vertices"), which lists one row per slider and one per bone. The
  filter only hides rows: a hidden row keeps its check mark, and clearing the
  box brings everything back.

  Shape > Masks adds four one-vertex-wide mask tools. Anatomical Seams finds
  the neck, wrist and ankle openings of an upright humanoid mesh while
  stopping at connected internal seams. Geometric Seams finds coincident
  split boundaries, UV Seams narrows that to UV splits, and Non-Manifold
  Borders finds open or over-connected edges. Hovering each item explains it.

  Mirror bone pose. Enable "Mirror pose" beside "Show Pose", then move a
  left/right bone in pose mode and its counterpart follows, same amount, sign
  flipped on the axes that mirror. It happens when you release the slider, not
  while dragging. Bones with no side -- Root,
  Pelvis, NPC Spine -- are left alone. Which axes flip is set in [MirrorPose],
  because it depends on how your skeleton orients each bone; the defaults are
  the Skyrim convention.

  Reorder sliders by dragging a row. Press on the BACKGROUND of the row --
  the pencil, the checkbox and the bar keep working as they do. Esc cancels.
  Reordering marks the project as modified. The new order is written into the
  matching SliderSet inside the .osp when you save the project, so Outfit
  Studio's normal save prompt and unsaved-change asterisk both work.

  Blender-style camera, OFF by default:

    Outfit Studio          Blender
    middle ....... pan     middle ....... orbit
    right ........ rotate  Shift+middle . pan
    Shift+middle . zoom    Ctrl+middle .. zoom
    wheel ........ zoom    wheel ........ zoom

  The wheel already matches; the rest is a three-way permutation and that is
  what this does. The right button keeps rotating.

  The modifier is swapped on the KEYBOARD, not in the message: Outfit Studio
  reads the physical key to choose pan or zoom, so rewriting the message does
  nothing. During a drag the mod releases the Shift you hold, or presses one
  you do not, and always undoes what it pressed.

  A "Blender camera keymap" entry sits at the end of the View menu whether it is on
  or off, so you can flip it without restarting.


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

  MO2 / Vortex      Do NOT install this archive as a normal standalone mod.
                    Open the installed BodySlide mod/folder and copy the
                    archive contents directly into the directory containing
                    the BodySlide and OutfitStudio executables. A typical MO2
                    destination is:
                    mods\BodySlide and Outfit Studio\CalienteTools\BodySlide\

  Manual            Extract the archive directly into the folder containing
                    the BodySlide and OutfitStudio executables. Do not create
                    another CalienteTools folder inside it.

Launching BodySlide through MO2 keeps working normally.

Requires a 64-bit executable. On 5.6 and 5.7 that is the one with the x64
suffix; the 32-bit build still starts, just without the improvements. 5.8.0
dropped the suffix and the plain name is the 64-bit build.

Verified against 5.8.2. Most features use standard Windows messages and turn
themselves off if their target UI cannot be found. The Shape > Masks tools
access the live mesh directly and are deliberately enabled only for the exact
verified Outfit Studio 5.8.2 binary; they stay unavailable on another build.

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

Two other sections use those same names. [TooltipShortcuts] only labels a
tooltip without binding anything, which is what the brush digits need:

  [TooltipShortcuts]
  btnInflateBrush=3

[MirrorPose] decides which axes flip when a pose is mirrored:

  [MirrorPose]
  NegateRotationY=1
  NegateOffsetX=1


COMPATIBILITY
-------------

Verified against 5.8.2, developed originally against 5.6.3. Most features use
standard Windows messages, so other versions should work and anything that
cannot be found disables itself. The direct mesh access used by Shape > Masks
is hash-locked to the verified Outfit Studio 5.8.2 executable for safety.

Dark mode is followed. 5.8.2 added a light/dark setting, and everything this
mod draws itself reads AppearanceMode from BodySlide's own Config.xml, so it
matches what you picked in Settings -- including "System", which tracks the
Windows theme.

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
