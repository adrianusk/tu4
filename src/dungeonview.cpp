/*
 * dungeonview.cpp
 */

#include <assert.h>
#include <string.h>
#include "config.h"
#include "context.h"
#include "debug.h"
#include "dungeon.h"
#include "dungeonview.h"
#include "error.h"
#include "imagemgr.h"
#include "savegame.h"
#include "settings.h"
#include "screen.h"
#include "tileanim.h"
#include "tileset.h"
#include "u4.h"
#include "utils.h"
#include "xu4.h"


DungeonView::DungeonView(int x, int y, int columns, int rows) : TileView(x, y, rows, columns)
, screen3dDungeonViewEnabled(true)
{
    spotTrapRange = -1;

    black  = tileset->getByName(Tile::sym.black)->getId();
    avatar = tileset->getByName(Tile::sym.avatar)->getId();

    corridor      = tileset->getByName(Tile::sym.dungeonFloor)->getId();
    up_ladder     = tileset->getByName(SYM_UP_LADDER)->getId();
    down_ladder   = tileset->getByName(SYM_DOWN_LADDER)->getId();
    updown_ladder = tileset->getByName(SYM_UP_DOWN_LADDER)->getId();

    /* Initialize scaled dungeon art lookup */
    memset(dungNpcTileIndex, -1, sizeof(dungNpcTileIndex));
    dungObj0Data = NULL;

    /* Map creature names to base tile indices (4 frames each).
       Used by the DUNGNPC1 (distance 1+) path only -- NPCs are never
       drawn at distance 0, so DUNGOBJ0 holds no NPC art. */
    static const char* npcNames[] = {
        "rat", "bat", "spider", "ghost", "slime",
        "troll", "gremlin", "reaper", "insect_swarm", "gazer", "phantom"
    };
    for (int i = 0; i < 11; i++) {
        Symbol sym = xu4.config->intern(npcNames[i]);
        const Tile* t = tileset->getByName(sym);
        if (t) {
            TileId tid = t->getId();
            if (tid < 256)
                dungNpcTileIndex[tid] = i * 4;  /* base tile index in DUNGNPC1 */
        }
    }

    /* Map dungeon objects to single-frame tiles (base index 44-47).
       The DUNGOBJ0/DUNGOBJ1 paths subtract 44 to index their 0/1/2/3
       layout. */
    static const struct { const char* name; int tileIdx; } objTiles[] = {
        { "fountain",      44 },
        { "chest",         45 },
        { "magic_orb",     46 },
        { "dungeon_altar", 47 },
    };
    for (int i = 0; i < 4; i++) {
        Symbol sym = xu4.config->intern(objTiles[i].name);
        const Tile* t = tileset->getByName(sym);
        if (t) {
            TileId tid = t->getId();
            if (tid < 256)
                dungNpcTileIndex[tid] = objTiles[i].tileIdx;
        }
    }

    /* Load DUNGOBJ0.ASP data (objects only: 0=fountain,1=chest,2=orb,3=altar) */
    Symbol obj0Sym = xu4.config->intern("dung_obj0");
    ImageInfo* obj0Info = xu4.imageMgr->get(obj0Sym);
    if (obj0Info && obj0Info->image && obj0Info->image->getAspData())
        dungObj0Data = obj0Info->image->getAspData();

    /* Load DUNGNPC1.ASP data */
    dungNpc1Data = NULL;
    Symbol npc1Sym = xu4.config->intern("dung_npc1");
    ImageInfo* npc1Info = xu4.imageMgr->get(npc1Sym);
    if (npc1Info && npc1Info->image && npc1Info->image->getAspData())
        dungNpc1Data = npc1Info->image->getAspData();

    /* Load DUNGOBJ1.ASP data (objects only: 0=fountain,1=chest,2=orb;
       no altar 8x8 record yet -- altar falls through to 4x4 at dist 1+) */
    dungObj1Data = NULL;
    Symbol obj1Sym = xu4.config->intern("dung_obj1");
    ImageInfo* obj1Info = xu4.imageMgr->get(obj1Sym);
    if (obj1Info && obj1Info->image && obj1Info->image->getAspData())
        dungObj1Data = obj1Info->image->getAspData();

    cacheGraphicData();
}

/*
 * Sets coords relative to party and fills tiles from that location.
 */
static void dungeonGetTiles(Coords& coords, std::vector<MapTile>& tiles,
                            int fwd, int side) {
    coords = c->location->coords;

    switch (c->saveGame->orientation) {
    case DIR_WEST:
        coords.x -= fwd;
        coords.y -= side;
        break;

    case DIR_NORTH:
        coords.x += side;
        coords.y -= fwd;
        break;

    case DIR_EAST:
        coords.x += fwd;
        coords.y += side;
        break;

    case DIR_SOUTH:
        coords.x -= side;
        coords.y += fwd;
        break;

    case DIR_ADVANCE:
    case DIR_RETREAT:
    default:
        ASSERT(0, "Invalid dungeon orientation");
    }

    // Wrap the coordinates if necessary
    map_wrap(coords, c->location->map);

    bool focus;
    tiles.clear();
    c->location->getTilesAt(tiles, coords, focus);
}

