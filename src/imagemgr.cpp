/*
 * imagemgr.cpp - Text-mode image manager
 * Loads ASP files into Image objects. Manages imagesets.
 * Simplified from the pixel-based version — no fixups, no palettes.
 */

#include <cstdio>
#include <cstring>
#include "config.h"
#include "debug.h"
#include "error.h"
#include "imageloader.h"
#include "imagemgr.h"
#include "settings.h"
#include "xu4.h"

extern bool verbose;

ImageSymbols ImageMgr::sym;

ImageInfo::ImageInfo()
    : filename(0), name(0), resGroup(0), tiles(0),
      width(0), height(0), subImageCount(0),
      depth(0), prescale(0), filetype(0), fixup(0),
      image(NULL), subImages(NULL) {
}

ImageInfo::~ImageInfo() {
    delete image;
    delete[] subImages;
}

std::string ImageInfo::getFilename() const {
    return xu4.config->symbolName(filename);
}

ImageSet::~ImageSet() {
    std::map<Symbol, ImageInfo *>::iterator it;
    for (it = info.begin(); it != info.end(); ++it)
        delete it->second;
}

ImageMgr::ImageMgr()
    : baseSet(NULL), vgaColors(NULL), greyColors(NULL),
      visionBuf(NULL), logger(NULL), listenerId(-1), resGroup(0) {

    xu4.config->internSymbols(&sym.tiles, 46,
        "tiles charset borders title options_top\n"
        "options_btm tree portal outside inside\n"
        "wagon gypsy abacus honcom valjus\n"
        "sachonor spirhum beasties key honesty\n"
        "compassn valor justice sacrific honor\n"
        "spirit humility truth love courage\n"
        "stoncrcl infinity rune1 rune2 rune3\n"
        "rune4 rune5 rune6 rune7 rune8\n"
        "gemtiles moonphases moongate items blackbead whitebead");

    notice(SENDER_SETTINGS, xu4.settings, this);
    listenerId = gs_listen(1<<SENDER_SETTINGS, notice, this);
}

ImageMgr::~ImageMgr() {
    gs_unplug(listenerId);
    std::map<Symbol, ImageSet *>::iterator it;
    for (it = imageSets.begin(); it != imageSets.end(); ++it)
        delete it->second;
    delete[] vgaColors;
    delete[] greyColors;
    delete[] visionBuf;
}

/**
 * Get the ImageSet for the given scheme name.
 */
ImageSet* ImageMgr::scheme(Symbol name) {
    std::map<Symbol, ImageSet *>::iterator it = imageSets.find(name);
    if (it != imageSets.end())
        return it->second;

    // Not cached yet; load it from Config by matching scheme name.
    const char* nameStr = xu4.config->symbolName(name);
    const char** names = xu4.config->schemeNames();
    const char** nit = names;
    while (*nit) {
        if (strcmp(nameStr, *nit) == 0) {
            ImageSet* sp = xu4.config->newScheme(nit - names);
            if (!sp)
                break;
            imageSets[sp->name] = sp;
            return sp;
        }
        ++nit;
    }
    return NULL;
}

/**
 * Open the file for an image, searching in the graphics directory.
 */
U4FILE* ImageMgr::getImageFile(ImageInfo *info) {
    std::string filename = info->getFilename();
    if (filename.empty())
        return NULL;

    /* Try in graphics/ subdirectory first */
    std::string path = u4find_graphics(filename);
    if (!path.empty())
        return u4fopen(path.c_str());

    /* Try as-is */
    return u4fopen(filename.c_str());
}

/**
 * Load an image from its file.
 */
ImageInfo* ImageMgr::load(ImageInfo* info, bool returnUnscaled) {
    (void)returnUnscaled;

    if (info->image)
        return info;  /* Already loaded */

    U4FILE* file = getImageFile(info);
    if (!file) {
        if (verbose)
            printf("Warning: cannot open image file: %s\n", info->getFilename().c_str());
        return info;
    }

    /* Use tiles count for FTYPE_ASP_TILES/CHARSET, otherwise standard screen load */
    int width = (info->filetype == FTYPE_ASP_TILES || info->filetype == FTYPE_ASP_CHARSET)
                ? info->tiles : info->width;

    info->image = loadImage(file, info->filetype, width, info->height, info->depth);
    u4fclose(file);

    if (!info->image && verbose) {
        printf("Warning: failed to load image: %s\n", info->getFilename().c_str());
    }

    return info;
}

