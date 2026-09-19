/*
 * progress_bar.cpp - Text-mode progress bar
 * Renders as [=====>         ] using characters in the text buffer.
 */

#include "progress_bar.h"
#include "screen.h"
#include "tu4.h"

ProgressBar::ProgressBar(int x, int y, int width, int height, int _min, int _max) :
    View(x, y, width, 1),
    min(_min),
    max(_max) {
    current = min;
    /* height is ignored in text mode; bar is always 1 row */
    bwidth = 0;
    color = {0x09, 0, 0, 255};   /* bright blue fg */
    bcolor = {0x07, 0, 0, 255};  /* light gray fg */
}

ProgressBar& ProgressBar::operator++()  { current++; draw(); return *this; }
ProgressBar& ProgressBar::operator--()  { current--; draw(); return *this; }

void ProgressBar::draw() {
    int barWidth = width - 2;  /* minus [ and ] */
    int pos = 0;

    if (max > min)
        pos = (int)((double)(current - min) / (double)(max - min) * barWidth);
    if (pos > barWidth)
        pos = barWidth;

    /* Attribute: bright blue on black for filled, dark gray on black for empty */
    uint8_t filledAttr = 0x09;  /* bright blue fg, black bg */
    uint8_t emptyAttr  = 0x08;  /* dark gray fg, black bg */
    uint8_t frameAttr  = 0x07;  /* light gray fg, black bg */

    /* Draw frame brackets */
    screenPutChar(x, y, '[', frameAttr);
    screenPutChar(x + width - 1, y, ']', frameAttr);

    /* Draw filled portion */
    for (int i = 0; i < pos; i++) {
        screenPutChar(x + 1 + i, y, 0xDB, filledAttr);  /* █ full block */
    }

    /* Draw empty portion */
    for (int i = pos; i < barWidth; i++) {
        screenPutChar(x + 1 + i, y, 0xB0, emptyAttr);   /* ░ light shade */
    }

    screenSwapBuffers();
}

void ProgressBar::setBorderColor(int r, int g, int b, int a) {
    bcolor.r = r; bcolor.g = g; bcolor.b = b; bcolor.a = a;
}

void ProgressBar::setBorderWidth(unsigned int width) {
    bwidth = width;
}

void ProgressBar::setColor(int r, int g, int b, int a) {
    color.r = r; color.g = g; color.b = b; color.a = a;
}