void DungeonView::display(Context * c, TileView *view)
{
    static const int8_t wallSides[3] = { -1, 1, 0 };
    Dungeon* dungeon = dynamic_cast<Dungeon *>(c->location->map);
    vector<MapTile> tiles;
    Coords drawLoc;
    int x, y;

    /* 1st-person perspective */
    if (screen3dDungeonViewEnabled) {
        //Note: This shouldn't go above 4, unless we check opaque tiles each step of the way.
        const int farthest_non_wall_tile_visibility = 4;

        screenEraseMapArea();
        if (c->party->getTorchDuration() > 0) {
            vector<MapTile> distant_tiles;

            for (y = 3; y >= 0; y--) {
                DungeonGraphicType type;
                Direction dir = (Direction) c->saveGame->orientation;

                // Draw walls player can see.
                
                for (x = 0; x < 3; ++x) {
                    dungeonGetTiles(drawLoc, tiles, y, wallSides[x]);
                    type = tilesToGraphic(dungeon, tiles);
                    drawWall(graphicIndex(drawLoc, wallSides[x], y, dir, type));
                }
                

                //This only checks that the tile at y==3 is opaque
                if (y == 3 && !tiles.front().getTileType()->isOpaque())
                {
                    for (int y_obj = farthest_non_wall_tile_visibility; y_obj > y; y_obj--)
                    {
                    dungeonGetTiles(drawLoc, distant_tiles, y_obj, 0);
                    DungeonGraphicType distant_type =
                        tilesToGraphic(dungeon, distant_tiles);

                    if ((distant_type == DNGGRAPHIC_DNGTILE) ||
                        (distant_type == DNGGRAPHIC_BASETILE))
                        drawInDungeon(distant_tiles.front(), 0, y_obj, dir);
                    }
                }
                if ((type == DNGGRAPHIC_DNGTILE) ||
                    (type == DNGGRAPHIC_BASETILE))
                    drawInDungeon(tiles.front(), 0, y, dir);
            }
        }
    }

    /* 3rd-person perspective */
    else {
        for (y = 0; y < VIEWPORT_H; y++) {
            for (x = 0; x < VIEWPORT_W; x++) {
                dungeonGetTiles(drawLoc, tiles,
                                (VIEWPORT_H / 2) - y, x - (VIEWPORT_W / 2));

                /* Only show blackness if there is no light */
                if (c->party->getTorchDuration() <= 0)
                    view->drawTile(black, x, y);
                else if (x == VIEWPORT_W/2 && y == VIEWPORT_H/2)
                    view->drawTile(avatar, x, y);
                else
                    view->drawTile(tiles, x, y);
            }
        }
    }
}

