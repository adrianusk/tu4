TU4
===

> Prepare yourself for a grand adventure: Ultima IV, sixteen times
> larger than Ultima III, is a milestone in computer gaming.  Lord
> British has produced a game to challenge, not only your physical
> and mental skills, but the true fabric of your character.  The evil
> triad of Mondain, Minax, and the hellspawn Exodus, have been
> vanquished and peace reigns throughout the land of Britannia.  Evil
> yet abounds, but in isolated pockets and in the hearts of men.  A
> new age awaits the coming of one who can conquer evil on all
> frontiers through the mastery of both magic and the use of force.
> Daemons, dragons and long-dead wizards still plague the countryside
> and must be destroyed.  The seeker on the path of the avatar will
> faces hostile groups composed of mixed enemy types and will survive
> such encounters only by strategic use of weapons and terrain.
> Earthly victories over seemingly impossible odds lead to the final
> conflict, where the ultimate challenge -- the self -- awaits...
>   -- Back cover of Ultima IV box

TU4 is a **text-mode adaptation of [xu4](https://xu4.sourceforge.net/)**,
the remake of the computer game Ultima IV.  Where xu4 renders Ultima IV
with pixel-based DOS EGA/VGA (or upgraded) graphics, TU4 renders the
entire game in an **80×50 character display** using the **16-color EGA
palette** and the **CP437 character set** — the look of a DOS text-mode
console.  The goal is the same faithful recreation of the classic game,
just drawn with characters and color attributes instead of pixels.

TU4 follows xu4 as faithfully as possible: game logic, coordinate
systems, map handling, and save files are kept identical to xu4.  Only
the display layer is replaced — pixel rendering becomes character +
attribute output.  As with xu4, TU4 aims to maintain compatibility with
the original Ultima IV savegame files.

TU4 is developed and built on Linux.


Status
------

The game is playable in text mode.  Implemented so far:

 - Text-mode display engine: 80×50 character buffer, 16 EGA colors,
   CP437 glyphs rendered through an SDL2 backend (640×400 window,
   nearest-neighbor scaling).
 - Full title/intro sequence (Lord British signature, ORIGIN/PRESENT,
   ULTIMA IV scatter reveal, "Quest of the Avatar" subtitle, and the
   map expand-from-center animation), plus the intro map with animated
   creatures and scrolling water.
 - Main menu (Return to view, Journey Onward, Initiate New Game,
   Configure, About) and character creation (gypsy questions).
 - World map, town/city/castle, and combat rendering with 4×4-character
   tiles, tile animations (scroll, frame, char/color alternation),
   NPC/creature animation, and optional tile transparency.
 - Peer "gem" view for world, city, and dungeon maps.
 - First-person 3D dungeon view with per-distance art, dungeon fields
   (poison/energy/fire/sleep), traps, and object rendering.
 - Message/text area with word wrap, stats (Ztats) panels, wind /
   dungeon-direction indicator, moon phases, and combat visual effects
   (projectiles, hit flash, screen shake, focus indicator).
 - Configure menu with a selectable **Text Style** (graphics theme),
   applied live without restarting.
 - Mouse click-to-move support in the map viewport.

TU4 ships two text-mode themes: **U5-EGA** (the default) and **EGA**.

Some thoughts for possible improvements (inherited from xu4):
 - Ultima 5 style aiming in combat (i.e. allow angle shots)
 - More sound effects
 - Allow the map view to display more of the world
 - Formal modding system to extend the world or create entirely new adventures.


Graphics assets (ASP format)
----------------------------

TU4's text-mode themes use **`.ASP` files** in place of xu4's PNG/image
assets.  An ASP file is a DOS **BSAVE**-format blob: a 7-byte header
followed by a payload of **char+attr pairs** — one byte for the CP437
character and one byte for the EGA color attribute per cell.  Tile and
charset files are indexed by the Ultima IV **save ID** (0–255), the same
way xu4 indexes its image assets.

The theme directories under `graphics/` (e.g. `graphics/U5-EGA/`,
`graphics/EGA/`) contain the converted assets — MYSHAPES.ASP (tiles),
CHARSET.ASP / GEM.ASP (glyphs), TITLE.ASP, dungeon art, etc.  Conversion
scripts (PNG/SHAPES → ASP) live in `graphics/` and `graphics/test/`.


Compiling
---------

TU4 is built on Linux.  Build from the `src/` directory:

    cd src
    make

The build configuration in `make.config` is:

    UI=sdl2      (SDL2 + SDL2_mixer backend)
    GPU=none     (no GPU rendering — text mode is character-based)
    CONF=xml

> **_NOTE_:** The build requires SDL2 and SDL2_mixer development
> libraries.  (SDL3 backend files exist, but the default build uses
> SDL2 because SDL3_mixer is not available on the development system.)

If the required libraries & headers are present, `make` will create the
executable `src/tu4`.

> **_IMPORTANT — build gotcha:_** Always build from `src/`.  The
> top-level `Makefile` only does `cp src/tu4 .`; it does **not**
> recompile.  After changing any source, run:
>
>     cd src && make && cp tu4 ..
>
> If stale object files are suspected, run `cd src && make clean && make`.

For more detailed build instructions see [doc/build.md](doc/build.md).


Running
-------

The actual data files from Ultima 4 are loaded at runtime, which means
that a copy of Ultima IV for DOS must be present at runtime.
Fortunately, Ultima IV is available as closed-source freeware from
https://www.gog.com/game/ultima_4.

If you have the optional u4upgrad.zip, place it in the same directory as the
tu4 executable.  TU4 will read the Ultima IV data files straight out of the
zipfile.

TU4 searches for the zipfiles, or the unpacked contents of the
zipfiles in the following places:
 - The current directory when tu4 is run
 - A subdirectory named `ultima4` of the current directory
 - On UNIX systems: `/usr/share/tu4` & `/usr/local/share/tu4`
 - On Linux `$HOME/.local/share/tu4` may also be used

The zipfile doesn't need to be unpacked, but if it is, TU4 can handle
uppercase or lowercase filenames even on case-sensitive filesystems,
so it doesn't matter whether the files are named AVATAR.EXE or
avater.exe or even Avatar.exe.

At the title screen, a configuration menu can be accessed by pressing
'c'.  Here, the screen scale, **Text Style** (graphics theme), volume
and other settings can be modified.  (Pixel-only settings from xu4 —
display filter, gamma, shadow size/opacity — have been removed since
they do not apply to 4×4-character tiles; tile transparency is a simple
on/off toggle.)  These settings are stored in
`$HOME/.config/tu4/tu4rc` on Linux and `%APPDATA%\tu4\tu4rc` on Windows.

The saved game files are stored in the settings directory.

TU4 also accepts the following command line options:

    -f, --fullscreen        Run in fullscreen mode.
    -h, --help              Print this message and quit.
    -i, --skip-intro        Skip the intro. and load the last saved game.
    -m, --module <file>     Specify game module (default is Ultima-IV).
    -p, --profile <string>  Use another set of settings and save files.
    -q, --quiet             Disable audio.
    -s, --scale <int>       Specify display scaling factor (1-5).
    -v, --verbose           Enable verbose console output.

> **_NOTE_:** The `--filter` option from xu4 is not present in TU4 —
> pixel filters do not apply to text-mode rendering.

### Profiles

Profiles are stored in the `profiles` sub-directory.
Use quotation marks around profile names that include spaces.
The active profile name is shown on the introduction map view off the main menu.


Desktop integration (Linux)
----------------------------

TU4 sets an application/window icon at runtime (via SDL, from
`graphics/tu4.bmp`) so it shows up in the taskbar/dock/alt-tab.  A
`.desktop` launcher entry is provided in `packaging/tu4.desktop`; when
installed to `~/.local/share/applications/` (with the icon copied into
`~/.local/share/icons/hicolor/*/apps/tu4.png`) TU4 appears in the
application menu.


Ultima 4 Documentation
----------------------

Included with Ultima 4 for DOS, as downloaded from one of the above
sites, are electronic copies of the printed documentation from the
original Ultima IV box.  HISTORY.TXT contains the "The History of
Britannia", a general introduction to the world of Ultima IV.
WISDOM.TXT contains "The Book of Mystic Wisdom", which explains the
system of magic and provides descriptions of the spells and reagents.

PDF versions of these books are included in the official download in
the EXTRA folder of the zip file.
As an added bonus this folder also includes a pdf of the offical 
Ultima IV cluebook.

An image of the cloth map from the original Ultima IV box is also
included in the EXTRA folder.


Debug Mode (cheats)
-------------------

TU4 has a very useful debug mode (you can also think of it as a cheat mode).
To enable it:
- press 'c' in the main TU4 menu
- make sure that
  1) Game Enhancements = On
  2) Enhanced Gameplay Options -> Debug Mode (Cheats) = On

