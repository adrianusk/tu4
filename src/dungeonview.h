/*
 * dungeonview.h
 */

#ifndef DUNGEONVIEW_H
#define DUNGEONVIEW_H

#include "tileview.h"

typedef enum {
    DNGGRAPHIC_NONE,
    DNGGRAPHIC_WALL,
    DNGGRAPHIC_LADDERUP,
    DNGGRAPHIC_LADDERDOWN,
    DNGGRAPHIC_LADDERUPDOWN,
    DNGGRAPHIC_DOOR,
    DNGGRAPHIC_DNGTILE,
    DNGGRAPHIC_BASETILE,
    DNGGRAPHIC_TRAP
} DungeonGraphicType;

class Context;
class Dungeon;
class ImageInfo;
class SubImage;

class DungeonView : public TileView {
public:
    struct GraphicData {
        const ImageInfo* info;
        const SubImage* sub;
    };

    DungeonView(int x, int y, int columns, int rows);

    void cacheGraphicData();
    void display(Context * c, TileView *view);
    void detectTraps();

    bool toggle3DDungeonView() {
        return screen3dDungeonViewEnabled = ! screen3dDungeonViewEnabled;
    }

private:
    void drawInDungeon(const MapTile& mt, int x_offset, int distance,
                       Direction orientation);
    int graphicIndex(const Coords& loc, int xoffset, int distance,
                     Direction orientation, DungeonGraphicType type);
    DungeonGraphicType tilesToGraphic(const Dungeon*,
                                      const std::vector<MapTile> &tiles);
    void drawWall(int graphic);

    MapTile black;
    MapTile avatar;
    TileId corridor;
    TileId up_ladder;
    TileId down_ladder;
    TileId updown_ladder;
    int      spotTrapRange;
    uint32_t spotTrapTime;
    bool screen3dDungeonViewEnabled;
    bool egaLayout;
    GraphicData graphic[84];

    /* Scaled dungeon art lookup */
    const uint8_t* dungObj0Data;     /* DUNGOBJ0.ASP raw object tile data (16x16 chars): 0=fountain,1=chest,2=orb */
    const uint8_t* dungNpc1Data;     /* DUNGNPC1.ASP raw NPC tile data (8x8 chars): NPCs 0-43 (4 frames each) */
    const uint8_t* dungObj1Data;     /* DUNGOBJ1.ASP raw object tile data (8x8 chars): 0=fountain,1=chest,2=orb */
    int dungNpcTileIndex[256];       /* Map from module TileId to base tile index (NPCs=i*4, objects=44/45/46; -1 = none) */
};

#endif /* DUNGEONVIEW_H */
