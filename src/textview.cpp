/*
 * textview.cpp - Text-mode text view
 * Writes characters directly into the 80x50 text buffer.
 * No pixel blitting, no charset image — just char+attr writes.
 */

#include <stdarg.h>
#include <cstring>

#include "debug.h"
#include "event.h"
#include "imagemgr.h"
#include "settings.h"
#include "textview.h"
#include "screen.h"
#include "tu4.h"

Image *TextView::charset = NULL;  /* unused in text mode, kept for linkage */

TextView::TextView(int x, int y, int columns, int rows)
    : View(x, y, columns, rows) {
    this->columns = columns;
    this->rows = rows;
    cursorEnabled = false;
    cursorFollowsText = false;
    cursorX = 0;
    cursorY = 0;
    cursorPhase = 0;
    colorFG = 0x0F;  /* white */
    colorBG = 0x00;  /* black */
    cursorSavedChar = ' ';
    cursorSavedAttr = 0x00;

    tu4.eventHandler->getTimer()->add(&cursorTimer, 4, this);
}

TextView::~TextView() {
    tu4.eventHandler->getTimer()->remove(&cursorTimer, this);
}

void TextView::reinit() {
    View::reinit();
}

/**
 * Draw a character at position (x,y) within this view.
 */
void TextView::drawChar(int chr, int x, int y) {
    ASSERT(x < columns, "x value of %d out of range", x);
    ASSERT(y < rows, "y value of %d out of range", y);

    uint8_t ch, attr;

    if (chr < 32) {
        /* Special symbol: look up in CHARSET.ASP for char+attr */
        ImageInfo* charsetInfo = tu4.imageMgr->get(BKGD_CHARSET);
        if (charsetInfo && charsetInfo->image && charsetInfo->image->getAspData()) {
            const uint8_t* data = charsetInfo->image->getAspData();
            ch = data[chr * 2];
            attr = data[chr * 2 + 1];
        } else {
            /* Fallback: use CP437 directly with current colors */
            ch = (uint8_t)chr;
            attr = (colorBG << 4) | (colorFG & 0x0F);
        }
    } else {
        /* Normal printable character: use current FG/BG colors */
        ch = (uint8_t)chr;
        attr = (colorBG << 4) | (colorFG & 0x0F);
    }

    screenPutChar(this->x + x, this->y + y, ch, attr);
}

/**
 * Draw a character with horizontal line mask.
 * Used for the avatar symbol in stats where lines are masked per virtue.
 * In text mode, we just draw the character (masking not meaningful for chars).
 */
void TextView::drawCharMasked(int chr, int x, int y, unsigned char mask) {
    /* In text mode, masking individual scan lines doesn't apply.
     * Just draw the character. If all bits masked, draw space. */
    if (mask == 0xFF)
        drawChar(' ', x, y);
    else
        drawChar(chr, x, y);
}

/* highlight the selected row using a background color */
void TextView::textSelectedAt(int x, int y, const char *text) {
    if (tu4.settings->enhancements &&
        tu4.settings->enhancementsOptions.textColorization) {
        uint8_t saveBG = colorBG;
        colorBG = 0x01;  /* blue background for selection */
        for (int i = 0; i < columns - 1; i++)
            drawChar(' ', x - 1 + i, y);
        textAt(x, y, "%s", text);
        colorBG = saveBG;
    } else {
        textAt(x, y, "%s", text);
    }
}

/* depending on the status type, apply colorization to the character */
string TextView::colorizeStatus(char statustype) {
    string output;

    if (!tu4.settings->enhancements ||
        !tu4.settings->enhancementsOptions.textColorization) {
        output = statustype;
        return output;
    }

    switch (statustype) {
        case 'P':  output = FG_GREEN;    break;
        case 'S':  output = FG_PURPLE;   break;
        case 'D':  output = FG_RED;      break;
        default:   output = statustype;  return output;
    }
    output += statustype;
    output += FG_WHITE;
    return output;
}

