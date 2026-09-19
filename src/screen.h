/**
 * screen.h
 * @brief Declares interfaces for working with the screen
 *
 * This file declares interfaces for manipulating areas of the screen, or the
 * entire screen.  Functions like drawing images, tiles, and other items are
 * declared, as well as functions to draw pixels and rectangles and to update
 * areas of the screen.
 *
 * Most of the functions here are obsolete and are slowly being
 * migrated to the xxxView classes.
 *
 * @todo
 *  <ul>
 *      <li>migrate rest of text output logic to TextView</li>
 *      <li>migrate rest of dungeon drawing logic to DungeonView</li>
 *  </ul>
 */

#ifndef SCREEN_H
#define SCREEN_H

/*
 * Text-mode screen dimensions, in character cells.
 *
 * These are the single source of truth for the size of the text screen
 * buffer. Historically the literals 80 and 50 were scattered throughout
 * the rendering code; they play two DISTINCT roles that happen to share
 * the same value today:
 *
 *   1. SCREEN_COLS / SCREEN_ROWS  -- the destination text-screen size
 *      (the textBuffer bounds and clip limits).
 *
 *   2. ASP_SCREEN_COLS / ASP_SCREEN_ROWS -- the row stride / dimensions of
 *      a *full-screen* ASP background blob (TITLE.ASP, START.ASP, etc.),
 *      which are authored to exactly fill the screen. This is a property
 *      of the source asset, not the destination buffer.
 *
 * They are kept as separate names (even though equal now) so a future
 * variable-screen-size change only has to touch one role at a time.
 */
#define SCREEN_COLS  80
#define SCREEN_ROWS  50

/* Dimensions (and row stride) of a full-screen ASP background asset. */
#define ASP_SCREEN_COLS  SCREEN_COLS
#define ASP_SCREEN_ROWS  SCREEN_ROWS
/* Byte size of a full-screen ASP payload: cols * rows * 2 (char + attr). */
#define ASP_SCREEN_SIZE  (ASP_SCREEN_COLS * ASP_SCREEN_ROWS * 2)

#include <vector>
#include <string>

#include "direction.h"
#include "types.h"
#include "u4file.h"

class Image;
class Map;
class Tile;
class TileView;
class Coords;

#if __GNUC__
#define PRINTF_LIKE(x,y)  __attribute__ ((format (printf, (x), (y))))
#else
#define PRINTF_LIKE(x,y)
#endif

enum ScreenFilter {
    ScreenFilter_point,
    ScreenFilter_2xBi,
    ScreenFilter_2xSaI,
    ScreenFilter_Scale2x
};

enum LayoutType {
    LAYOUT_STANDARD,
    LAYOUT_GEM,
    LAYOUT_DUNGEONGEM
};

struct Layout {
    StringId name;
    LayoutType type;
    struct {
        int16_t width, height;
    } tileshape;
    struct {
        int16_t x, y;
        int16_t width, height;
    } viewport;
};

typedef enum {
    MC_DEFAULT,
    MC_WEST,
    MC_NORTH,
    MC_EAST,
    MC_SOUTH
} MouseCursor;

typedef struct _MouseArea {
    int npoints;
    struct {
        int x, y;
    } point[4];
    MouseCursor cursor;
    int command[3];
} MouseArea;

class TileAnimSet;

// Expose a few Screen members via this struct.
struct ScreenState {
    const TileAnimSet* tileanims;
    int currentCycle;
    int vertOffset;
    bool formatIsABGR;
};

#define SCR_CYCLE_PER_SECOND 4

void screenInit(void);
void screenRefreshTimerInit(void);
void screenDelete(void);
void screenReInit(void);
void screenSwapBuffers();
void screenWait(int numberOfAnimationFrames);
#define screenUploadToGPU()

void screenIconify(void);

const std::vector<std::string> &screenGetGemLayoutNames();
const char** screenGetFilterNames();
const char** screenGetLineOfSightStyles();

void screenDrawImageInMapArea(Symbol bkgd);

void screenCycle(void);
void screenEraseMapArea(void);
void screenEraseTextArea(int x, int y, int width, int height);
void screenGemUpdate(void);

void screenCrLf();
void screenMessage(const char *fmt, ...) PRINTF_LIKE(1, 2);
void screenMessageN(const char* buffer, int buflen);
void screenPrompt(void);
void screenRedrawMapArea(void);
void screenShake(int iterations);
void screenShowChar(int chr, int x, int y);
void screenShowCharMasked(int chr, int x, int y, unsigned char mask);
void screenTextAt(int x, int y, const char *fmt, ...) PRINTF_LIKE(3, 4);
void screenTextColor(int color);
bool screenTileUpdate(TileView *view, const Coords &coords);
void screenUpdate(TileView *view, bool showmap, bool blackout);
void screenUpdateCursor(void);
void screenUpdateMoons(void);
void screenUpdateWind(void);
std::vector<MapTile> screenViewportTile(unsigned int width, unsigned int height, int x, int y, bool &focus);

void screenShowCursor(void);
void screenHideCursor(void);
void screenEnableCursor(void);
void screenDisableCursor(void);
void screenSetCursorPos(int x, int y);

bool screenToggle3DDungeonView();
void screenMakeDungeonView();
void screenDetectDungeonTraps();

void screenSetMouseCursor(MouseCursor cursor);
void screenShowMouseCursor(bool visible);
void screenPointToMouseArea(int* x, int* y);
int  pointInMouseArea(int x, int y, MouseArea *area);


ScreenState* screenState();

/* Text-mode buffer API */
void screenPutChar(int x, int y, uint8_t ch, uint8_t attr);
void screenGetChar(int x, int y, uint8_t *ch, uint8_t *attr);
const uint8_t* screenGetBuffer();

#define SCR_CYCLE_MAX 16

#endif
