/*
 * screen.cpp - Text-mode screen buffer (80x50, 16-color, CP437)
 *
 * Maintains the 80x50 character+attribute text buffer.
 * All rendering goes through screenPutChar/screenGetChar.
 * screenSwapBuffers() triggers the Allegro backend to render
 * the buffer to the window.
 */

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "screen.h"
#include "config.h"
#include "context.h"
#include "dungeonview.h"
#include "error.h"
#include "event.h"
#include "imagemgr.h"
#include "location.h"
#include "map.h"
#include "names.h"
#include "savegame.h"
#include "settings.h"
#include "tile.h"
#include "tileanim.h"
#include "tileset.h"
#include "tileview.h"
#include "textview.h"
#include "u4.h"
#include "tu4.h"

using namespace std;

/*
 * The text-mode screen buffer: SCREEN_COLS columns x SCREEN_ROWS rows x
 * 2 bytes (char + attr).  Dimensions come from screen.h.
 */
static uint8_t textBuffer[SCREEN_ROWS][SCREEN_COLS][2];

static ScreenState scrState;
static int textCursorX = 0;
static int textCursorY = 0;
static int textColor = 0x0F;  /* default: white on black */
static int messageX = 0;
static int messageY = 0;

/* Layout names for gem view */
static vector<string> gemLayoutNames;
static const char* filterNames[] = { "none", NULL };

/*
 * Screen struct - holds line-of-sight and blocking data for map rendering.
 */
struct Screen {
    DungeonView* dungeonView;
    uint8_t blockingGrid[VIEWPORT_W * VIEWPORT_H];
    uint8_t screenLos[VIEWPORT_W * VIEWPORT_H];

    Screen() {
        dungeonView = NULL;
        memset(blockingGrid, 0, sizeof(blockingGrid));
        memset(screenLos, 0, sizeof(screenLos));
    }
    ~Screen() {
        delete dungeonView;
    }
};
static const char* losStyles[] = { "DOS", "Enhanced", NULL };

/* Forward declarations for sys backend */
extern void screenInit_sys(const Settings* settings, int* dim, int reset);
extern void screenDelete_sys();


/*-----------------------------------------------------------------------
 * Text buffer access (public API used by textview, tileview, imageview, etc.)
 *-----------------------------------------------------------------------*/

void screenPutChar(int x, int y, uint8_t ch, uint8_t attr) {
    if (x >= 0 && x < SCREEN_COLS && y >= 0 && y < SCREEN_ROWS) {
        textBuffer[y][x][0] = ch;
        textBuffer[y][x][1] = attr;
    }
}

void screenGetChar(int x, int y, uint8_t *ch, uint8_t *attr) {
    if (x >= 0 && x < SCREEN_COLS && y >= 0 && y < SCREEN_ROWS) {
        *ch   = textBuffer[y][x][0];
        *attr = textBuffer[y][x][1];
    } else {
        *ch = 0;
        *attr = 0;
    }
}

/**
 * Get a pointer to the raw text buffer for rendering.
 * Used by screen_allegro.cpp to read the buffer for display.
 */
const uint8_t* screenGetBuffer() {
    return (const uint8_t*)textBuffer;
}


/*-----------------------------------------------------------------------
 * Screen lifecycle
 *-----------------------------------------------------------------------*/

void screenInit() {
    /* Clear the text buffer */
    memset(textBuffer, 0, sizeof(textBuffer));

    /* Allocate Screen struct for map rendering data */
    if (!tu4.screen)
        tu4.screen = new Screen();

    /* Initialize screen state */
    scrState.tileanims = NULL;
    scrState.currentCycle = 0;
    scrState.vertOffset = 0;
    scrState.formatIsABGR = false;

    /* Create the image manager */
    if (!tu4.imageMgr)
        tu4.imageMgr = new ImageMgr();

    /* Initialize the system display (SDL window) */
    /* Logical pixel size = character grid * 8px glyph cell (see backend). */
    int dim[4] = {0, 0, SCREEN_COLS * 8, SCREEN_ROWS * 8};
    screenInit_sys(tu4.settings, dim, 0);

    /* Load tile animations if available */
    scrState.tileanims = tu4.config->newTileAnims(tu4.settings->textStyle.c_str());

    /* Load tile images (resolves animation rules for each tile) */
    Tileset::loadImages();

    /* Set up gem layout names */
    uint32_t layoutCount;
    const Layout* layouts = tu4.config->layouts(&layoutCount);
    gemLayoutNames.clear();
    for (uint32_t i = 0; i < layoutCount; i++) {
        if (layouts[i].type == LAYOUT_GEM) {
            gemLayoutNames.push_back(tu4.config->symbolName(layouts[i].name));
        }
    }

    /* Set message area position (right panel, below stats) */
    messageX = 48;
    messageY = 24;
}

void screenDelete() {
    screenDelete_sys();
}

