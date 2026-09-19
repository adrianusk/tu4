Peer Gem Tile Mapping (xu4-1.0)
================================

How the Gem View Works
----------------------

In `screenGemUpdate()` (src/screen.cpp), the gem view uses a layout with
a 32×32 tile viewport (tileshape 4×4 pixels each). For each tile position:

1. Gets the tile via `screenViewportTile()`:
   - World map: centers viewport on the player (BORDER_WRAP).
   - City/town/castle: map is 32×32 = same as viewport, so the entire
     map is shown at once (centers on map center, not the player).
2. Converts the module tile ID to `ultimaId` via `usaveIds->ultimaId(tile)`.
3. If `ultimaId < 128` → draws from `gem.png` (128 simplified tile graphics).
4. If `ultimaId >= 128` → draws BLACK (invisible).

In VIEW_GEM mode without enhancements (`peerShowsObjects = false`), only
the ground terrain tile and the avatar are shown — no creatures, NPCs,
or objects. With `peerShowsObjects = true`, objects/NPCs are also visible.


Tile ID Table (tilemap-base.xml order)
--------------------------------------

The order in tilemap-base.xml directly maps to Ultima IV save IDs (0-based).
Multi-frame tiles occupy consecutive IDs.

```
ID    Tile Name             Frames  Notes
----  --------------------  ------  ---------------------------
  0   sea                   1       Deep ocean
  1   water                 1       Shallow water (rivers, lakes)
  2   shallows              1       Coastal shallows
  3   swamp                 1
  4   grass                 1       Default/padding tile
  5   brush                 1
  6   forest                1
  7   hills                 1
  8   mountains             1
  9   dungeon               1       Dungeon entrance on world map
 10   city                  1       City icon on world map
 11   castle                1       Castle icon on world map
 12   town                  1       Town icon on world map
 13   lcb_west              1       Lord British's Castle (west)
 14   lcb_entrance          1       Lord British's Castle (entrance)
 15   lcb_east              1       Lord British's Castle (east)
 16   ship (west)           \
 17   ship (north)           } 4 frames (directional)
 18   ship (east)           /
 19   ship (south)          /
 20   horse (frame 1)       \
 21   horse (frame 2)        } 2 frames
 22   dungeon_floor         1       Interior floor in dungeons
 23   bridge                1
 24   balloon               1
 25   bridge_n              1       Bridge (north segment)
 26   bridge_s              1       Bridge (south segment)
 27   up_ladder             1
 28   down_ladder           1
 29   ruins                 1
 30   shrine                1
 31   avatar                1
 32   mage (frame 1)        \
 33   mage (frame 2)         } Party member class tiles
 34   bard (frame 1)        \
 35   bard (frame 2)         }
 36   fighter (frame 1)     \
 37   fighter (frame 2)      }
 38   druid (frame 1)       \
 39   druid (frame 2)        }
 40   tinker (frame 1)      \
 41   tinker (frame 2)       }
 42   paladin (frame 1)     \
 43   paladin (frame 2)      }
 44   ranger (frame 1)      \
 45   ranger (frame 2)       }
 46   shepherd (frame 1)    \
 47   shepherd (frame 2)     }
 48   column                1       Pillar/column
 49   waterside_sw          1       Water edge (southwest)
 50   waterside_se          1       Water edge (southeast)
 51   waterside_nw          1       Water edge (northwest)
 52   waterside_ne          1       Water edge (northeast)
 53   shipmast              1
 54   shipwheel             1
 55   rocks                 1
 56   corpse                1
 57   stone_wall            1
 58   locked_door           1
 59   door                  1
 60   chest                 1
 61   ankh                  1
 62   brick_floor           1
 63   wood_floor            1
 64   moongate_opening 1    \
 65   moongate_opening 2     } 3 frames (opening animation)
 66   moongate_opening 3    /
 67   moongate              1       Fully open moongate
 68   poison_field          1
 69   energy_field          1
 70   fire_field            1
 71   sleep_field           1
 72   solid                 1       Impassable solid block
 73   secret_door           1
 74   altar                 1
 75   campfire              1
 76   lava                  1
 77   miss_flash            1       Combat miss effect
 78   magic_flash           1       Magic hit effect
 79   hit_flash             1       Physical hit effect
 80   guard (frame 1)       \
 81   guard (frame 2)        } NPC tiles (city only)
 82   villager (frame 1)    \
 83   villager (frame 2)     }
 84   bard_singing (frame 1)\
 85   bard_singing (frame 2) }
 86   jester (frame 1)      \
 87   jester (frame 2)       }
 88   beggar (frame 1)      \
 89   beggar (frame 2)       }
 90   child (frame 1)       \
 91   child (frame 2)        }
 92   bull (frame 1)        \
 93   bull (frame 2)         }
 94   lord_british (frame 1)\
 95   lord_british (frame 2) }
 96   A                     \
 97   B                      \
 98   C                       \
 99   D                        \
100   E                         \
101   F                          \
102   G                           } Letter tiles (signs)
103   H                          /
104   I                         /
105   J                        /
106   K                       /
107   L                      /
108   M                     /
109   N                     \
110   O                      \
111   P                       \
112   Q                        \
113   R                         \
114   S                          \
115   T                           }
116   U                          /
117   V                         /
118   W                        /
119   X                       /
120   Y                      /
121   Z                     /
122   space                 1
123   space_r               1
124   space_l               1
125   window                1
126   black                 1
127   brick_wall            1
--- (128 and above: drawn as BLACK in gem view) ---
128+  pirate_ship           4       \
132+  nixie                 2        \
134+  giant_squid           2         \
136+  sea_serpent           2          } Water monsters
138+  sea_horse             2         /
140+  whirlpool             2        /
142+  twister               2       /
144+  rat                   4       \
148+  bat                   4        \
152+  spider                4         \
156+  ghost                 4          \
160+  slime                 4           \
164+  troll                 4            \
168+  gremlin               4             \
172+  mimic                 4              \
176+  reaper                4               } Land monsters
180+  insect_swarm          4              /  (all invisible
184+  gazer                 4             /    in gem view)
188+  phantom               4            /
192+  orc                   4           /
196+  skeleton              4          /
200+  rogue                 4         /
204+  python                4        /
208+  ettin                 4       /
212+  headless              4      /
216+  cyclops               4     /
220+  wisp                  4    /
224+  evil_mage             4   /
228+  liche                 4  /
232+  lava_lizard           4 /
236+  zorn                  4/
240+  daemon                4
244+  hydra                 4
248+  dragon                4
252+  balron                4
```


