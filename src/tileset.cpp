/*
 * tileset.cpp
 */

#include <cstring>
#include "tileset.h"

#include "error.h"
#include "imagemgr.h"
#include "tileanim.h"
#include "tu4.h"

/**
 * Loads all tileset images.
 */
void Tileset::loadImages() {
    Tileset* ts = (Tileset*) tu4.config->tileset();
    if (ts) {
        Tile* it  = ts->tiles;
        Tile* end = it + ts->tileCount;
        for (; it != end; ++it)
            it->loadImage();
    }
}

/**
 * Delete all tileset images.
 */
void Tileset::unloadImages() {
    Tileset* ts = (Tileset*) tu4.config->tileset();
    if (ts) {
        Tile* it  = ts->tiles;
        Tile* end = it + ts->tileCount;
        for (; it != end; ++it)
            it->deleteImage();
    }
}

/**
 * Returns the tile that has the given name from any tileset, if there is one
 */
const Tile* Tileset::findTileByName(Symbol name) {
    return tu4.config->tileset()->getByName(name);
}

const Tile* Tileset::findTileById(TileId id) {
    return tu4.config->tileset()->get(id);
}

Tileset::Tileset(int count) : tileCount(0) {
    tiles  = new Tile[count];
    render = new TileRenderData[count];
    memset(tiles, 0, sizeof(Tile) * count);
}

Tileset::~Tileset() {
    delete[] tiles;
    delete[] render;
}

/**
 * Returns the tile with the given id in the tileset
 */
const Tile* Tileset::get(TileId id) const {
    if (id < tileCount)
        return tiles + id;
    return NULL;
}

/**
 * Returns the tile with the given name from the tileset, if it exists
 */
const Tile* Tileset::getByName(Symbol name) const {
    TileNameMap::const_iterator it = nameMap.find(name);
    if (it != nameMap.end())
        return it->second;
    return NULL;
}