/**
 * Re-initializes the screen and implements any changes made in settings.
 *
 * Faithful to xu4's screenReInit() (screen.cpp): tear down the graphics data
 * (image manager, tile animations, tile images) and rebuild it, so a changed
 * text style reloads all graphics assets from the newly selected scheme.
 * Adapted for the text UI: instead of xu4's pixel screenImage, we resize the
 * SDL window via screenInit_sys(reset=1).
 */
void screenReInit() {
    /* --- Tear down graphics data (mirrors xu4 screenDelete_data) --- */
    Tileset::unloadImages();        // free per-tile Image* copies

    /* An in-progress game may hold cached graphics state that must be
       reloaded for the newly selected theme. The DungeonView caches raw
       pointers into the scheme's images/ASP data (dungObj0/npc1/obj1Data +
       graphic[].info/.sub); those images are freed just below by
       'delete tu4.imageMgr'. Inspect whether such state is loaded (a
       DungeonView exists => a dungeon game is in progress), destroy it here,
       and RECREATE it after the image manager is rebuilt so it re-caches
       against the new scheme.

       Recreate (not just NULL): resuming an in-progress dungeon via
       "Journey Onward" does NOT go through setMap()/screenMakeDungeonView()
       -- the location is unchanged -- so a NULL dungeonView would leave the
       3D view undrawn (black screen). */
    bool hadDungeonView = (tu4.screen && tu4.screen->dungeonView);
    if (tu4.screen) {
        delete tu4.screen->dungeonView;
        tu4.screen->dungeonView = NULL;
    }

    delete scrState.tileanims;
    scrState.tileanims = NULL;

    delete tu4.imageMgr;            // frees every cached ImageSet/ImageInfo->image
    tu4.imageMgr = NULL;

    /* --- Resize the display for any scale/fullscreen change --- */
    /* Logical pixel size = character grid * 8px glyph cell (see backend). */
    int dim[4] = {0, 0, SCREEN_COLS * 8, SCREEN_ROWS * 8};
    screenInit_sys(tu4.settings, dim, 1);

    /* --- Rebuild graphics data (mirrors xu4 screenInit_data) --- */
    tu4.imageMgr = new ImageMgr();  // ctor re-points baseSet to current textStyle

    scrState.tileanims = tu4.config->newTileAnims(tu4.settings->textStyle.c_str());

    Tileset::loadImages();          // reload tile Image* copies from new scheme

    /* Reload the in-progress dungeon's cached art against the new scheme.
       screenMakeDungeonView() rebuilds the DungeonView (re-caching all its
       image/ASP pointers) via the now-rebuilt imageMgr and reloaded tiles. */
    if (hadDungeonView)
        screenMakeDungeonView();
}

/*
 * Resize the output window for a scale/fullscreen change ONLY.
 *
 * A scale (or fullscreen) change alters only the on-screen window size; the
 * logical framebuffer and every loaded graphics asset are unchanged (the text
 * style is the same). So unlike screenReInit() this must NOT delete/reload the
 * ImageMgr, tiles, tile-anims or dungeon view — doing so is wasted work and
 * invalidates cached ImageInfo pointers (e.g. the intro's beastiesImg), which
 * made the intro beasties/ANIMATE stop drawing after a scale change.
 *
 * screenInit_sys(reset=1) re-reads settings->scale and resizes the SDL window;
 * the renderer keeps its logical size + nearest scaling, so the same
 * framebuffer is simply scaled to the new window.
 */
void screenResize() {
    int dim[4] = {0, 0, SCREEN_COLS * 8, SCREEN_ROWS * 8};
    screenInit_sys(tu4.settings, dim, 1);
}

void screenRefreshTimerInit() {
    /* Timer-based refresh handled by event loop */
}


/*-----------------------------------------------------------------------
 * Screen output
 *-----------------------------------------------------------------------*/

void screenTextAt(int x, int y, const char *fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    uint8_t attr = (uint8_t)textColor;
    for (int i = 0; buffer[i] && (x + i) < SCREEN_COLS; i++) {
        screenPutChar(x + i, y, (uint8_t)buffer[i], attr);
    }
}

void screenTextColor(int color) {
    textColor = color;
}

void screenShowChar(int chr, int x, int y) {
    uint8_t ch = (uint8_t)chr;
    uint8_t attr = (uint8_t)textColor;

    if (ch < 32) {
        ImageInfo* charsetInfo = tu4.imageMgr->get(BKGD_CHARSET);
        if (charsetInfo && charsetInfo->image && charsetInfo->image->getAspData()) {
            const uint8_t* data = charsetInfo->image->getAspData();
            int idx = ch * 2;
            ch = data[idx];
            attr = data[idx + 1];
        }
    }

    screenPutChar(x, y, ch, attr);
}

void screenShowCharMasked(int chr, int x, int y, unsigned char mask) {
    /* In text mode, masking doesn't apply meaningfully */
    if (mask == 0xFF)
        screenPutChar(x, y, ' ', 0x00);
    else
        screenPutChar(x, y, (uint8_t)chr, (uint8_t)textColor);
}