void DungeonView::drawInDungeon(const MapTile& mt, int x_offset, int distance, Direction orientation) {
    /*
     * Text-mode adaptation of xu4's drawInDungeon.
     * Handles two cases:
     *   1) tiledInDungeon tiles (fields): fill the corridor opening with
     *      the GEM.ASP glyph, tiled to fill the area at that distance.
     *   2) Normal tiles (chests, orbs, monsters): draw 4x4 tile centered.
     */
    (void)x_offset;
    (void)orientation;

    if (distance >= 4)
        return;

    const Tile* tile = tileset->get(mt.id);
    if (!tile)
        return;

    /*
     * Tiled wall rendering (fields): fill corridor opening with the 4x4
     * tile from SHAPES.ASP repeated to fill the area at that distance.
     * Applies scroll animation if the tile has one.
     * Only visible up to distance 2.
     */
    if (tile->isTiledInDungeon()) {
        if (distance > 2)
            return;

        static const int fillSize[] = { 44, 28, 12 };
        int size = fillSize[distance];

        /* Get the 4x4 tile data from SHAPES.ASP */
        ImageInfo* tilesInfo = xu4.imageMgr->get(BKGD_SHAPES);
        if (!tilesInfo || !tilesInfo->image || !tilesInfo->image->getAspData())
            return;

        const uint8_t* shapes = tilesInfo->image->getAspData();
        const UltimaSaveIds* usaveIds = xu4.config->usaveIds();
        uint8_t uid = usaveIds->ultimaId(mt);
        const uint8_t* tileData = shapes + uid * 32; /* 4 rows x 4 cols x 2 bytes */

        /* Get scroll offset from tile animation (if any).
         * We must also advance the scroll counter here since the normal
         * tile draw path (TileAnimTransform::draw) is not called in 3D view. */
        int scrollOffset = 0;
        TileAnim* anim = tile->getAnim();
        if (anim && !anim->transforms.empty()) {
            TileAnimTransform* tf = anim->transforms[0];
            if (tf->animType == ATYPE_SCROLL) {
                int offset = screenState()->currentCycle / 2;
                if (tf->var.scroll.lastOffset != offset) {
                    tf->var.scroll.lastOffset = offset;
                    tf->var.scroll.current += 1;
                    if (tf->var.scroll.current >= 4)  // TILE_CHARS_H
                        tf->var.scroll.current = 0;
                }
                scrollOffset = tf->var.scroll.current;
            }
        }

        /* Center the fill area in the 44x44 viewport */
        int cx = BORDER_WIDTH + (44 / 2) - (size / 2);
        int cy = BORDER_HEIGHT + (44 / 2) - (size / 2);

        /* Fill the area by tiling the 4x4 tile with scroll offset */
        for (int row = 0; row < size; row++) {
            int dy = cy + row;
            if (dy < BORDER_HEIGHT || dy >= BORDER_HEIGHT + 44)
                continue;
            int trow = (row - scrollOffset + 4) % 4;
            for (int col = 0; col < size; col++) {
                int dx = cx + col;
                if (dx < BORDER_WIDTH || dx >= BORDER_WIDTH + 44)
                    continue;
                int tcol = col % 4;
                int bi = trow * (4 * 2) + tcol * 2;
                screenPutChar(dx, dy, tileData[bi], tileData[bi + 1]);
            }
        }
        return;
    }

    /*
     * Normal tile rendering (chests, orbs, monsters, etc.)
     * Draw the 4x4 tile from MYSHAPES.ASP centered at the appropriate depth.
     * At distance 0-1, objects use 16x16 scaled art from DUNGOBJ0.ASP if
     * available. NPCs are never drawn in the 3D view (combat triggers
     * before the player reaches distance 0), so DUNGOBJ0 holds only the
     * 3 objects (fountain/chest/orb). Monster art still comes from
     * DUNGNPC1 at the farther distances.
     *
     * For EGA text styles (textStyle ends with "EGA"):
     *   Distance 0: DUNGOBJ0 objects (16x16)
     *   Distance 1: DUNGNPC1 NPCs / DUNGOBJ1 objects (8x8)
     *   Distance 2: MYSHAPES (4x4)
     *   Distance 3+: not shown
     *
     * For non-EGA styles:
     *   Distance 0-1: DUNGOBJ0 objects (16x16)
     *   Distance 2: DUNGNPC1 NPCs / DUNGOBJ1 objects (8x8)
     *   Distance 3+: MYSHAPES (4x4) fallthrough
     */

    /* Determine if we're using an EGA-style text theme */
    const std::string& style = xu4.settings->textStyle;
    bool egaStyle = (style.size() >= 3 &&
                     style.compare(style.size() - 3, 3, "EGA") == 0);

    /*
     * Y offset per distance, adapted from xu4's EGA formula:
     *   y_offset = max(0, (nscale_ega[dist] - 1) * 4)  (pixels)
     * Converted to character units (÷4) for 44-char viewport.
     * Makes closer NPCs appear grounded (feet near bottom of corridor).
     */
    static const int npc_y_offset_ega[] = { 5, 2, 1, 0 };  /* dist 0-3 */
    static const int npc_y_offset_vga[] = { 5, 3, 1, 0 };  /* dist 0-3 */
    const int* npc_y_offset = egaStyle ? npc_y_offset_ega : npc_y_offset_vga;

    if (egaStyle) {
        /* EGA style: dist 0 = DUNGOBJ0 objects, dist 1 = DUNGNPC1 NPCs / DUNGOBJ1 objects, dist 2 = MYSHAPES, dist 3+ = nothing */
        if (distance >= 3)
            return;

        /* Distance 0: use 16x16 scaled object art from DUNGOBJ0.ASP */
        if (distance == 0 && dungObj0Data && mt.id < 256 &&
            dungNpcTileIndex[mt.id] >= 44) {
            int baseTile = dungNpcTileIndex[mt.id];
            /* DUNGOBJ0 holds the objects at indices 0/1/2/3 */
            int tileIdx = baseTile - 44;
            const uint8_t* tileData = dungObj0Data + tileIdx * 512;

            int scrollOffset = 0;
            if (baseTile == 44) {
                TileAnim* anim = tile->getAnim();
                if (anim && !anim->transforms.empty()) {
                    TileAnimTransform* tf = anim->transforms[0];
                    if (tf->animType == ATYPE_SCROLL) {
                        int offset = screenState()->currentCycle / 2;
                        if (tf->var.scroll.lastOffset != offset) {
                            tf->var.scroll.lastOffset = offset;
                            tf->var.scroll.current += 1;
                            if (tf->var.scroll.current >= 16)
                                tf->var.scroll.current = 0;
                        }
                        scrollOffset = tf->var.scroll.current;
                    }
                }
            }

            int cx = BORDER_WIDTH + (44 / 2) - (16 / 2);
            int cy = BORDER_HEIGHT + (44 / 2) + npc_y_offset[0];

            for (int row = 0; row < 16; row++) {
                int dy = cy + row;
                if (dy < BORDER_HEIGHT || dy >= BORDER_HEIGHT + 44)
                    continue;
                int srcRow = (row - scrollOffset + 16) % 16;
                for (int col = 0; col < 16; col++) {
                    int dx = cx + col;
                    if (dx < BORDER_WIDTH || dx >= BORDER_WIDTH + 44)
                        continue;
                    int idx = (srcRow * 16 + col) * 2;
                    uint8_t ch   = tileData[idx];
                    uint8_t attr = tileData[idx + 1];
                    if (ch == ' ')
                        continue;
                    screenPutChar(dx, dy, ch, attr);
                }
            }
            return;
        }

        /* Distance 1: 8x8 art -- NPCs from DUNGNPC1.ASP, objects from DUNGOBJ1.ASP */
        if (distance == 1 && mt.id < 256 && dungNpcTileIndex[mt.id] >= 0 &&
            ((dungNpcTileIndex[mt.id] < 44 && dungNpc1Data) ||
             /* DUNGOBJ1 currently holds only 3 objects (44-46); the altar
                (47) has no 8x8 record yet, so let it fall through to the
                4x4 MYSHAPES path below. */
             (dungNpcTileIndex[mt.id] >= 44 && dungNpcTileIndex[mt.id] <= 46 &&
              dungObj1Data))) {
            int baseTile = dungNpcTileIndex[mt.id];
            const uint8_t* tileData;
            if (baseTile < 44) {
                /* NPC: 4 animation frames in DUNGNPC1 */
                int tileIdx = baseTile + (mt.frame % 4);
                tileData = dungNpc1Data + tileIdx * 128;
            } else {
                /* Object: single frame in DUNGOBJ1 at index 0/1/2 */
                tileData = dungObj1Data + (baseTile - 44) * 128;
            }

            int scrollOffset = 0;
            if (baseTile == 44) {
                TileAnim* anim = tile->getAnim();
                if (anim && !anim->transforms.empty()) {
                    TileAnimTransform* tf = anim->transforms[0];
                    if (tf->animType == ATYPE_SCROLL) {
                        int offset = screenState()->currentCycle / 2;
                        if (tf->var.scroll.lastOffset != offset) {
                            tf->var.scroll.lastOffset = offset;
                            tf->var.scroll.current += 1;
                            if (tf->var.scroll.current >= 8)
                                tf->var.scroll.current = 0;
                        }
                        scrollOffset = tf->var.scroll.current;
                    }
                }
            }

            int cx = BORDER_WIDTH + (44 / 2) - (8 / 2);
            int cy = BORDER_HEIGHT + (44 / 2) + npc_y_offset[1];

            for (int row = 0; row < 8; row++) {
                int dy = cy + row;
                if (dy < BORDER_HEIGHT || dy >= BORDER_HEIGHT + 44)
                    continue;
                int srcRow = (row - scrollOffset + 8) % 8;
                for (int col = 0; col < 8; col++) {
                    int dx = cx + col;
                    if (dx < BORDER_WIDTH || dx >= BORDER_WIDTH + 44)
                        continue;
                    int idx = (srcRow * 8 + col) * 2;
                    uint8_t ch   = tileData[idx];
                    uint8_t attr = tileData[idx + 1];
                    if (ch == ' ')
                        continue;
                    screenPutChar(dx, dy, ch, attr);
                }
            }
            return;
        }

        /* Distance 2: fall through to MYSHAPES 4x4 rendering below */

    } else {
        /* Non-EGA style: dist 0-1 = DUNGOBJ0 objects, dist 2 = DUNGNPC1 NPCs / DUNGOBJ1 objects, dist 3+ = MYSHAPES fallthrough */

        /* Check for 16x16 scaled object art at distance 0-1 (DUNGOBJ0) */
        if (distance <= 1 && dungObj0Data && mt.id < 256 &&
            dungNpcTileIndex[mt.id] >= 44) {
            int baseTile = dungNpcTileIndex[mt.id];
            /* DUNGOBJ0 holds the objects at indices 0/1/2/3 */
            int tileIdx = baseTile - 44;
            const uint8_t* tileData = dungObj0Data + tileIdx * 512;

            int scrollOffset = 0;
            if (baseTile == 44) {
                TileAnim* anim = tile->getAnim();
                if (anim && !anim->transforms.empty()) {
                    TileAnimTransform* tf = anim->transforms[0];
                    if (tf->animType == ATYPE_SCROLL) {
                        int offset = screenState()->currentCycle / 2;
                        if (tf->var.scroll.lastOffset != offset) {
                            tf->var.scroll.lastOffset = offset;
                            tf->var.scroll.current += 1;
                            if (tf->var.scroll.current >= 16)
                                tf->var.scroll.current = 0;
                        }
                        scrollOffset = tf->var.scroll.current;
                    }
                }
            }

            int cx = BORDER_WIDTH + (44 / 2) - (16 / 2);
            int cy = BORDER_HEIGHT + (44 / 2) + npc_y_offset[distance];

            for (int row = 0; row < 16; row++) {
                int dy = cy + row;
                if (dy < BORDER_HEIGHT || dy >= BORDER_HEIGHT + 44)
                    continue;
                int srcRow = (row - scrollOffset + 16) % 16;
                for (int col = 0; col < 16; col++) {
                    int dx = cx + col;
                    if (dx < BORDER_WIDTH || dx >= BORDER_WIDTH + 44)
                        continue;
                    int idx = (srcRow * 16 + col) * 2;
                    uint8_t ch   = tileData[idx];
                    uint8_t attr = tileData[idx + 1];
                    if (ch == ' ')
                        continue;
                    screenPutChar(dx, dy, ch, attr);
                }
            }
            return;
        }

        /* Check for 8x8 scaled art at distance 2 (NPCs from DUNGNPC1, objects from DUNGOBJ1) */
        if (distance == 2 && mt.id < 256 && dungNpcTileIndex[mt.id] >= 0 &&
            ((dungNpcTileIndex[mt.id] < 44 && dungNpc1Data) ||
             /* DUNGOBJ1 currently holds only 3 objects (44-46); the altar
                (47) has no 8x8 record yet, so let it fall through to the
                4x4 MYSHAPES path below. */
             (dungNpcTileIndex[mt.id] >= 44 && dungNpcTileIndex[mt.id] <= 46 &&
              dungObj1Data))) {
            int baseTile = dungNpcTileIndex[mt.id];
            const uint8_t* tileData;
            if (baseTile < 44) {
                /* NPC: 4 animation frames in DUNGNPC1 */
                int tileIdx = baseTile + (mt.frame % 4);
                tileData = dungNpc1Data + tileIdx * 128;
            } else {
                /* Object: single frame in DUNGOBJ1 at index 0/1/2 */
                tileData = dungObj1Data + (baseTile - 44) * 128;
            }

            int scrollOffset = 0;
            if (baseTile == 44) {
                TileAnim* anim = tile->getAnim();
                if (anim && !anim->transforms.empty()) {
                    TileAnimTransform* tf = anim->transforms[0];
                    if (tf->animType == ATYPE_SCROLL) {
                        int offset = screenState()->currentCycle / 2;
                        if (tf->var.scroll.lastOffset != offset) {
                            tf->var.scroll.lastOffset = offset;
                            tf->var.scroll.current += 1;
                            if (tf->var.scroll.current >= 8)
                                tf->var.scroll.current = 0;
                        }
                        scrollOffset = tf->var.scroll.current;
                    }
                }
            }

            int cx = BORDER_WIDTH + (44 / 2) - (8 / 2);
            int cy = BORDER_HEIGHT + (44 / 2) + npc_y_offset[2];

            for (int row = 0; row < 8; row++) {
                int dy = cy + row;
                if (dy < BORDER_HEIGHT || dy >= BORDER_HEIGHT + 44)
                    continue;
                int srcRow = (row - scrollOffset + 8) % 8;
                for (int col = 0; col < 8; col++) {
                    int dx = cx + col;
                    if (dx < BORDER_WIDTH || dx >= BORDER_WIDTH + 44)
                        continue;
                    int idx = (srcRow * 8 + col) * 2;
                    uint8_t ch   = tileData[idx];
                    uint8_t attr = tileData[idx + 1];
                    if (ch == ' ')
                        continue;
                    screenPutChar(dx, dy, ch, attr);
                }
            }
            return;
        }
    }

    ImageInfo* tilesInfo = xu4.imageMgr->get(BKGD_SHAPES);
    if (!tilesInfo || !tilesInfo->image || !tilesInfo->image->getAspData())
        return;

    const uint8_t* shapes = tilesInfo->image->getAspData();
    const UltimaSaveIds* usaveIds = xu4.config->usaveIds();

    /*
     * Resolve the MYSHAPES save id, honoring the tile's image override
     * (e.g. the fountain uses image="tile_shallows", so it must render as
     * shallow water -- ultimaId(fountain) has no graphic of its own and
     * would land on the wrong slot). Many dungeon tiles use a
     * "tile_<name>" image override; map it to that base tile's ultimaId.
     * Falls back to the tile's own ultimaId when there is no override.
     */
    MapTile shapeTile = mt;
    if (tile && tile->imageName) {
        const char* imgName = xu4.config->symbolName(tile->imageName);
        if (imgName && strncmp(imgName, "tile_", 5) == 0) {
            const Tile* baseTile =
                tileset->getByName(xu4.config->intern(imgName + 5));
            if (baseTile)
                shapeTile = MapTile(baseTile->getId(), mt.frame);
        }
    }
    uint8_t uid = usaveIds->ultimaId(shapeTile);
    const uint8_t* tileData = shapes + uid * 32; /* 4x4 * 2 bytes */

    /*
     * Center position in the 44x44 viewport.
     * Y offset adapted from xu4's grounded positioning formula.
     */
    int cx = BORDER_WIDTH + (44 / 2) - (tileWidth / 2);
    int cy = BORDER_HEIGHT + (44 / 2) + npc_y_offset[distance];

    /*
     * Advance scroll animation inline (the 3D view does not go through
     * TileAnimTransform::draw()). Applies to scroll-animated tiles such
     * as the fountain when it reaches this 4x4 fallthrough (e.g. the
     * default U5-EGA theme draws it at distance 2 here).
     */
    int scrollOffset = 0;
    {
        TileAnim* anim = tile->getAnim();
        if (anim && !anim->transforms.empty()) {
            TileAnimTransform* tf = anim->transforms[0];
            if (tf->animType == ATYPE_SCROLL) {
                int offset = screenState()->currentCycle / 2;
                if (tf->var.scroll.lastOffset != offset) {
                    tf->var.scroll.lastOffset = offset;
                    tf->var.scroll.current += 1;
                    if (tf->var.scroll.current >= tileHeight)
                        tf->var.scroll.current = 0;
                }
                scrollOffset = tf->var.scroll.current;
            }
        }
    }

    /* Draw the 4x4 tile characters, skipping transparent cells */
    for (int row = 0; row < tileHeight; row++) {
        int dy = cy + row;
        if (dy < BORDER_HEIGHT || dy >= BORDER_HEIGHT + 44)
            continue;
        int srcRow = (row - scrollOffset + tileHeight) % tileHeight;
        for (int col = 0; col < tileWidth; col++) {
            int dx = cx + col;
            if (dx < BORDER_WIDTH || dx >= BORDER_WIDTH + 44)
                continue;
            int idx = (srcRow * tileWidth + col) * 2;
            uint8_t ch   = tileData[idx];
            uint8_t attr = tileData[idx + 1];
            /* Skip fully black cells (space with black attr = transparent) */
            if (ch == ' ')
                continue;
            screenPutChar(dx, dy, ch, attr);
        }
    }
}

