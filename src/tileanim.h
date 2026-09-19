/*
 * tileanim.h
 */

#ifndef TILEANIM_H
#define TILEANIM_H

#include <map>
#include <vector>

#include "direction.h"

class Image;
class Tile;
struct RGBA;

/* Global flag: when true, tile drawing skips space+0x00 cells (transparency) */
extern bool g_tileTransparent;

enum TileAnimType {
    ATYPE_INVERT,
    // Turn part of a tile upside down. Used for flags on buildings and ships.

    ATYPE_SCROLL,
    // Scroll the tile's contents vertically within the tile's boundaries.

    ATYPE_FRAME,
    // A transformation that advances the tile's frame by 1.

#if 0
    ATYPE_PIXEL,
    // Change single pixels to a random color selected from a list.
    // Used for animating the campfire in EGA mode.
#endif

    ATYPE_PIXEL_COLOR,
    // Changes pixels with colors that fall in a given range to another color.
    // Used to animate the campfire in VGA mode.

    ATYPE_CHAR_ALT,
    // Alternate between two characters at a given position in a text-mode tile.
    // Used for flag animations in text mode.

    ATYPE_COLOR_ALT,
    // Alternate the color attribute at a given position in a text-mode tile.
    // Used for campfire flickering.

    ATYPE_COUNT
};

enum TileAnimContext {
    ACON_NONE,
    ACON_FRAME,     // Only animate if MapTile::frame matches.
    ACON_DIR        // Only animate if object facing direction matches.
};

/**
 * Properties for tile animation transformations.
 */
struct TileAnimTransform {
    uint8_t animType;       // TileAnimType
    uint8_t random;
    uint8_t context;        // TileAnimContext
    uint8_t contextSelect;
    union {
        struct {
            int16_t x, y, w, h;
        } invert;
        struct {
            int16_t increment, current, lastOffset;
            uint16_t vid;   // Source tile VisualId for scrolled pixels
        } scroll;
        struct {
            int16_t current;
        } frame;
#if 0
        struct {
            int16_t x, y;
            RGBA color[?];
        } pixel;
#endif
        struct {
            int16_t x, y, w, h;
            RGBA start;
            RGBA end;
        } pcolor;
        struct {
            int16_t x, y;
            uint8_t altchar;
            uint8_t current;    // 0 = original tile char, 1 = altchar
        } charAlt;
        struct {
            int16_t x, y;
            uint8_t altcolor;   // alternate color attribute
            uint8_t current;    // 0 = original color, 1 = altcolor
        } colorAlt;
    } var;

    void init(int type) {
        animType = type;
        random = context = contextSelect = 0;
    }
    void draw(Image* dest, const Tile* tile, const MapTile& mapTile);
    void draw(int screenX, int screenY, const Tile* tile, const MapTile& mapTile);
};

/**
 * Instructions for animating a tile.  Each tile animation is made up
 * of a list of transformations which are applied to the tile after it
 * is drawn.
 */
class TileAnim {
public:
    TileAnim() : random(0) {}
    ~TileAnim();

    void draw(Image *dest, const Tile *tile, const MapTile &mapTile, Direction dir);
    void draw(int screenX, int screenY, const Tile *tile, const MapTile &mapTile, Direction dir);

    std::vector<TileAnimTransform *> transforms;
    Symbol name;
    int16_t random;     /* Non-zero if the animation occurs randomly */
};

/**
 * A set of tile animations.  Tile animations are associated with a
 * specific image set which shares the same name.
 */
class TileAnimSet {
public:
    typedef std::map<Symbol, TileAnim *> TileAnimMap;

    ~TileAnimSet();
    TileAnim* getByName(Symbol name) const;

    Symbol name;
    TileAnimMap tileanims;
};

#endif
