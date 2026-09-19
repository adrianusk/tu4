/*
 * event_sdl3.cpp - SDL3 input event backend
 *
 * Handles keyboard, mouse, and window events via SDL_PollEvent.
 * Maps SDL3 keycodes to xu4 U4_ key constants.
 */

#include <SDL3/SDL.h>
#include "u4.h"
#include "event.h"
#include "context.h"
#include "screen.h"
#include "settings.h"
#include "tu4.h"

extern bool verbose;

static void handleMouseMotionEvent(const SDL_Event &event) {
    if (!tu4.settings->mouseOptions.enabled)
        return;

    int x = (int)event.motion.x;
    int y = (int)event.motion.y;
    MouseArea *area;
    area = tu4.eventHandler->mouseAreaForPoint(x, y);
    screenSetMouseCursor(area ? area->cursor : MC_DEFAULT);
}

static void handleMouseButtonDownEvent(const SDL_Event &event,
                                       Controller *controller,
                                       updateScreenCallback updateScreen) {
    if (!tu4.settings->mouseOptions.enabled)
        return;

    int x = (int)event.button.x;
    int y = (int)event.button.y;
    MouseArea* area;
    area = tu4.eventHandler->mouseAreaForPoint(x, y);
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
    SDL_Keycode sym = event.key.key;
    SDL_Keymod mod = event.key.mod;

    /* Map SDL3 keycodes to xu4 key values */
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
            /* SDL3 uses uppercase SDLK_A - SDLK_Z */
            if (sym >= SDLK_A && sym <= SDLK_Z) {
                /* Convert to lowercase ASCII for xu4 */
                key = sym - SDLK_A + 'a';
                /* Apply shift for uppercase */
                if (mod & SDL_KMOD_SHIFT)
                    key = sym - SDLK_A + 'A';
            } else if (sym >= SDLK_0 && sym <= SDLK_9) {
                key = sym - SDLK_0 + '0';
            } else if (sym >= 32 && sym < 127) {
                key = sym;
            } else {
                return;  /* Ignore unmapped keys */
            }
            break;
    }

    /* Apply modifiers */
    if (mod & SDL_KMOD_ALT)
        key += U4_ALT;
    if (mod & SDL_KMOD_GUI)  /* Meta/Super/Cmd key */
        key += U4_META;

    /* Ctrl+key: xu4 expects the ctrl character (1-26) */
    if ((mod & SDL_KMOD_CTRL) && key >= 'a' && key <= 'z')
        key = key - 'a' + 1;
    if ((mod & SDL_KMOD_CTRL) && key >= 'A' && key <= 'Z')
        key = key - 'A' + 1;

#ifdef DEBUG
    tu4.eventHandler->recordKey(key);
#endif

    if (verbose)
        printf("key event: sym=%d mod=0x%x -> key=%d\n", (int)sym, (int)mod, key);

    /* Handle the keypress */
    if (controller->notifyKeyPressed(key)) {
        if (updateScreen)
            (*updateScreen)();
    }
}

/**
 * Process all pending SDL3 events.
 */
void EventHandler::handleInputEvents(Controller* waitCon,
                                     updateScreenCallback update) {
    SDL_Event event;
    Controller* controller = waitCon;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            if (!waitCon) controller = getController();
            handleKeyDownEvent(event, controller, update);
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (!waitCon) controller = getController();
            handleMouseButtonDownEvent(event, controller, update);
            break;

        case SDL_EVENT_MOUSE_MOTION:
            handleMouseMotionEvent(event);
            break;

        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_RESTORED:
            if (update)
                (*update)();
            break;

        case SDL_EVENT_QUIT:
            quitGame();
            break;

        default:
            break;
        }
    }
}

/**
 * Set key repeat rate.
 * SDL3 uses OS-level key repeat, so this is a no-op.
 */
int EventHandler::setKeyRepeat(int delay, int interval) {
    (void)delay;
    (void)interval;
    return 0;
}
