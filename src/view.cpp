/*
 * view.cpp - Text-mode view base class
 * Coordinates are in character cells (1-based in config, 0-based internally).
 */

#include "view.h"
#include "screen.h"
#include "xu4.h"

View::View(int x, int y, int width, int height) :
    x(x), y(y), width(width), height(height),
    highlightX(0), highlightY(0), highlightW(0), highlightH(0),
    highlighted(false)
{
}

/**
 * Hook for reinitializing when graphics reloaded.
 */
void View::reinit() {
}

/**
 * Clear the view to black (space with attribute 0x00).
 */
void View::clear() {
    unhighlight();
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            screenPutChar(x + col, y + row, ' ', 0x00);
        }
    }
}

/**
 * Update the view to the screen.
 */
void View::update() {
    if (highlighted)
        drawHighlighted();
}

/**
 * Update a piece of the view to the screen.
 */
void View::update(int x, int y, int width, int height) {
    if (highlighted)
        drawHighlighted();
}

/**
 * Highlight a piece of the screen by inverting foreground/background colors.
 */
void View::highlight(int x, int y, int width, int height) {
    highlighted = true;
    highlightX = x;
    highlightY = y;
    highlightW = width;
    highlightH = height;

    update(x, y, width, height);
}

void View::unhighlight() {
    highlighted = false;
    highlightX = highlightY = highlightW = highlightH = 0;
}

void View::drawHighlighted() {
    /* Invert colors in the highlighted region.
     * xu4 inverts RGB (255-R, 255-G, 255-B). In EGA 4-bit space,
     * this is equivalent to XOR 0x0F on each color nibble:
     * black↔white, blue↔yellow, red↔cyan, etc. */
    for (int row = 0; row < highlightH; row++) {
        for (int col = 0; col < highlightW; col++) {
            int sx = this->x + highlightX + col;
            int sy = this->y + highlightY + row;
            uint8_t ch, attr;
            screenGetChar(sx, sy, &ch, &attr);
            uint8_t newAttr = attr ^ 0xFF;  /* invert both fg and bg */
            screenPutChar(sx, sy, ch, newAttr);
        }
    }
}
