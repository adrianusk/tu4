/*
 * event_sdl2.cpp - SDL2 input event backend
 *
 * Handles keyboard, mouse, and window events via SDL_PollEvent.
 * Maps SDL2 keycodes to xu4 U4_ key constants.
 */

#include <SDL2/SDL.h>
#include "u4.h"
#include "event.h"
#include "context.h"
#include "screen.h"
#include "settings.h"
#include "tu4.h"

extern bool verbose;

extern void screenDrawMouseCursor(int charX, int charY, MouseCursor cursor);

static void handleMouseMotionEvent(const SDL_Event &event) {
    if (!tu4.settings->mouseOptions.enabled)
        return;

    MouseArea *area;
    area = tu4.eventHandler->mouseAreaForPoint(event.motion.x, event.motion.y);
    MouseCursor cursor = area ? area->cursor : MC_DEFAULT;
    screenSetMouseCursor(cursor);

    /* Convert to character coordinates (SDL logical 640x400, /8 = chars) */
    int charX = event.motion.x / 8;
    int charY = event.motion.y / 8;

    /* Map viewport is at chars (2,2) to (45,45) — only active in game mode */
    bool inViewport = (tu4.stage == StagePlay) &&
                      (charX >= 2 && charX <= 45 && charY >= 2 && charY <= 45);

    if (inViewport) {
        /* Hide OS cursor, show text cursor */
        SDL_ShowCursor(SDL_DISABLE);
        screenDrawMouseCursor(charX, charY, area ? cursor : MC_DEFAULT);
    } else {
        /* Show OS cursor, hide text cursor */
        screenDrawMouseCursor(-1, -1, MC_DEFAULT);
        SDL_ShowCursor(SDL_ENABLE);
    }
}

static void handleMouseButtonDownEvent(const SDL_Event &event,
                                       Controller *controller,
                                       updateScreenCallback updateScreen) {
    if (!tu4.settings->mouseOptions.enabled)
        return;

    MouseArea* area;
    area = tu4.eventHandler->mouseAreaForPoint(event.button.x, event.button.y);
    if (area) {
        int tu4Button;
        switch (event.button.button) {
            case SDL_BUTTON_LEFT:   tu4Button = 0; break;
            case SDL_BUTTON_MIDDLE: tu4Button = 1; break;
            case SDL_BUTTON_RIGHT:  tu4Button = 2; break;
            default:                tu4Button = 0; break;
        }

        int keyCmd = area->command[tu4Button];
        if (keyCmd) {
            controller->keyPressed(keyCmd);
            if (updateScreen)
                (*updateScreen)();
        }
    }
}

static void handleKeyDownEvent(const SDL_Event &event,
                               Controller *controller,
                               updateScreenCallback updateScreen) {
    int key;
    SDL_Keycode sym = event.key.keysym.sym;
    Uint16 mod = event.key.keysym.mod;

    /* Map SDL2 keycodes to xu4 key values */
    switch (sym) {
        case SDLK_UP:        key = U4_UP;        break;
        case SDLK_DOWN:      key = U4_DOWN;      break;
        case SDLK_LEFT:      key = U4_LEFT;      break;
        case SDLK_RIGHT:     key = U4_RIGHT;     break;
        case SDLK_BACKSPACE: key = U4_BACKSPACE;  break;
        case SDLK_DELETE:    key = U4_BACKSPACE;  break;
        case SDLK_TAB:       key = U4_TAB;       break;
        case SDLK_SPACE:     key = U4_SPACE;     break;
        case SDLK_ESCAPE:    key = U4_ESC;       break;
        case SDLK_RETURN:    key = U4_ENTER;     break;
        case SDLK_KP_ENTER:  key = U4_ENTER;     break;

        case SDLK_F1:  case SDLK_F2:  case SDLK_F3:  case SDLK_F4:
        case SDLK_F5:  case SDLK_F6:  case SDLK_F7:  case SDLK_F8:
        case SDLK_F9:  case SDLK_F10: case SDLK_F11: case SDLK_F12:
            key = U4_FKEY + (sym - SDLK_F1);
            break;

        default:
            /* For printable ASCII, use the character directly */
            if (sym >= SDLK_a && sym <= SDLK_z) {
                key = sym;
                /* Apply shift for uppercase */
                if (mod & KMOD_SHIFT)
                    key = sym - 32;  /* 'a' - 32 = 'A' */
            } else if (sym >= SDLK_0 && sym <= SDLK_9) {
                key = sym;
            } else if (sym >= 32 && sym < 127) {
                key = sym;
            } else {
                return;  /* Ignore unmapped keys */
            }
            break;
    }

    /* Apply modifiers */
    if (mod & KMOD_ALT)
        key += U4_ALT;
    if (mod & KMOD_GUI)  /* Meta/Super/Cmd key */
        key += U4_META;

    /* Ctrl+key: xu4 expects the ctrl character (1-26) */
    if ((mod & KMOD_CTRL) && key >= 'a' && key <= 'z')
        key = key - 'a' + 1;
    if ((mod & KMOD_CTRL) && key >= 'A' && key <= 'Z')
        key = key - 'A' + 1;

#ifdef DEBUG
    tu4.eventHandler->recordKey(key);
#endif

    if (verbose)
        printf("key event: sym=%d mod=0x%x -> key=%d\n", sym, mod, key);

    /* Handle the keypress */
    if (controller->notifyKeyPressed(key)) {
        if (updateScreen) {
            (*updateScreen)();
            screenSwapBuffers();
        }
    }
}

/**
 * Process all pending SDL2 events.
 */
void EventHandler::handleInputEvents(Controller* waitCon,
                                     updateScreenCallback update) {
    SDL_Event event;
    Controller* controller = waitCon;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_KEYDOWN:
            if (!waitCon) controller = getController();
            handleKeyDownEvent(event, controller, update);
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (!waitCon) controller = getController();
            handleMouseButtonDownEvent(event, controller, update);
            break;

        case SDL_MOUSEMOTION:
            handleMouseMotionEvent(event);
            break;

        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
                event.window.event == SDL_WINDOWEVENT_RESTORED) {
                if (update)
                    (*update)();
            }
            break;

        case SDL_QUIT:
            quitGame();
            break;

        default:
            break;
        }
    }
}

/**
 * Set key repeat rate.
 * SDL2 uses OS-level key repeat, so this is a no-op.
 * The EventHandler timer handles its own repeat logic.
 */
int EventHandler::setKeyRepeat(int delay, int interval) {
    (void)delay;
    (void)interval;
    return 0;
}