/*
 * Begin trap detection for the current view.
 * One trap in range may be shown after a delay.
 */
void DungeonView::detectTraps() {
    spotTrapRange = xu4_random(4);
    if (spotTrapRange < 3)
        spotTrapTime = c->commandTimer + 200 + xu4_random(3000);
    else
        spotTrapRange = -1;
}

int DungeonView::graphicIndex(const Coords& loc, int xoffset, int distance,
                              Direction orientation, DungeonGraphicType type) {
    int index;
    assert(distance < 4);

    if (type == DNGGRAPHIC_LADDERUP && xoffset == 0)
        return 48 +
        (distance * 2) +
        (DIR_IN_MASK(orientation, MASK_DIR_SOUTH | MASK_DIR_NORTH) ? 1 : 0);

    if (type == DNGGRAPHIC_LADDERDOWN && xoffset == 0)
        return 56 +
        (distance * 2) +
        (DIR_IN_MASK(orientation, MASK_DIR_SOUTH | MASK_DIR_NORTH) ? 1 : 0);

    if (type == DNGGRAPHIC_LADDERUPDOWN && xoffset == 0)
        return 64 +
        (distance * 2) +
        (DIR_IN_MASK(orientation, MASK_DIR_SOUTH | MASK_DIR_NORTH) ? 1 : 0);

    if (type == DNGGRAPHIC_TRAP) {
#if 1
        if (xoffset == 0 && spotTrapRange == distance &&
             c->commandTimer >= spotTrapTime)
#else
        if (xoffset == 0)   // For Testing
#endif
        {
            index = static_cast<Dungeon *>(c->location->map)->subTokenAt(loc);
            if (index == TRAP_FALLING_ROCK)
                return 78 + distance;
            if (index == TRAP_PIT)
                return 81 + distance;
        }
        return -1;
    }

    /* FIXME */
    if (type != DNGGRAPHIC_WALL && type != DNGGRAPHIC_DOOR)
        return -1;

    index = 0;
    if (type == DNGGRAPHIC_DOOR)
        index += 24;

    index += (xoffset + 1) * 2;
    index += distance * 6;

    if (DIR_IN_MASK(orientation, MASK_DIR_SOUTH | MASK_DIR_NORTH))
        index++;

    return index;
}