string TextView::colorizeString(string input, TextColor color, unsigned int colorstart, unsigned int colorlength) {
    if (!tu4.settings->enhancements ||
        !tu4.settings->enhancementsOptions.textColorization)
        return input;

    string output = "";
    string::size_type length = input.length();
    string::size_type i;
    bool colorization = false;

    for (i = 0; i < length; i++) {
        if (i == colorstart) {
            output += color;
            colorization = true;
        }
        output += input[i];
        if (colorization) {
            colorlength--;
            if (colorlength == 0) {
                output += FG_WHITE;
                colorization = false;
            }
        }
    }

    if (colorization)
        output += FG_WHITE;

    return output;
}

/**
 * Map xu4 color escape codes to EGA attribute fg color values.
 */
static uint8_t mapColorCode(char code) {
    switch (code) {
        case FG_GREY:   return 0x07;
        case FG_BLUE:   return 0x09;
        case FG_PURPLE: return 0x0D;
        case FG_GREEN:  return 0x0A;
        case FG_RED:    return 0x0C;
        case FG_YELLOW: return 0x0E;
        case FG_WHITE:  return 0x0F;
        default:        return 0x0F;
    }
}

void TextView::textAt(int x, int y, const char *fmt, ...) {
    char buffer[1024];
    unsigned int i;
    unsigned int offset = 0;

    bool reenableCursor = false;
    if (cursorFollowsText && cursorEnabled) {
        disableCursor();
        reenableCursor = true;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    for (i = 0; i < strlen(buffer); i++) {
        switch (buffer[i]) {
            case FG_GREY:
            case FG_BLUE:
            case FG_PURPLE:
            case FG_GREEN:
            case FG_RED:
            case FG_YELLOW:
            case FG_WHITE:
                colorFG = mapColorCode(buffer[i]);
                offset++;
                break;
            default:
                drawChar(buffer[i], x + (i - offset), y);
        }
    }

    if (cursorFollowsText)
        setCursorPos(x + i, y, true);
    if (reenableCursor)
        enableCursor();
}

void TextView::scroll() {
    /* Scroll the view contents up by two rows */
    for (int row = 0; row < rows - 2; row++) {
        for (int col = 0; col < columns; col++) {
            uint8_t ch, attr;
            screenGetChar(x + col, y + row + 2, &ch, &attr);
            screenPutChar(x + col, y + row, ch, attr);
        }
    }
    /* Clear the bottom two rows */
    for (int row = rows - 2; row < rows; row++) {
        for (int col = 0; col < columns; col++) {
            screenPutChar(x + col, y + row, ' ', 0x00);
        }
    }

    update();
}

void TextView::setCursorPos(int x, int y, bool clearOld) {
    while (x >= columns) {
        x -= columns;
        y++;
    }
    ASSERT(y < rows, "y value of %d out of range", y);

    if (clearOld && cursorEnabled) {
        /* Erase old cursor by drawing a space (matching xu4 behavior) */
        screenPutChar(this->x + cursorX, this->y + cursorY, ' ', 0x00);
    }

    cursorX = x;
    cursorY = y;

    drawCursor();
}

void TextView::enableCursor() {
    cursorEnabled = true;
    drawCursor();
}

void TextView::disableCursor() {
    cursorEnabled = false;
    screenPutChar(this->x + cursorX, this->y + cursorY, ' ', 0x00);
}

void TextView::drawCursor() {
    ASSERT(cursorPhase >= 0 && cursorPhase < 4, "invalid cursor phase: %d", cursorPhase);

    if (!cursorEnabled)
        return;

    /* Save character under cursor */
    screenGetChar(this->x + cursorX, this->y + cursorY, &cursorSavedChar, &cursorSavedAttr);

    /* Cursor glyphs come from the charset (chars 28-31), matching xu4 */
    drawChar(31 - cursorPhase, cursorX, cursorY);
}

void TextView::cursorTimer(void *data) {
    TextView *thiz = static_cast<TextView *>(data);
    thiz->cursorPhase = (thiz->cursorPhase + 1) % 4;
    thiz->drawCursor();
}