World Map — What You Actually See in the Gem
---------------------------------------------

On the world map, the gem shows a 32×32 area centered on the player.
The tiles that actually appear on the world map terrain are:

- **Terrain:** sea(0), water(1), shallows(2), swamp(3), grass(4),
  brush(5), forest(6), hills(7), mountains(8)
- **Locations:** dungeon(9), city(10), castle(11), town(12),
  lcb_west(13), lcb_entrance(14), lcb_east(15)
- **Transports:** ship(16–19), horse(20–21), balloon(24)
- **Structures:** bridge(23), bridge_n(25), bridge_s(26)
- **Special:** ruins(29), shrine(30)
- **Player:** avatar(31)

Monsters on the world map (pirates, sea serpents, etc.) are >= 128 and
rendered as black. With `peerShowsObjects = true`, they would still be
black since their IDs are above 128.


City/Town/Castle — What You Actually See in the Gem
----------------------------------------------------

City maps are 32×32, matching the gem viewport exactly, so the ENTIRE
city is visible at once. The gem shows the complete layout.

**Common city tiles visible in the gem:**

- **Floors:** brick_floor(62), wood_floor(63)
- **Walls:** stone_wall(57), brick_wall(127)
- **Terrain:** grass(4), water(1), swamp(3), brush(5), forest(6)
- **Structures:** column(48), rocks(55)
- **Doors:** locked_door(58), door(59)
- **Objects:** chest(60), ankh(61), campfire(75)
- **Fields:** poison_field(68), energy_field(69), fire_field(70),
  sleep_field(71)
- **Special:** window(125), secret_door(73), altar(74)
- **Signs:** letters A–Z(96–121), space(122–124)
- **Water edges:** waterside_sw/se/nw/ne(49–52)
- **Player:** avatar(31), or ship(16–19)/horse(20–21)/balloon(24)

**Important: Map Data vs Object Layer**

City .ULT files have two layers:
1. **Map data** (32×32 tile grid) — terrain, walls, floors, doors,
   chests, ankhs, campfires, fields, signs, etc. These are baked into
   the map file.
2. **Object layer** (persons/NPCs) — guards, villagers, jesters,
   beggars, children, bulls, lord_british, etc. These are loaded
   separately from the person data block in the .ULT file.

In VIEW_GEM mode, `getTilesAt()` calls `map->getTileFromData(coords)`
which returns directly from the map data grid. This means:

- **Map data tiles are ALWAYS visible in the gem** — chests(60),
  doors(59), locked_doors(58), ankhs(61), campfires(75), fields(68–71),
  signs/letters(96–124), stone_wall(57), brick_wall(127), etc. These
  are NOT affected by the `peerShowsObjects` setting.

- **Object layer (NPCs) are NOT visible by default.** By default
  (`peerShowsObjects = false`, matching the original DOS Ultima IV),
  NPCs are hidden. The code skips the object layer entirely. You see
  the city layout (walls, floors, doors, chests, signs) but NO
  characters — no guards, no villagers, no jester, no beggar, no
  child, no bull, no lord_british. Just empty rooms and corridors
  with their furnishings.

Only with the xu4 enhancement option "Gem View Shows Objects" enabled
(`peerShowsObjects = true`) would NPC tiles become visible. Their IDs
(80–95) are < 128 so gem.png does contain graphics for them, but this
is a non-default enhancement — the original game never shows them.

Note: Chests dropped by defeated monsters in combat are added as
annotations/objects (not map data), so those would NOT be visible in
the default gem view. Only pre-placed chests in the .ULT map data are
always visible.


Key Differences: World Map vs City Gem View
--------------------------------------------

| Aspect          | World Map                    | City                        |
|-----------------|------------------------------|-----------------------------|
| Viewport        | 32×32 centered on player     | 32×32 = entire map visible  |
| Border          | BORDER_WRAP (wraps around)   | BORDER_EXIT (grass padding) |
| Visible terrain | Overworld tiles (0–15, 23–30)| Interior tiles (57–63, 127) |
| Monsters        | Black (≥128)                 | N/A (no monsters in cities) |
| NPCs            | N/A                          | Hidden by default           |
| Player          | Always visible               | Always visible              |


Source Code Reference (xu4-1.0)
-------------------------------

- `screenGemUpdate()` — src/screen.cpp:1504
- `screenShowGemTile()` — src/screen.cpp:1458
- `screenViewportTile()` — src/screen.cpp:483
- `Location::getTilesAt()` — src/location.cpp (VIEW_GEM filtering)
- `UltimaSaveIds::ultimaId()` — src/savegame.cpp:494
- Gem layout definition — conf/graphics.xml:5742
- Tile ID mapping — conf/tilemap-base.xml
- Gem tile image — gem.png (128 tiles, 4×4 pixels each)