DungeonGraphicType DungeonView::tilesToGraphic(const Dungeon* dungeon,
                                        const std::vector<MapTile> &tiles) {
    MapTile tile = tiles.front();

    /*
     * check if the dungeon tile has an annotation or object on top
     * (always displayed as a tile, unless a ladder)
     */
    if (tiles.size() > 1) {
        if (tile.id == up_ladder)
            return DNGGRAPHIC_LADDERUP;
        else if (tile.id == down_ladder)
            return DNGGRAPHIC_LADDERDOWN;
        else if (tile.id == updown_ladder)
            return DNGGRAPHIC_LADDERUPDOWN;
        else if (tile.id == corridor)
            return DNGGRAPHIC_NONE;
        else {
            /* A stack (size > 1) is a creature/object/annotation ON TOP of a
               base dungeon tile. getTilesAt() pushes the base tile LAST, so
               tiles.front() is the topmost overlay (the creature) and
               tiles.back() is the base tile it stands on.

               If that base tile is OPAQUE architecture (wall/door/room/secret
               door), its face is nearer to the viewer than the creature and
               would completely cover it. Rather than draw the creature and
               then overwrite it with the door (same visual, wasted work), we
               just draw the nearer thing: return WALL/DOOR so drawWall()
               renders the opaque face, and the display loop then SKIPS the
               drawInDungeon() overlay for this cell (its guard only fires for
               DNGTILE/BASETILE). Net: a creature standing on a door two steps
               ahead is correctly hidden behind the door face.

               Also handle the rarer case where the FRONT tile is itself such
               architecture. Otherwise (open corridor/floor base) fall through
               to BASETILE so the creature/object is drawn. */
            DungeonToken ftoken = dungeon->tokenForTile(tile.id);
            switch (ftoken) {
            case DUNGEON_WALL:
            case DUNGEON_SECRET_DOOR:
                return DNGGRAPHIC_WALL;
            case DUNGEON_ROOM:
            case DUNGEON_DOOR:
                return DNGGRAPHIC_DOOR;
            default:
                break;
            }
            DungeonToken btoken = dungeon->tokenForTile(tiles.back().id);
            switch (btoken) {
            case DUNGEON_WALL:
            case DUNGEON_SECRET_DOOR:
                return DNGGRAPHIC_WALL;
            case DUNGEON_ROOM:
            case DUNGEON_DOOR:
                return DNGGRAPHIC_DOOR;
            default:
                return DNGGRAPHIC_BASETILE;
            }
        }
    }

    /*
     * if not an annotation or object, then the tile is a dungeon
     * token
     */
    DungeonToken token = dungeon->tokenForTile(tile.id);
    switch (token) {
    case DUNGEON_TRAP:
        return DNGGRAPHIC_TRAP;
    case DUNGEON_CORRIDOR:
        return DNGGRAPHIC_NONE;
    case DUNGEON_WALL:
    case DUNGEON_SECRET_DOOR:
        return DNGGRAPHIC_WALL;
    case DUNGEON_ROOM:
    case DUNGEON_DOOR:
        return DNGGRAPHIC_DOOR;
    case DUNGEON_LADDER_UP:
        return DNGGRAPHIC_LADDERUP;
    case DUNGEON_LADDER_DOWN:
        return DNGGRAPHIC_LADDERDOWN;
    case DUNGEON_LADDER_UPDOWN:
        return DNGGRAPHIC_LADDERUPDOWN;

    default:
        return DNGGRAPHIC_DNGTILE;
    }
}