Cheat list:
* ctrl-c (cheat menu; press one of the following keys to use a cheat)

        1-8   gate (teleports you to a moongate location)
        F1-F8 virtue +10
        a     advance moons (advances the left moon [Trammel] to its next phase)
        c     collision (lets you walk across water, through mountains, etc.)
        e     equipment (gives the party Armour and Weapons)
        f     full stats (gives all party members 50 str, dex & intel and level 8)
        g     goto (enter a location and TU4 teleports you there)
        h     help (displays list of available cheats)
        i     items (gives the party Items and Equipment)
        j     joined by companions (if eligible)
        k     show karma (shows your virtues)
        l     location (displays current map and coordinates)
        m     mixtures (gives the party 99 mixtures of all spells)
        o     opacity (lets you see through opaque tiles)
        p     peer (switches between normal and gem view)
        r     reagents (gives the party 99 of all reagents)
        s     summon (enter a monster name, and TU4 creates it somewhere nearby)
        t     transports (press b/h/s + arrow key, and TU4 creates a balloon/horse/ship)
        v     full virtues (makes you a full avatar)
        w     change wind (changes or locks the wind direction)
        x     exit map (teleports the party to where it entered the current map)
        y     y-up (like the Y-up spell, but free)
        z     z-down (like the Z-down spell, but free)

* ctrl-d (destroy monster/object; doesn't work on energy fields)
* ctrl-h (teleport to Lord British's throne room)
* ctrl-v (switch between 3-d and 2-d view; dungeons only)
* F1-F8 (teleport to a dungeon entrance; surface only)
* F9-F11 (teleport to the altar room of truth/love/courage; surface only)
* F12 (display torch duration)
* Escape (end combat)

Note:
Except for the Escape key, none of the cheats work during combat.

Also: Alt-X quits the game immediately from anywhere (title animation,
map animation, menu, or about screen).


Credits
-------

TU4 is a text-mode adaptation of xu4.

    Copyright (c) 2026, ASCII Dragon (TU4 text-mode port)
    Copyright (c) 2002-2022, xu4 Team
    Copyright (c) 1987, Lord British

See http://xu4.sourceforge.net/links.html for some other interesting
Ultima IV related links.


[Allegro]: https://liballeg.org/
