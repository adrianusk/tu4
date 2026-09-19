/*
 * tileview.cpp - Text-mode tile view
 * Draws 4x4 character tiles from MYSHAPES.ASP data into the text buffer.
 * Handles tile animation (scroll for water, frame swap for creatures).
 */

#include "debug.h"
#include "config.h"
#include "imagemgr.h"
#include "savegame.h"
#include "settings.h"
#include "screen.h"
#include "tileanim.h"
#include "tileset.h"
#include "tileview.h"
#include "u4.h"
#include "tu4.h"

using std::vector;

/* Tile dimensions in characters */
#define TILE_CHARS_W  4
#define TILE_CHARS_H  4
#define TILE_BYTES    32   /* 4 rows * 4 cells * 2 bytes */

TileView::TileView(int x, int y, int columns, int rows)
    : View(x, y, columns * TILE_CHARS_W, rows * TILE_CHARS_H) {
    this->columns = columns;
    this->rows = rows;
    tileWidth  = TILE_CHARS_W;
    tileHeight = TILE_CHARS_H;
    tileset = tu4.config->tileset();
    animated = NULL;  /* not used in text mode */
}

TileView::~TileView() {
}

void TileView::reinit() {
    View::reinit();
    tileset = tu4.config->tileset();
}

/**
 * Load tile data for animation purposes.
 * In text mode, tile data is accessed directly from the ASP buffer,
 * so this is a no-op.
 */
void TileView::loadTile(const MapTile &mapTile) {
    (void)mapTile;
}

/**
 * Get a pointer to the raw tile data (char+attr pairs) for the given MapTile.
 * MYSHAPES.ASP is indexed by U4 save ID (0-255), not by module tile ID.
 * Uses ultimaId() to convert module ID + frame to the correct U4 index.
 * Returns pointer to 32 bytes (4 rows x 4 cells x 2 bytes).
 */
static const uint8_t* getTileData(const MapTile &mapTile) {
    ImageInfo* tilesInfo = tu4.imageMgr->get(BKGD_SHAPES);
    if (!tilesInfo || !tilesInfo->image)
        return NULL;

    const uint8_t* shapes = tilesInfo->image->getAspData();
    if (!shapes)
        return NULL;

    const UltimaSaveIds* usaveIds = tu4.config->usaveIds();
    uint8_t uid = usaveIds->ultimaId(mapTile);

    return shapes + (uid * TILE_BYTES);
}

/**
 * Draw a single tile at grid position (x, y) within this view.
 * If the tile has an animation, delegates to TileAnim::draw().
 * Otherwise draws the static tile data directly.
 */
void TileView::drawTile(const MapTile &mapTile, int x, int y) {
    int screenX = this->x + x * TILE_CHARS_W;
    int screenY = this->y + y * TILE_CHARS_H;

    const Tile* tile = tileset->get(mapTile.id);
    if (tile && tile->getAnim()) {
        tile->getAnim()->draw(screenX, screenY, tile, mapTile, DIR_NONE);
        return;
    }

    const uint8_t* tileData = getTileData(mapTile);
    if (!tileData)
        return;

    /* Draw the 4x4 tile to the text buffer */
    for (int row = 0; row < TILE_CHARS_H; row++) {
        for (int col = 0; col < TILE_CHARS_W; col++) {
            int byteIdx = row * (TILE_CHARS_W * 2) + col * 2;
            uint8_t ch   = tileData[byteIdx];
            uint8_t attr = tileData[byteIdx + 1];
            if (g_tileTransparent && ch == 0x20)
                continue;
            screenPutChar(screenX + col, screenY + row, ch, attr);
        }
    }
}

/**
 * Draw a single tile with transparency enabled.
 * Sets the global flag and delegates to drawTile.
 */
void TileView::drawTileTransparent(const MapTile &mapTile, int x, int y) {
    g_tileTransparent = true;
    drawTile(mapTile, x, y);
    g_tileTransparent = false;
}

/**
 * Draw a tile with vertical row scrolling (for water/lava/field animation).
 * scrollOffset shifts which source row maps to which display row,
 * wrapping around modulo 4 (the tile height).
 * Matches xu4 scroll direction (downward).
 */
void TileView::drawTileScroll(const MapTile &mapTile, int x, int y, int scrollOffset) {
    int screenX = this->x + x * TILE_CHARS_W;
    int screenY = this->y + y * TILE_CHARS_H;

    const uint8_t* tileData = getTileData(mapTile);
    if (!tileData)
        return;

    /* Draw the 4x4 tile with row offset for scroll animation */
    for (int row = 0; row < TILE_CHARS_H; row++) {
        int srcRow = (row - scrollOffset + TILE_CHARS_H) % TILE_CHARS_H;
        for (int col = 0; col < TILE_CHARS_W; col++) {
            int byteIdx = srcRow * (TILE_CHARS_W * 2) + col * 2;
            uint8_t ch   = tileData[byteIdx];
            uint8_t attr = tileData[byteIdx + 1];
            screenPutChar(screenX + col, screenY + row, ch, attr);
        }
    }
}

/**
 * Draw multiple tiles stacked (for transparency layering).
 * When tile transparency is enabled: draw all layers from back (terrain)
 * to front (creature/object), with transparency after the first layer.
 * When disabled: draw only the topmost (front) tile opaquely.
 */
void TileView::drawTile(vector<MapTile> &tiles, int x, int y) {
    if (tiles.empty())
        return;

    /* If only one tile or transparency disabled, just draw the front tile */
    if (tiles.size() <= 1 ||
        !tu4.settings->enhancements ||
        !tu4.settings->enhancementsOptions.u4TileTransparencyHack) {
        drawTile(tiles.front(), x, y);
        return;
    }

    /* Draw all layers from back (terrain) to front (creature/object).
     * First layer is opaque, subsequent layers skip transparent cells. */
    int layer = 0;
    for (vector<MapTile>::reverse_iterator t = tiles.rbegin();
         t != tiles.rend(); ++t, ++layer) {
        if (layer > 0)
            g_tileTransparent = true;
        drawTile(*t, x, y);
    }
    g_tileTransparent = false;
}

/**
 * Draw a blinking focus indicator at the four inside corners of a tile.
 * Blinks on/off based on the screen animation cycle, matching tu4.
 */
void TileView::drawFocus(int x, int y) {
    /* Blink: only draw on alternate half-cycles */
    if ((screenState()->currentCycle * 4 / SCR_CYCLE_PER_SECOND) % 2 == 0)
        return;

    int screenX = this->x + x * TILE_CHARS_W;
    int screenY = this->y + y * TILE_CHARS_H;

    uint8_t attr = 0x0F;  /* bright white on black */

    /* Inside corners of the 4x4 tile */
    screenPutChar(screenX,                   screenY,                   0xDA, attr);  /* ┌ */
    screenPutChar(screenX + TILE_CHARS_W - 1, screenY,                   0xBF, attr);  /* ┐ */
    screenPutChar(screenX,                   screenY + TILE_CHARS_H - 1, 0xC0, attr);  /* └ */
    screenPutChar(screenX + TILE_CHARS_W - 1, screenY + TILE_CHARS_H - 1, 0xD9, attr);  /* ┘ */
}

/* clear() inherited from View */