void screenMessage(const char *fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    screenMessageN(buffer, strlen(buffer));
}

// whitespace & color codes for word-wrap detection
static const uint8_t nonWordChars[32] = {
    0x01, 0x27, 0xF8, 0x03, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

#define IS_NONWORD(c) (nonWordChars[(c) >> 3] & (1 << ((c) & 7)))

void screenMessageN(const char* buffer, int buflen) {
    if (!c) return;

    const int colCount = TEXT_AREA_W;
    int i, w;

    for (i = 0; i < buflen; i++) {
        switch (buffer[i]) {
            case '\b':          // backspace
                c->col--;
                if (c->col < 0) {
                    c->col += colCount;
                    c->line--;
                }
                continue;

            case '\n':          // new line
newline:
                screenCrLf();
                continue;

            case '\r':          // carriage return
                c->col = 0;
                continue;

            case 0x12:          // DC2 - move cursor right
                c->col++;
                continue;

            case FG_GREY:
            case FG_BLUE:
            case FG_PURPLE:
            case FG_GREEN:
            case FG_RED:
            case FG_YELLOW:
            case FG_WHITE:
                continue;

            case '\t':
            case ' ':
                if (c->col == colCount)
                    goto newline;
                if (c->col == 0)
                    continue;  // skip leading space on new line
                screenPutChar(TEXT_AREA_X + c->col, TEXT_AREA_Y + c->line, ' ', (uint8_t)textColor);
                c->col++;
                continue;

            default:
                break;
        }

        if (c->col == colCount) {
            --i;
            goto newline;
        }

        // Check for word wrap
        for (w = i; w < buflen; ++w) {
            unsigned int ch = ((uint8_t*)buffer)[w];
            if (IS_NONWORD(ch))
                break;
        }
        if (w == i)
            continue;
        if (c->col + (w - i) > colCount) {
            if (c->col > 0) {
                --i;
                goto newline;
            }
            // Word doesn't fit on one line — break it up
            w = i + colCount;
        }

        // Draw the word
        for (; i < w; ++i) {
            uint8_t ch = (uint8_t)buffer[i];
            uint8_t attr = (uint8_t)textColor;

            if (ch < 32) {
                ImageInfo* charsetInfo = tu4.imageMgr->get(BKGD_CHARSET);
                if (charsetInfo && charsetInfo->image && charsetInfo->image->getAspData()) {
                    const uint8_t* data = charsetInfo->image->getAspData();
                    int idx = ch * 2;
                    ch = data[idx];
                    attr = data[idx + 1];
                }
            }

            screenPutChar(TEXT_AREA_X + c->col, TEXT_AREA_Y + c->line, ch, attr);
            c->col++;
        }
        --i;  // undo loop increment (for loop will ++ again)
    }
}

void screenCrLf() {
    if (!c) return;
    c->col = 0;
    c->line++;
    if (c->line >= TEXT_AREA_H) {
        /* Scroll message area up by 2 screen rows */
        int sy = TEXT_AREA_Y;
        for (int row = sy; row < sy + TEXT_AREA_H - 2; row++) {
            memcpy(textBuffer[row] + TEXT_AREA_X, textBuffer[row + 2] + TEXT_AREA_X, TEXT_AREA_W * 2);
        }
        /* Clear bottom two screen rows */
        for (int row = sy + TEXT_AREA_H - 2; row < sy + TEXT_AREA_H; row++) {
            for (int col = TEXT_AREA_X; col < TEXT_AREA_X + TEXT_AREA_W; col++) {
                textBuffer[row][col][0] = ' ';
                textBuffer[row][col][1] = 0x00;
            }
        }
        c->line = TEXT_AREA_H - 1;
    }
}

void screenPrompt() {
    if (c && c->col == 0) {
        screenMessage("%c", CHARSET_PROMPT);
    }
}


/*-----------------------------------------------------------------------
 * Map / tile rendering
 *-----------------------------------------------------------------------*/

void screenEraseMapArea() {
    /* Clear the map viewport (44x44 chars starting at col 2, row 2) */
    for (int row = 2; row < 46; row++) {
        for (int col = 2; col < 46; col++) {
            textBuffer[row][col][0] = ' ';
            textBuffer[row][col][1] = 0x00;
        }
    }
}

void screenEraseTextArea(int x, int y, int width, int height) {
    for (int row = y; row < y + height && row < SCREEN_ROWS; row++) {
        for (int col = x; col < x + width && col < SCREEN_COLS; col++) {
            textBuffer[row][col][0] = ' ';
            textBuffer[row][col][1] = 0x00;
        }
    }
}

void screenDrawImageInMapArea(Symbol bkgd) {
    ImageInfo* info = tu4.imageMgr->get(bkgd);
    if (!info || !info->image)
        return;

    const uint8_t* aspData = info->image->getAspData();
    if (!aspData)
        return;

    /*
     * Two source layouts are supported:
     *
     *  1. Native 44x44 map-area images (shrine runes, stone circle): the ASP
     *     payload is exactly the 44x44 map viewport, drawn 1:1 with no border
     *     offset. Detected by getCols()==mapW (set by the loader for a
     *     3872-byte payload).
     *
     *  2. Legacy full-screen 80x50 images: mimic xu4's drawSubRect, extracting
     *     the map viewport region at (BORDER_WIDTH, BORDER_HEIGHT) with an
     *     80-wide source stride:
     *       drawSubRect(BORDER_WIDTH, BORDER_HEIGHT,
     *                   BORDER_WIDTH, BORDER_HEIGHT,
     *                   VIEWPORT_W * TILE_WIDTH, VIEWPORT_H * TILE_HEIGHT)
     *
     * Blending: skip cells where char=' ' and attr=0x00 (transparent), so
     * codex endgame images can layer on top of each other.
     */
    const int mapW = VIEWPORT_W * TILE_WIDTH;   /* 44 chars */
    const int mapH = VIEWPORT_H * TILE_HEIGHT;  /* 44 chars */
    const int srcCols = info->image->getCols();  /* 44 native, 80 full-screen */
    const bool native = (srcCols == mapW);       /* native 44x44 map-area image */
    const int srcOffX = native ? 0 : BORDER_WIDTH;
    const int srcOffY = native ? 0 : BORDER_HEIGHT;

    for (int row = 0; row < mapH; row++) {
        for (int col = 0; col < mapW; col++) {
            int srcIdx = ((srcOffY + row) * srcCols + (srcOffX + col)) * 2;
            uint8_t ch   = aspData[srcIdx];
            uint8_t attr = aspData[srcIdx + 1];
            /* Skip transparent cells (space with black attribute) */
            if (ch == ' ')
                continue;
            textBuffer[BORDER_HEIGHT + row][BORDER_WIDTH + col][0] = ch;
            textBuffer[BORDER_HEIGHT + row][BORDER_WIDTH + col][1] = attr;
        }
    }
}

void screenRedrawMapArea() {
    /* Map redraw handled by screenUpdate */
}

void screenGemUpdate() {
    MapTile tile;
    int x, y;
    const Layout* layout;
    const Map* map = c->location->map;
    bool focus;

    /* Find the appropriate gem layout */
    uint32_t layoutCount;
    const Layout* layouts = tu4.config->layouts(&layoutCount);
    const Layout* gemLayout = NULL;
    const Layout* dungeonGemLayout = NULL;

    for (uint32_t i = 0; i < layoutCount; i++) {
        if (layouts[i].type == LAYOUT_GEM && !gemLayout)
            gemLayout = &layouts[i];
        else if (layouts[i].type == LAYOUT_DUNGEONGEM && !dungeonGemLayout)
            dungeonGemLayout = &layouts[i];
    }

    /* Clear the full map area (44x44 at position 2,2 in 0-based coords) with black */
    for (y = 0; y < 44; y++) {
        for (x = 0; x < 44; x++) {
            screenPutChar(2 + x, 2 + y, ' ', 0x00);
        }
    }

    if (map->type == Map::DUNGEON) {
        /* DUNGEON GEM: flood-fill traversal from avatar position,
         * rendering with charset glyphs */
        layout = dungeonGemLayout ? dungeonGemLayout : gemLayout;
        if (!layout)
            return;

        /* Get charset data for dungeon glyph rendering */
        ImageInfo* charsetInfo = tu4.imageMgr->get(BKGD_CHARSET);
        const uint8_t* charsetData = NULL;
        if (charsetInfo && charsetInfo->image)
            charsetData = charsetInfo->image->getAspData();

        /* Dungeon tile name -> charset glyph mapping */
        struct DungeonGlyph {
            const char* name;
            uint8_t ch;
            uint8_t attr;
        };

        /* Build glyph table: for chars < 32, look up from CHARSET.ASP;
         * for chars >= 32, use the char directly with white-on-black */
        #define DG_CHARSET(c) 0, c   /* marker: ch=0 means look up from charset */
        #define DG_ASCII(c, a) c, a  /* direct char + attr */

        static const DungeonGlyph dungeonGlyphs[] = {
            { "brick_floor",    DG_CHARSET(CHARSET_FLOOR) },
            { "up_ladder",      DG_CHARSET(CHARSET_LADDER_UP) },
            { "down_ladder",    DG_CHARSET(CHARSET_LADDER_DOWN) },
            { "up_down_ladder", DG_CHARSET(CHARSET_LADDER_UPDOWN) },
            { "chest",          DG_ASCII('$', 0x0E) },  /* yellow $ */
            { "ceiling_hole",   DG_ASCII('T', 0x0F) },
            { "floor_hole",     DG_ASCII('T', 0x0F) },
            { "magic_orb",      DG_CHARSET(CHARSET_ORB) },
            { "fountain",       DG_ASCII('F', 0x09) },  /* light blue F */
            { "secret_door",    DG_CHARSET(CHARSET_SDOOR) },
            { "brick_wall",     DG_CHARSET(CHARSET_WALL) },
            { "dungeon_door",   DG_CHARSET(CHARSET_ROOM) },
            { "avatar",         DG_CHARSET(CHARSET_REDDOT) },
            { "dungeon_room",   DG_CHARSET(CHARSET_ROOM) },
            { "dungeon_altar",  DG_CHARSET(CHARSET_ANKH) },
            { "energy_field",   DG_ASCII('^', 0x0D) },  /* light magenta */
            { "fire_field",     DG_ASCII('^', 0x0C) },  /* light red */
            { "poison_field",   DG_ASCII('^', 0x0A) },  /* light green */
            { "sleep_field",    DG_ASCII('^', 0x0B) },  /* light cyan */
            { NULL, 0, 0 }
        };

        #undef DG_CHARSET
        #undef DG_ASCII

        /* Flood-fill from avatar position */
        vector<vector<int> > drawnTiles(layout->viewport.width,
                                        vector<int>(layout->viewport.height, 0));
        vector<std::pair<int,int> > coordStack;

        const Coords& coords = c->location->coords;
        int center_x = layout->viewport.width / 2 - 1;
        int center_y = layout->viewport.height / 2 - 1;
        int avt_x = coords.x - 1;
        int avt_y = coords.y - 1;

        coordStack.push_back(std::pair<int,int>(center_x, center_y));
        bool weAreDrawingTheAvatarTile = true;

        while (coordStack.size() > 0) {
            const std::pair<int,int> currentXY = coordStack.back();
            x = currentXY.first;
            y = currentXY.second;
            coordStack.pop_back();

            if (x < 0 || x >= layout->viewport.width ||
                y < 0 || y >= layout->viewport.height)
                continue;

            if (drawnTiles[x][y])
                continue;

            drawnTiles[x][y] = 1;

            /* Get the tile at this position */
            vector<MapTile> tiles = screenViewportTile(
                layout->viewport.width, layout->viewport.height,
                x - center_x + avt_x, y - center_y + avt_y, focus);
            tile = tiles.front();

            if (!weAreDrawingTheAvatarTile) {
                TileId avatarTileId =
                    map->tileset->getByName(Tile::sym.avatar)->getId();
                if (tile.getId() == avatarTileId)
                    tile = map->getTileFromData(coords);
            }

            /* Look up the glyph for this tile */
            const char* tileName = tile.getTileType()->nameStr();
            uint8_t ch = ' ';
            uint8_t attr = 0x00;
            bool found = false;

            for (int gi = 0; dungeonGlyphs[gi].name != NULL; gi++) {
                if (strcmp(tileName, dungeonGlyphs[gi].name) == 0) {
                    ch = dungeonGlyphs[gi].ch;
                    attr = dungeonGlyphs[gi].attr;
                    /* If ch == 0 and attr < 32, look up from charset data */
                    if (ch == 0 && charsetData) {
                        int idx = attr * 2;
                        ch = charsetData[idx];
                        attr = charsetData[idx + 1];
                    }
                    found = true;
                    break;
                }
            }

            if (found) {
                screenPutChar(layout->viewport.x + x,
                              layout->viewport.y + y, ch, attr);
            }

            /* Flood fill: continue through walkable/non-opaque tiles */
            if (!tile.getTileType()->isOpaque() ||
                tile.getTileType()->isWalkable() || weAreDrawingTheAvatarTile)
            {
                coordStack.push_back(std::pair<int,int>(x + 1, y - 1));
                coordStack.push_back(std::pair<int,int>(x + 1, y    ));
                coordStack.push_back(std::pair<int,int>(x + 1, y + 1));
                coordStack.push_back(std::pair<int,int>(x    , y - 1));
                coordStack.push_back(std::pair<int,int>(x    , y + 1));
                coordStack.push_back(std::pair<int,int>(x - 1, y - 1));
                coordStack.push_back(std::pair<int,int>(x - 1, y    ));
                coordStack.push_back(std::pair<int,int>(x - 1, y + 1));

                weAreDrawingTheAvatarTile = false;
            }
        }
    } else {
        /* WORLD MAP / CITY / TOWN / CASTLE: show all tiles using GEM.ASP */
        layout = gemLayout;
        if (!layout)
            return;

        /* Load gem tiles data from GEM.ASP */
        ImageInfo* gemInfo = tu4.imageMgr->get(BKGD_GEMTILES);
        const uint8_t* gemData = NULL;
        if (gemInfo && gemInfo->image)
            gemData = gemInfo->image->getAspData();
        if (!gemData)
            return;

        const UltimaSaveIds* usaveIds = tu4.config->usaveIds();

        for (x = 0; x < layout->viewport.width; x++) {
            for (y = 0; y < layout->viewport.height; y++) {
                tile = screenViewportTile(layout->viewport.width,
                                          layout->viewport.height,
                                          x, y, focus).front();

                unsigned int uid = usaveIds->ultimaId(tile);

                if (uid < 128) {
                    int idx = uid * 2;
                    uint8_t ch = gemData[idx];
                    uint8_t attr = gemData[idx + 1];
                    screenPutChar(layout->viewport.x + x,
                                  layout->viewport.y + y, ch, attr);
                } else {
                    /* Tiles >= 128 are drawn as black (space) */
                    screenPutChar(layout->viewport.x + x,
                                  layout->viewport.y + y, ' ', 0x00);
                }
            }
        }
    }

    screenUpdateMoons();
    screenUpdateWind();
}

void screenCycle() {
    scrState.currentCycle = (scrState.currentCycle + 1) % SCR_CYCLE_MAX;
    tu4.eventHandler->advanceFlourishAnim();
}

bool screenTileUpdate(TileView *view, const Coords &coords) {
    Location* loc = c->location;
    if (loc->map->flags & FIRST_PERSON)
        return false;

    int x = coords.x;
    int y = coords.y;

    if (loc->map->width > VIEWPORT_W || loc->map->height > VIEWPORT_H) {
        x = x - loc->coords.x + VIEWPORT_W / 2;
        y = y - loc->coords.y + VIEWPORT_H / 2;
    }

    if (x >= 0 && y >= 0 && x < VIEWPORT_W && y < VIEWPORT_H) {
        bool focus;
        Coords mc(coords);
        map_wrap(mc, loc->map);
        vector<MapTile> tiles;
        loc->getTilesAt(tiles, mc, focus);

        view->drawTile(tiles, x, y);
        if (focus)
            view->drawFocus(x, y);
        return true;
    }
    return false;
}

void screenShake(int iterations) {
    if (tu4.settings->screenShakes) {
        for (int i = 0; i < iterations; i++) {
            // shift the screen down
            scrState.vertOffset = 1;
            screenSwapBuffers();
            EventHandler::wait_msecs(tu4.settings->shakeInterval);

            // shift the screen back up
            scrState.vertOffset = 0;
            screenSwapBuffers();
            EventHandler::wait_msecs(tu4.settings->shakeInterval);
        }
    }
}


/*-----------------------------------------------------------------------
 * Cursor
 *-----------------------------------------------------------------------*/

void screenShowCursor() {}
void screenHideCursor() {}
void screenEnableCursor() {}
void screenDisableCursor() {}
bool screenCursorEnabled() { return false; }
void screenSetCursorPos(int x, int y) {
    textCursorX = x;
    textCursorY = y;
}
void screenUpdateCursor() {}


/*-----------------------------------------------------------------------
 * Status bar helpers
 *-----------------------------------------------------------------------*/

void screenUpdateMoons() {
    if (!c) return;

    /* show "L?" for the dungeon level */
    if (c->location->context == CTX_DUNGEON) {
        /* Erase the moon phase area (trammel + felucca = 4 chars wide, 2 rows) */
        for (int row = 0; row < 2; row++)
            for (int col = MOON_TRAMMEL_X; col < MOON_FELUCCA_X + 2; col++)
                screenPutChar(col, MOON_AREA_Y + row, ' ', 0x00);
        /* Center "L?" in the 4-char-wide moon area */
        screenShowChar('L', MOON_TRAMMEL_X + 1, MOON_AREA_Y);
        screenShowChar('1' + c->location->coords.z, MOON_TRAMMEL_X + 2, MOON_AREA_Y);
        return;
    }

    /* moons only shown non-combat */
    if ((c->location->context & CTX_NON_COMBAT) != c->location->context)
        return;

    ImageInfo* info = tu4.imageMgr->get(BKGD_MOONPHASES);
    if (!info || !info->image)
        return;
    const uint8_t* data = info->image->getAspData();
    if (!data)
        return;

    int trammelPhase = (c->saveGame->trammelphase == 0) ?
        7 : c->saveGame->trammelphase - 1;
    int feluccaPhase = (c->saveGame->feluccaphase == 0) ?
        7 : c->saveGame->feluccaphase - 1;

    /* Each moon phase tile is 2x2 chars = 8 bytes (4 cells * 2 bytes) */
    for (int i = 0; i < 2; i++) {
        int phase = (i == 0) ? trammelPhase : feluccaPhase;
        int destX = (i == 0) ? MOON_TRAMMEL_X : MOON_FELUCCA_X;
        const uint8_t* tile = data + (phase * 8);

        for (int row = 0; row < 2; row++) {
            for (int col = 0; col < 2; col++) {
                int idx = (row * 2 + col) * 2;
                screenPutChar(destX + col, MOON_AREA_Y + row, tile[idx], tile[idx + 1]);
            }
        }
    }
}

void screenUpdateWind() {
    if (!c) return;

    if (c->location->context == CTX_DUNGEON) {
        screenEraseTextArea(WIND_AREA_X, WIND_AREA_Y, WIND_AREA_W, WIND_AREA_H);
        screenTextAt(WIND_AREA_X, WIND_AREA_Y, "Dir: %5s", getDirectionName((Direction)c->saveGame->orientation));
    }
    else if ((c->location->context & CTX_NON_COMBAT) == c->location->context) {
        screenEraseTextArea(WIND_AREA_X, WIND_AREA_Y, WIND_AREA_W, WIND_AREA_H);
        screenTextAt(WIND_AREA_X, WIND_AREA_Y, "Wind %5s", getDirectionName((Direction) c->windDirection));
    }
}


/*-----------------------------------------------------------------------
 * Dungeon view
 *-----------------------------------------------------------------------*/

bool screenToggle3DDungeonView() {
    DungeonView* view = tu4.screen->dungeonView;
    if (view)
        return view->toggle3DDungeonView();
    return false;
}

void screenMakeDungeonView() {
    if (tu4.screen->dungeonView)
        return;
    tu4.screen->dungeonView = new DungeonView(BORDER_WIDTH, BORDER_HEIGHT,
                                              VIEWPORT_W, VIEWPORT_H);
}

void screenDetectDungeonTraps() {
    if (tu4.screen->dungeonView)
        tu4.screen->dungeonView->detectTraps();
}


/*-----------------------------------------------------------------------
 * Mouse
 *-----------------------------------------------------------------------*/

/* Mouse cursor functions are in screen_sdl2.cpp / screen_sdl3.cpp */

void screenPointToMouseArea(int* x, int* y) {
    /* SDL2 with RenderSetLogicalSize reports mouse events in logical
     * coordinates (640x400). Divide by CHAR size (8) to get char cell. */
    *x = *x / 8;
    *y = *y / 8;
}

static int pointInTriangle(int px, int py,
                           int x1, int y1, int x2, int y2, int x3, int y3) {
    /* Barycentric coordinate method */
    int d1 = (px - x2) * (y1 - y2) - (x1 - x2) * (py - y2);
    int d2 = (px - x3) * (y2 - y3) - (x2 - x3) * (py - y3);
    int d3 = (px - x1) * (y3 - y1) - (x3 - x1) * (py - y1);

    bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

    return !(has_neg && has_pos);
}

int pointInMouseArea(int x, int y, MouseArea *area) {
    if (area->npoints == 2) {
        /* Rectangle: point[0] = top-left, point[1] = bottom-right */
        if (x >= area->point[0].x && y >= area->point[0].y &&
            x <  area->point[1].x && y <  area->point[1].y)
            return 1;
    } else if (area->npoints == 3) {
        /* Triangle */
        return pointInTriangle(x, y,
            area->point[0].x, area->point[0].y,
            area->point[1].x, area->point[1].y,
            area->point[2].x, area->point[2].y);
    }
    return 0;
}


/*-----------------------------------------------------------------------
 * Misc queries
 *-----------------------------------------------------------------------*/

const vector<string>& screenGetGemLayoutNames() {
    return gemLayoutNames;
}

const char** screenGetFilterNames() {
    return (const char**)filterNames;
}

const char** screenGetLineOfSightStyles() {
    return (const char**)losStyles;
}

ScreenState* screenState() {
    return &scrState;
}

vector<MapTile> screenViewportTile(unsigned int width, unsigned int height, int x, int y, bool &focus) {
    Map* map = c->location->map;
    Coords center = c->location->coords;
    static MapTile grass;
    static bool grassInit = false;
    if (!grassInit) {
        grass = map->tileset->getByName(Tile::sym.grass)->getId();
        grassInit = true;
    }

    if (map->width <= (int)width && map->height <= (int)height) {
        center.x = map->width / 2;
        center.y = map->height / 2;
    }

    Coords tc = center;
    tc.x += x - (width / 2);
    tc.y += y - (height / 2);

    map_wrap(tc, map);

    if (MAP_IS_OOB(map, tc)) {
        focus = false;
        vector<MapTile> result;
        result.push_back(grass);
        return result;
    }

    vector<MapTile> tiles;
    c->location->getTilesAt(tiles, tc, focus);
    return tiles;
}

/**
 * Finds which tiles in the viewport are visible from the avatar's
 * location in the middle. (original DOS algorithm from xu4)
 */
#define BLOCKING(x,y)   blocking[(y) * VIEWPORT_W + (x)]
#define LOS(x,y)        lineOfSight[(y) * VIEWPORT_W + (x)]

static void screenFindLineOfSight(const uint8_t* blocking, uint8_t* lineOfSight) {
    int x, y;
    const int halfW = VIEWPORT_W / 2;
    const int halfH = VIEWPORT_H / 2;

    LOS(halfW, halfH) = 1;

    for (x = halfW - 1; x >= 0; x--)
        if (LOS(x + 1, halfH) && ! BLOCKING(x + 1, halfH))
            LOS(x, halfH) = 1;

    for (x = halfW + 1; x < VIEWPORT_W; x++)
        if (LOS(x - 1, halfH) && ! BLOCKING(x - 1, halfH))
            LOS(x, halfH) = 1;

    for (y = halfH - 1; y >= 0; y--)
        if (LOS(halfW, y + 1) && ! BLOCKING(halfW, y + 1))
            LOS(halfW, y) = 1;

    for (y = halfH + 1; y < VIEWPORT_H; y++)
        if (LOS(halfW, y - 1) && ! BLOCKING(halfW, y - 1))
            LOS(halfW, y) = 1;

    for (y = halfH - 1; y >= 0; y--) {

        for (x = halfW - 1; x >= 0; x--) {
            if (LOS(x, y + 1) && ! BLOCKING(x, y + 1))
                LOS(x, y) = 1;
            else if (LOS(x + 1, y) && ! BLOCKING(x + 1, y))
                LOS(x, y) = 1;
            else if (LOS(x + 1, y + 1) && ! BLOCKING(x + 1, y + 1))
                LOS(x, y) = 1;
        }

        for (x = halfW + 1; x < VIEWPORT_W; x++) {
            if (LOS(x, y + 1) && ! BLOCKING(x, y + 1))
                LOS(x, y) = 1;
            else if (LOS(x - 1, y) && ! BLOCKING(x - 1, y))
                LOS(x, y) = 1;
            else if (LOS(x - 1, y + 1) && ! BLOCKING(x - 1, y + 1))
                LOS(x, y) = 1;
        }
    }

    for (y = halfH + 1; y < VIEWPORT_H; y++) {

        for (x = halfW - 1; x >= 0; x--) {
            if (LOS(x, y - 1) && ! BLOCKING(x, y - 1))
                LOS(x, y) = 1;
            else if (LOS(x + 1, y) && ! BLOCKING(x + 1, y))
                LOS(x, y) = 1;
            else if (LOS(x + 1, y - 1) && ! BLOCKING(x + 1, y - 1))
                LOS(x, y) = 1;
        }

        for (x = halfW + 1; x < VIEWPORT_W; x++) {
            if (LOS(x, y - 1) && ! BLOCKING(x, y - 1))
                LOS(x, y) = 1;
            else if (LOS(x - 1, y) && ! BLOCKING(x - 1, y))
                LOS(x, y) = 1;
            else if (LOS(x - 1, y - 1) && ! BLOCKING(x - 1, y - 1))
                LOS(x, y) = 1;
        }
    }
}

#undef BLOCKING
#undef LOS

void screenUpdate(TileView *view, bool showmap, bool blackout) {
    if (!view || !c)
        return;

    Screen* scr = tu4.screen;
    if (!scr)
        return;

    if (blackout) {
        MapTile black = c->location->map->tileset->getByName(Tile::sym.black)->getId();
        for (int y = 0; y < VIEWPORT_H; y++)
            for (int x = 0; x < VIEWPORT_W; x++)
                view->drawTile(black, x, y);
    }
    else if (c->location->map->flags & FIRST_PERSON) {
        /* Dungeon first-person 3D view */
        if (tu4.screen->dungeonView)
            tu4.screen->dungeonView->display(c, view);
    }
    else if (showmap) {
        MapTile black = c->location->map->tileset->getByName(Tile::sym.black)->getId();
        vector<MapTile> viewTiles[VIEWPORT_W][VIEWPORT_H];
        uint8_t* blocked = scr->blockingGrid;
        bool focus;
        int focusX = -1, focusY = -1;

        for (int y = 0; y < VIEWPORT_H; y++) {
            for (int x = 0; x < VIEWPORT_W; x++) {
                viewTiles[x][y] = screenViewportTile(VIEWPORT_W, VIEWPORT_H,
                                                     x, y, focus);
                *blocked++ = viewTiles[x][y].front().getTileType()->isOpaque();
                if (focus) {
                    focusX = x;
                    focusY = y;
                }
            }
        }

        if (c->location->map->flags & NO_LINE_OF_SIGHT) {
            memset(scr->screenLos, 1, VIEWPORT_W * VIEWPORT_H);
        } else {
            memset(scr->screenLos, 0, VIEWPORT_W * VIEWPORT_H);
            screenFindLineOfSight(scr->blockingGrid, scr->screenLos);
        }

        const uint8_t* lineOfSight = scr->screenLos;
        for (int y = 0; y < VIEWPORT_H; y++) {
            for (int x = 0; x < VIEWPORT_W; x++) {
                if (*lineOfSight++)
                    view->drawTile(viewTiles[x][y], x, y);
                else
                    view->drawTile(black, x, y);
            }
        }

        if (focusX >= 0)
            view->drawFocus(focusX, focusY);
    }

    if (view->highlightActive())
        view->update();

    screenUpdateMoons();
    screenUpdateWind();
}


/*-----------------------------------------------------------------------
 * Scaling (stubs — not used in text mode, handled by Allegro window)
 *-----------------------------------------------------------------------*/


