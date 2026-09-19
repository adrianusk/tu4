/*
 * tileanim.cpp - Text-mode tile animation
 *
 * Adapted from original xu4 tileanim.cpp pixel-based animation to work
 * with 4x4 character cell tiles from MYSHAPES.ASP.
 */

#include "config.h"
#include "image.h"
#include "imagemgr.h"
#include "savegame.h"
#include "screen.h"
#include "tileanim.h"
#include "u4.h"
#include "utils.h"
#include "tile.h"
#include "xu4.h"

#define TILE_CHARS_W  4
#define TILE_CHARS_H  4
#define TILE_BYTES    32

/**
 * Get raw tile data from MYSHAPES.ASP for a given MapTile (module ID + frame).
 * Converts to U4 save ID before indexing.
 */
static const uint8_t* animGetTileData(const MapTile &mapTile) {
    ImageInfo* tilesInfo = xu4.imageMgr->get(BKGD_SHAPES);
    if (!tilesInfo || !tilesInfo->image)
        return NULL;
    const uint8_t* shapes = tilesInfo->image->getAspData();
    if (!shapes)
        return NULL;
    const UltimaSaveIds* usaveIds = xu4.config->usaveIds();
    uint8_t uid = usaveIds->ultimaId(mapTile);
    return shapes + (uid * TILE_BYTES);
}

/**
 * Draw a tile's character data at screen position (screenX, screenY).
 */
bool g_tileTransparent = false;

static void animDrawTile(int screenX, int screenY, const uint8_t* tileData) {
    for (int row = 0; row < TILE_CHARS_H; row++) {
        for (int col = 0; col < TILE_CHARS_W; col++) {
            int bi = row * (TILE_CHARS_W * 2) + col * 2;
            uint8_t ch   = tileData[bi];
            uint8_t attr = tileData[bi + 1];
            if (g_tileTransparent && ch == 0x20)
                continue;
            screenPutChar(screenX + col, screenY + row, ch, attr);
        }
    }
}

/**
 * Draw a tile with vertical scroll at screen position.
 */
static void animDrawTileScroll(int screenX, int screenY, const uint8_t* tileData, int scrollOffset) {
    for (int row = 0; row < TILE_CHARS_H; row++) {
        int srcRow = (row - scrollOffset + TILE_CHARS_H) % TILE_CHARS_H;
        for (int col = 0; col < TILE_CHARS_W; col++) {
            int bi = srcRow * (TILE_CHARS_W * 2) + col * 2;
            uint8_t ch   = tileData[bi];
            uint8_t attr = tileData[bi + 1];
            if (g_tileTransparent && ch == 0x20)
                continue;
            screenPutChar(screenX + col, screenY + row, ch, attr);
        }
    }
}

void TileAnimTransform::draw(int screenX, int screenY, const Tile* tile,
                             const MapTile& mapTile)
{
    switch(animType) {
    case ATYPE_SCROLL:
    {
        /* Advance scroll offset to match introt.c timing:
         * introt.c runs at 10fps, scrolls every 4 frames = 400ms per row.
         * We tick at 4Hz (250ms). Use currentCycle/4 so offset changes
         * every 4 ticks = 1000ms, giving a full 4-row cycle in 4 seconds.
         * This is close to introt.c's 1600ms full cycle. */
        int offset = screenState()->currentCycle / 2;
        if (var.scroll.lastOffset != offset) {
            var.scroll.lastOffset = offset;
            var.scroll.current += 1;
            if (var.scroll.current >= TILE_CHARS_H)
                var.scroll.current = 0;
        }
        const uint8_t* tileData = animGetTileData(mapTile);
        if (tileData)
            animDrawTileScroll(screenX, screenY, tileData, var.scroll.current);
    }
        break;

    case ATYPE_FRAME:
    {
        int frame;
        if (xu4.stage == StagePlay) {
            frame = mapTile.frame;
        } else {
            /* Intro map: shared frame counter for all tiles of this type */
            if (++var.frame.current >= tile->getFrames())
                var.frame.current = 0;
            frame = var.frame.current;
        }
        MapTile mt(mapTile.id, frame);
        const uint8_t* tileData = animGetTileData(mt);
        if (tileData)
            animDrawTile(screenX, screenY, tileData);
    }
        break;

    case ATYPE_INVERT:
        /* Flag inversion: just draw the base tile (flags are cosmetic) */
        {
            const uint8_t* tileData = animGetTileData(mapTile);
            if (tileData)
                animDrawTile(screenX, screenY, tileData);
        }
        break;

    case ATYPE_CHAR_ALT:
        /* Alternate between tile's original character and altchar at a given position.
         * The toggle happens in TileAnim::draw() when random check passes.
         * Here we just draw the base tile with the current state.
         * Note: x,y from XML are 1-based, convert to 0-based for indexing. */
        {
            const uint8_t* tileData = animGetTileData(mapTile);
            if (tileData) {
                animDrawTile(screenX, screenY, tileData);

                if (var.charAlt.current) {
                    /* Overwrite the character at the specified position with altchar */
                    int cx = var.charAlt.x - 1;
                    int cy = var.charAlt.y - 1;
                    int bi = cy * (TILE_CHARS_W * 2) + cx * 2;
                    uint8_t attr = tileData[bi + 1];
                    screenPutChar(screenX + cx, screenY + cy, var.charAlt.altchar, attr);
                }
            }
        }
        break;

    case ATYPE_COLOR_ALT:
        /* Alternate the color at a given position between original and altcolor.
         * x,y from XML are 1-based. */
        {
            const uint8_t* tileData = animGetTileData(mapTile);
            if (tileData) {
                animDrawTile(screenX, screenY, tileData);

                if (var.colorAlt.current) {
                    int cx = var.colorAlt.x - 1;
                    int cy = var.colorAlt.y - 1;
                    int bi = cy * (TILE_CHARS_W * 2) + cx * 2;
                    uint8_t ch = tileData[bi];
                    screenPutChar(screenX + cx, screenY + cy, ch, var.colorAlt.altcolor);
                }
            }
        }
        break;

    default:
        /* ATYPE_PIXEL_COLOR etc: just draw base tile */
        {
            const uint8_t* tileData = animGetTileData(mapTile);
            if (tileData)
                animDrawTile(screenX, screenY, tileData);
        }
        break;
    }
}

