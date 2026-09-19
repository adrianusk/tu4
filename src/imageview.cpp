/*
 * imageview.cpp - Text-mode image view
 * Displays ASP screen data (80x50 char+attr) or subregions of it
 * into the text buffer.
 */

#include "error.h"
#include "imagemgr.h"
#include "imageview.h"
#include "screen.h"
#include "tu4.h"

ImageView::ImageView(int x, int y, int width, int height)
    : View(x, y, width, height) {
}

ImageView::~ImageView() {
}

/**
 * Draw a subimage from an ASP screen onto the view at optional offset.
 * The subimage x,y,width,height define the source region within the
 * 80x50 ASP data. It is drawn at (this->x + ox, this->y + oy).
 */
void ImageView::draw(const ImageInfo* info, int sub, int ox, int oy) {
    if (!info || !info->image)
        return;

    const SubImage* subimage = info->subImages + sub;
    const uint8_t* aspData = info->image->getAspData();
    if (!aspData)
        return;

    /* Subimage x,y are 1-based in XML; convert to 0-based for buffer access */
    int srcX = subimage->x - 1;
    int srcY = subimage->y - 1;

    /* Copy subimage region from ASP data to screen buffer */
    for (int row = 0; row < subimage->height; row++) {
        for (int col = 0; col < subimage->width; col++) {
            int srcIdx = ((srcY + row) * ASP_SCREEN_COLS + (srcX + col)) * 2;
            uint8_t ch   = aspData[srcIdx];
            uint8_t attr = aspData[srcIdx + 1];
            screenPutChar(x + ox + col, y + oy + row, ch, attr);
        }
    }
}

/**
 * Draw a full ASP screen or subimage by name.
 */
void ImageView::draw(Symbol imageName, int dx, int dy, int maxRows) {
    const SubImage* subimage;
    ImageInfo *info = tu4.imageMgr->imageInfo(imageName, &subimage);
    if (!info) {
        errorLoadImage(imageName);
        return;
    }

    const uint8_t* aspData = info->image->getAspData();
    if (!aspData)
        return;

    if (subimage) {
        /* Subimage x,y are 1-based in XML; convert to 0-based */
        int srcX = subimage->x - 1;
        int srcY = subimage->y - 1;

        /* Draw a subimage region */
        for (int row = 0; row < subimage->height; row++) {
            for (int col = 0; col < subimage->width; col++) {
                int srcIdx = ((srcY + row) * ASP_SCREEN_COLS + (srcX + col)) * 2;
                uint8_t ch   = aspData[srcIdx];
                uint8_t attr = aspData[srcIdx + 1];
                screenPutChar(x + dx + col, y + dy + row, ch, attr);
            }
        }
    } else {
        /* Draw full screen (capped at maxRows) */
        int rows = (maxRows < ASP_SCREEN_ROWS) ? maxRows : ASP_SCREEN_ROWS;
        for (int row = 0; row < rows; row++) {
            for (int col = 0; col < ASP_SCREEN_COLS; col++) {
                int srcIdx = (row * ASP_SCREEN_COLS + col) * 2;
                uint8_t ch   = aspData[srcIdx];
                uint8_t attr = aspData[srcIdx + 1];
                screenPutChar(x + dx + col, y + dy + row, ch, attr);
            }
        }
    }
}
