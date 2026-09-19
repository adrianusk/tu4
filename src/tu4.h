/*
 * tu4.h
 */

#include "notify.h"

enum NotifySender {
    // Sender Id           Message
    SENDER_LOCATION,    // MoveEvent*
    SENDER_PARTY,       // PartyEvent*
    SENDER_AURA,        // Aura*
    SENDER_MENU,        // MenuEvent*
    SENDER_SETTINGS     // Settings*
};

class Settings;
class Config;
class ImageMgr;
struct Screen;
class Image;
class EventHandler;
struct SaveGame;
class IntroController;
class GameController;

enum TU4GameStage {
    StageExitGame,
    StageIntro,
    StagePlay
};

struct TU4GameServices {
    NotifyBus notifyBus;
    Settings* settings;
    Config* config;
    ImageMgr* imageMgr;
    Screen* screen;
    void* screenSys;
    void* gpu;
    Image* screenImage;
    EventHandler* eventHandler;
    SaveGame* saveGame;
    IntroController* intro;
    GameController* game;
    const char* errorMessage;
    int stage;
};

extern TU4GameServices tu4;

#define gs_listen(msk,func,user)    notify_listen(&tu4.notifyBus,msk,func,user)
#define gs_unplug(id)               notify_unplug(&tu4.notifyBus,id)
#define gs_emitMessage(sid,data)    notify_emit(&tu4.notifyBus,sid,data);