//--------------------------------------

TileAnim::~TileAnim()
{
#ifndef USE_BORON
    std::vector<TileAnimTransform *>::iterator ti;
    foreach (ti, transforms)
        delete *ti;
#endif
}

/* Legacy Image* overloads (called by dungeonview.cpp - no-ops in text mode) */
void TileAnimTransform::draw(Image* dest, const Tile* tile, const MapTile& mapTile) {
    (void)dest; (void)tile; (void)mapTile;
}

void TileAnim::draw(Image *dest, const Tile *tile, const MapTile &mapTile, Direction dir) {
    (void)dest; (void)tile; (void)mapTile; (void)dir;
}

static bool drawsTile(const TileAnimTransform* tf)
{
    return (tf->animType == ATYPE_SCROLL || tf->animType == ATYPE_FRAME
         || tf->animType == ATYPE_CHAR_ALT || tf->animType == ATYPE_COLOR_ALT);
}

void TileAnim::draw(int screenX, int screenY, const Tile *tile, const MapTile &mapTile, Direction dir)
{
    if (mapTile.freezeAnimation || (random && xu4_random(100) > random)) {
        /* Not animating this tick: for char_alt/color_alt, still draw current state */
        bool altDrawn = false;
        if (!mapTile.freezeAnimation) {
            std::vector<TileAnimTransform *>::const_iterator it;
            foreach (it, transforms) {
                TileAnimTransform* trans = *it;
                if (trans->animType == ATYPE_CHAR_ALT || trans->animType == ATYPE_COLOR_ALT) {
                    if (trans->context == ACON_FRAME &&
                        mapTile.frame != trans->contextSelect)
                        continue;
                    if (trans->context == ACON_DIR &&
                        dir != trans->contextSelect)
                        continue;
                    trans->draw(screenX, screenY, tile, mapTile);
                    altDrawn = true;
                    break;
                }
            }
        }
        if (!altDrawn) {
            const uint8_t* tileData = animGetTileData(mapTile);
            if (tileData)
                animDrawTile(screenX, screenY, tileData);
        }
        return;
    }

    bool drawn = false;
    std::vector<TileAnimTransform *>::const_iterator it;
    foreach (it, transforms) {
        TileAnimTransform* trans = *it;

        if (trans->context == ACON_FRAME) {
            if (mapTile.frame != trans->contextSelect)
                continue;
        } else if (trans->context == ACON_DIR) {
            if (dir != trans->contextSelect)
                continue;
        }

        if (! trans->random || xu4_random(100) < trans->random) {
            if (! drawsTile(trans) && ! drawn) {
                const uint8_t* tileData = animGetTileData(mapTile);
                if (tileData)
                    animDrawTile(screenX, screenY, tileData);
            }
            /* Toggle char_alt/color_alt state when the random check passes */
            if (trans->animType == ATYPE_CHAR_ALT)
                trans->var.charAlt.current ^= 1;
            else if (trans->animType == ATYPE_COLOR_ALT)
                trans->var.colorAlt.current ^= 1;
            trans->draw(screenX, screenY, tile, mapTile);
            drawn = true;
        }
    }

    if (!drawn) {
        /* No transform fired: draw base tile */
        const uint8_t* tileData = animGetTileData(mapTile);
        if (tileData)
            animDrawTile(screenX, screenY, tileData);
    }
}

//--------------------------------------

TileAnimSet::~TileAnimSet()
{
    TileAnimMap::iterator it;
    foreach (it, tileanims)
        delete it->second;
}

/**
 * Returns the tile animation with the given name from the current set
 */
TileAnim* TileAnimSet::getByName(Symbol name) const
{
    TileAnimMap::const_iterator i = tileanims.find(name);
    if (i == tileanims.end())
        return NULL;
    return i->second;
}