#define GRAPHIC_COUNT   90

const struct {
    const char* imageName;
    uint8_t ega_x2, ega_y2;
    uint8_t vga_x2, vga_y2;
    uint8_t subimage2;
} dngGraphicInfo[GRAPHIC_COUNT] = {
    { "dung0_lft_ew", 0,0,0,0,0 },
    { "dung0_lft_ns", 0,0,0,0,0 },
    { "dung0_mid_ew", 0,0,0,0,0 },
    { "dung0_mid_ns", 0,0,0,0,0 },
    { "dung0_rgt_ew", 0,0,0,0,0 },
    { "dung0_rgt_ns", 0,0,0,0,0 },
        // 6
    { "dung1_lft_ew", 0, 8, 0, 8, 72 },         // + "dung1_xxx_ew"
    { "dung1_lft_ns", 0, 8, 0, 8, 73 },         // + "dung1_xxx_ns"
    { "dung1_mid_ew", 0,0,0,0,0 },
    { "dung1_mid_ns", 0,0,0,0,0 },
    { "dung1_rgt_ew", 36, 8, 36, 8, 72 },       // + "dung1_xxx_ew"
    { "dung1_rgt_ns", 36, 8, 36, 8, 73 },       // + "dung1_xxx_ns"
        // 12
    { "dung2_lft_ew", 0, 16, 0, 16, 74 },       // + "dung2_xxx_ew"
    { "dung2_lft_ns", 0, 16, 0, 16, 75 },       // + "dung2_xxx_ns"
    { "dung2_mid_ew", 0,0,0,0,0 },
    { "dung2_mid_ns", 0,0,0,0,0 },
    { "dung2_rgt_ew", 28, 16, 28, 16, 74 },     // + "dung2_xxx_ew"
    { "dung2_rgt_ns", 28, 16, 28, 16, 75 },     // + "dung2_xxx_ns"
        // 18
    { "dung3_lft_ew", 0, 20, 0, 20, 76 },       // + "dung3_xxx_ew"
    { "dung3_lft_ns", 0, 20, 0, 20, 77 },       // + "dung3_xxx_ns"
    { "dung3_mid_ew", 0,0,0,0,0 },
    { "dung3_mid_ns", 0,0,0,0,0 },
    { "dung3_rgt_ew", 24, 20, 24, 20, 76 },     // + "dung3_xxx_ew"
    { "dung3_rgt_ns", 24, 20, 24, 20, 77 },     // + "dung3_xxx_ns"
        // 24
    { "dung0_lft_ew_door", 0,0,0,0,0 },
    { "dung0_lft_ns_door", 0,0,0,0,0 },
    { "dung0_mid_ew_door", 0,0,0,0,0 },
    { "dung0_mid_ns_door", 0,0,0,0,0 },
    { "dung0_rgt_ew_door", 0,0,0,0,0 },
    { "dung0_rgt_ns_door", 0,0,0,0,0 },
        // 30
    { "dung1_lft_ew_door", 0, 8, 0, 8, 72 },        // + "dung1_xxx_ew"
    { "dung1_lft_ns_door", 0, 8, 0, 8, 73 },        // + "dung1_xxx_ns"
    { "dung1_mid_ew_door", 0,0,0,0,0 },
    { "dung1_mid_ns_door", 0,0,0,0,0 },
    { "dung1_rgt_ew_door", 36, 8, 36, 8, 72 },      // + "dung1_xxx_ew"
    { "dung1_rgt_ns_door", 36, 8, 36, 8, 73 },      // + "dung1_xxx_ns"
        // 36
    { "dung2_lft_ew_door", 0, 16, 0, 16, 74 },      // + "dung2_xxx_ew"
    { "dung2_lft_ns_door", 0, 16, 0, 16, 75 },      // + "dung2_xxx_ns"
    { "dung2_mid_ew_door", 0,0,0,0,0 },
    { "dung2_mid_ns_door", 0,0,0,0,0 },
    { "dung2_rgt_ew_door", 28, 16, 28, 16, 74 },    // + "dung2_xxx_ew"
    { "dung2_rgt_ns_door", 28, 16, 28, 16, 75 },    // + "dung2_xxx_ns"
        // 42
    { "dung3_lft_ew_door", 0, 20, 0, 20, 76 },      // + "dung3_xxx_ew"
    { "dung3_lft_ns_door", 0, 20, 0, 20, 77 },      // + "dung3_xxx_ns"
    { "dung3_mid_ew_door", 0,0,0,0,0 },
    { "dung3_mid_ns_door", 0,0,0,0,0 },
    { "dung3_rgt_ew_door", 24, 20, 24, 20, 76 },    // + "dung3_xxx_ew"
    { "dung3_rgt_ns_door", 24, 20, 24, 20, 77 },    // + "dung3_xxx_ns"
        // 48
    { "dung0_ladderup",      0,0,0,0,0 },
    { "dung0_ladderup_side", 0,0,0,0,0 },
    { "dung1_ladderup",      0,0,0,0,0 },
    { "dung1_ladderup_side", 0,0,0,0,0 },
    { "dung2_ladderup",      0,0,0,0,0 },
    { "dung2_ladderup_side", 0,0,0,0,0 },
    { "dung3_ladderup",      0,0,0,0,0 },
    { "dung3_ladderup_side", 0,0,0,0,0 },
        // 56
    { "dung0_ladderdown",      0,0,0,0,0 },
    { "dung0_ladderdown_side", 0,0,0,0,0 },
    { "dung1_ladderdown",      0,0,0,0,0 },
    { "dung1_ladderdown_side", 0,0,0,0,0 },
    { "dung2_ladderdown",      0,0,0,0,0 },
    { "dung2_ladderdown_side", 0,0,0,0,0 },
    { "dung3_ladderdown",      0,0,0,0,0 },
    { "dung3_ladderdown_side", 0,0,0,0,0 },
        // 64
    { "dung0_ladderupdown",      0,0,0,0,0 },
    { "dung0_ladderupdown_side", 0,0,0,0,0 },
    { "dung1_ladderupdown",      0,0,0,0,0 },
    { "dung1_ladderupdown_side", 0,0,0,0,0 },
    { "dung2_ladderupdown",      0,0,0,0,0 },
    { "dung2_ladderupdown_side", 0,0,0,0,0 },
    { "dung3_ladderupdown",      0,0,0,0,0 },
    { "dung3_ladderupdown_side", 0,0,0,0,0 },
        // 72
    { "dung1_xxx_ew", 0,0,0,0,0 },
    { "dung1_xxx_ns", 0,0,0,0,0 },
    { "dung2_xxx_ew", 0,0,0,0,0 },
    { "dung2_xxx_ns", 0,0,0,0,0 },
    { "dung3_xxx_ew", 0,0,0,0,0 },
    { "dung3_xxx_ns", 0,0,0,0,0 },
        // 78
    { "dung0_hole",   0,0,0,0,0 },
    { "dung1_hole",   0,0,0,0,0 },
    { "dung2_hole",   0,0,0,0,0 },
    { "dung0_pit",    0,0,0,0,0 },
    { "dung1_pit",    0,0,0,0,0 },
    { "dung2_pit",    0,0,0,0,0 },
        // 84 - right-side fill variants (fall back to xxx if not defined)
    { "dung1_rgt_fill_ew", 0,0,0,0,0 },
    { "dung1_rgt_fill_ns", 0,0,0,0,0 },
    { "dung2_rgt_fill_ew", 0,0,0,0,0 },
    { "dung2_rgt_fill_ns", 0,0,0,0,0 },
    { "dung3_rgt_fill_ew", 0,0,0,0,0 },
    { "dung3_rgt_fill_ns", 0,0,0,0,0 }
};