/**
 * Find an ImageInfo by name, searching the current scheme and base set.
 */
ImageInfo* ImageMgr::getInfoFromSet(Symbol name, ImageSet *set) {
    if (!set)
        return NULL;

    std::map<Symbol, ImageInfo *>::iterator it = set->info.find(name);
    if (it != set->info.end())
        return it->second;

    /* Check the set this extends */
    if (set->extends) {
        ImageSet* parent = scheme(set->extends);
        if (parent)
            return getInfoFromSet(name, parent);
    }

    return NULL;
}

/**
 * Get an ImageInfo, loading it if necessary.
 * This is the main entry point for accessing images.
 */
ImageInfo* ImageMgr::get(Symbol name, bool returnUnscaled) {
    ImageInfo* info = getInfoFromSet(name, baseSet);
    if (!info)
        return NULL;

    if (!info->image)
        load(info, returnUnscaled);

    return info;
}

/**
 * Get ImageInfo and resolve subimage by name.
 */
ImageInfo* ImageMgr::imageInfo(Symbol name, const SubImage** subPtr) {
    *subPtr = NULL;

    /* First try as a top-level image name */
    ImageInfo* info = getInfoFromSet(name, baseSet);
    if (info) {
        if (!info->image)
            load(info, false);
        return info;
    }

    /* Try as a subimage name — search all images for matching subimage */
    if (baseSet) {
        std::map<Symbol, ImageInfo *>::iterator it;
        for (it = baseSet->info.begin(); it != baseSet->info.end(); ++it) {
            ImageInfo* ii = it->second;
            std::map<Symbol, int>::iterator sub = ii->subImageIndex.find(name);
            if (sub != ii->subImageIndex.end()) {
                if (!ii->image)
                    load(ii, false);
                *subPtr = ii->subImages + sub->second;
                return ii;
            }
        }
    }

    return NULL;
}

/**
 * Set the current resource group (used to track intro vs game resources).
 */
uint16_t ImageMgr::setResourceGroup(uint16_t group) {
    uint16_t prev = resGroup;
    resGroup = group;
    return prev;
}

/**
 * Free all images in a resource group.
 */
void ImageMgr::freeResourceGroup(uint16_t group) {
    if (!baseSet)
        return;

    std::map<Symbol, ImageInfo *>::iterator it;
    for (it = baseSet->info.begin(); it != baseSet->info.end(); ++it) {
        ImageInfo* info = it->second;
        if (info->resGroup == group && info->image) {
            delete info->image;
            info->image = NULL;
        }
    }
}

/**
 * Return VGA palette (stub — not used in text mode).
 */
const RGBA* ImageMgr::vgaPalette() {
    return NULL;
}

/**
 * Return grey palette (stub — not used in text mode).
 */
const RGBA* ImageMgr::greyPalette() {
    return NULL;
}

/**
 * Handle settings change notification.
 * In xu4 this reloads the graphics scheme; in tu4 it reloads the text style.
 */
void ImageMgr::notice(int sender, void* eventData, void* user) {
    (void)sender;
    ImageMgr* mgr = (ImageMgr*)user;
    Settings* settings = (Settings*)eventData;

    std::string setname = settings->textStyle;
    Symbol sym = xu4.config->intern(setname.c_str());
    mgr->baseSet = mgr->scheme(sym);
}

/* Fixup stubs — not used in text mode */
void ImageMgr::fixupIntro(Image*, int) {}
void ImageMgr::fixupAbyssVision(void*) {}
void ImageMgr::fixupTransparent(Image*, RGBA) {}
void ImageMgr::fixupAbacus(Image*, int) {}
void ImageMgr::fixupDungNS(Image*) {}
void ImageMgr::fixupFMTowns(Image*) {}

const SubImage* ImageMgr::getSubImage(Symbol name, ImageInfo** infoPtr) {
    const SubImage* sub = NULL;
    *infoPtr = imageInfo(name, &sub);
    return sub;
}