// Right-side fill graphics (indices 84-89 in dngGraphicInfo).
static DungeonView::GraphicData rgtFillGraphic[6];

/*
 * Cache wall graphic pointers at setup to avoid lookup by name and image
 * loading during drawWall().
 */
void DungeonView::cacheGraphicData() {
    Symbol name;
    int i;

    for (i = 0; i < 84; ++i) {
        name = xu4.config->intern(dngGraphicInfo[i].imageName);
        graphic[i].info = xu4.imageMgr->imageInfo(name, &graphic[i].sub);
    }
    // Cache right-fill entries (84-89) into static storage.
    for (i = 84; i < GRAPHIC_COUNT; ++i) {
        name = xu4.config->intern(dngGraphicInfo[i].imageName);
        rgtFillGraphic[i - 84].info = xu4.imageMgr->imageInfo(name, &rgtFillGraphic[i - 84].sub);
    }

    /* Text mode always uses EGA-style dungeon layout */
    egaLayout = true;
}

static void drawGraphic(const ImageInfo* info, const SubImage* subimage,
                        int x, int y) {
    x += BORDER_WIDTH;
    y += BORDER_HEIGHT;

    if (subimage) {
        info->image->drawSubRect(x, y,
                                 subimage->x,
                                 subimage->y,
                                 subimage->width,
                                 subimage->height);
    } else {
        info->image->draw(x, y);
    }
}

void DungeonView::drawWall(int index) {
    const SubImage* subimage;
    int x, y;
    int i2;

    if (index < 0)
        return;
    if (! graphic[index].info)
        return;

    subimage = graphic[index].sub;
    if (subimage) {
        /* In text mode, subimage position IS the draw position (char units) */
        x = subimage->x;
        y = subimage->y;
    } else {
        x = y = 0;
    }
    drawGraphic(graphic[index].info, subimage, x, y);

    /* Draw secondary fill graphic (ceiling/floor above/beside walls) */
    i2 = dngGraphicInfo[index].subimage2;
    if (i2) {
        /* ega_x2/ega_y2 are in character units (already converted) */
        x = dngGraphicInfo[index].ega_x2;
        y = dngGraphicInfo[index].ega_y2;

        // Use right-side fill variant if available for right walls.
        int groupOffset = (index % 6);
        bool isRight = (groupOffset >= 4);
        if (isRight) {
            int rgtIdx = i2 - 72;
            if (rgtFillGraphic[rgtIdx].info)
                drawGraphic(rgtFillGraphic[rgtIdx].info, rgtFillGraphic[rgtIdx].sub, x, y);
            else
                drawGraphic(graphic[i2].info, graphic[i2].sub, x, y);
        } else {
            drawGraphic(graphic[i2].info, graphic[i2].sub, x, y);
        }
    }
}

