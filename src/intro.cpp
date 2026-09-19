/*
 * $Id$
 */


#include <algorithm>
#include <cstring>
#include "u4.h"

#include "intro.h"

#include "debug.h"
#include "error.h"
#include "imagemgr.h"
#include "sound.h"
#include "party.h"
#include "screen.h"
#include "settings.h"
#include "tileset.h"
#include "u4file.h"
#include "utils.h"
#include "tu4.h"

extern uint32_t getTicks();

using namespace std;

#define INTRO_MAP_HEIGHT 5
#define INTRO_MAP_WIDTH 19
#define INTRO_TEXT_X 20
#define INTRO_TEXT_Y 41
#define INTRO_TEXT_WIDTH 40
#define INTRO_TEXT_HEIGHT 6

// Invalidate cached title-screen art after a graphics-asset reload (defined
// alongside the title data below; used by the config-menu 'c' handler).
void titleDataLoadedReset();

#define GYP_PLACES_FIRST 0
#define GYP_PLACES_TWOMORE 1
#define GYP_PLACES_LAST 2
#define GYP_UPON_TABLE 3
#define GYP_SEGUE1 13
#define GYP_SEGUE2 14

class IntroObjectState {
public:
    IntroObjectState() : x(0), y(0), tile(0) {}
    int x, y;
    MapTile tile; /* base tile + tile frame */
};

/* temporary place-holder for settings changes */
SettingsData settingsChanged;

const int IntroBinData::INTRO_TEXT_OFFSET = 17445 - 1;  // (start at zero)
const int IntroBinData::INTRO_MAP_OFFSET = 30339;
const int IntroBinData::INTRO_FIXUPDATA_OFFSET = 29806;
const int IntroBinData::INTRO_SCRIPT_TABLE_SIZE = 548;
const int IntroBinData::INTRO_SCRIPT_TABLE_OFFSET = 30434;
const int IntroBinData::INTRO_BASETILE_TABLE_SIZE = 15;
const int IntroBinData::INTRO_BASETILE_TABLE_OFFSET = 16584;
const int IntroBinData::BEASTIE1_FRAMES = 0x80;
const int IntroBinData::BEASTIE2_FRAMES = 0x40;
const int IntroBinData::BEASTIE_FRAME_TABLE_OFFSET = 0x7380;
const int IntroBinData::BEASTIE1_FRAMES_OFFSET = 0;
const int IntroBinData::BEASTIE2_FRAMES_OFFSET = 0x78;

IntroBinData::IntroBinData() :
    sigData(NULL),
    scriptTable(NULL),
    baseTileTable(NULL),
    beastie1FrameTable(NULL),
    beastie2FrameTable(NULL) {
}

IntroBinData::~IntroBinData() {
    delete [] sigData;
    delete [] scriptTable;
    delete [] baseTileTable;
    delete [] beastie1FrameTable;
    delete [] beastie2FrameTable;

    introQuestions.clear();
    introText.clear();
    introGypsy.clear();
}

bool IntroBinData::load() {
    int i;
    const UltimaSaveIds* usaveIds = tu4.config->usaveIds();
    const Tileset* tileset = tu4.config->tileset();

    U4FILE *title = u4fopen("title.exe");
    if (!title)
        return false;

    introQuestions = u4read_stringtable(title, INTRO_TEXT_OFFSET, 28);
    introText = u4read_stringtable(title, -1, 24);
    introGypsy = u4read_stringtable(title, -1, 15);

    /* clean up stray newlines at end of strings */
    for (i = 0; i < 15; i++)
        trim(introGypsy[i]);

    if (sigData)
        delete sigData;
    sigData = new unsigned char[533];
    u4fseek(title, INTRO_FIXUPDATA_OFFSET, SEEK_SET);
    u4fread(sigData, 1, 533, title);

    u4fseek(title, INTRO_MAP_OFFSET, SEEK_SET);
    introMap.resize(INTRO_MAP_WIDTH * INTRO_MAP_HEIGHT, MapTile(0));
    for (i = 0; i < INTRO_MAP_HEIGHT * INTRO_MAP_WIDTH; i++)
        introMap[i] = usaveIds->moduleId(u4fgetc(title));

    u4fseek(title, INTRO_SCRIPT_TABLE_OFFSET, SEEK_SET);
    scriptTable = new unsigned char[INTRO_SCRIPT_TABLE_SIZE];
    u4fread(scriptTable, 1, INTRO_SCRIPT_TABLE_SIZE, title);

    u4fseek(title, INTRO_BASETILE_TABLE_OFFSET, SEEK_SET);
    baseTileTable = new const Tile*[INTRO_BASETILE_TABLE_SIZE];
    for (i = 0; i < INTRO_BASETILE_TABLE_SIZE; i++) {
        MapTile tile = usaveIds->moduleId(u4fgetc(title));
        baseTileTable[i] = tileset->get(tile.id);
    }

    /* --------------------------
       load beastie frame table 1
       -------------------------- */
    beastie1FrameTable = new unsigned char[BEASTIE1_FRAMES];
    u4fseek(title, BEASTIE_FRAME_TABLE_OFFSET + BEASTIE1_FRAMES_OFFSET, SEEK_SET);
    for (i = 0; i < BEASTIE1_FRAMES; i++) {
        beastie1FrameTable[i] = u4fgetc(title);
    }

    /* --------------------------
       load beastie frame table 2
       -------------------------- */
    beastie2FrameTable = new unsigned char[BEASTIE2_FRAMES];
    u4fseek(title, BEASTIE_FRAME_TABLE_OFFSET + BEASTIE2_FRAMES_OFFSET, SEEK_SET);
    for (i = 0; i < BEASTIE2_FRAMES; i++) {
        beastie2FrameTable[i] = u4fgetc(title);
    }

    u4fclose(title);

    return true;
}

IntroController::IntroController() :
    Controller(1),
    backgroundArea(),
    menuArea(2, 26, 76, 22),
    extendedMenuArea(22, 27, 36, 18),
    questionArea(INTRO_TEXT_X, INTRO_TEXT_Y, INTRO_TEXT_WIDTH, INTRO_TEXT_HEIGHT),
    mapArea(2, 26, INTRO_MAP_WIDTH, INTRO_MAP_HEIGHT),
    binData(NULL),
    bSkipTitles(false),
    textStyleChanged(false)
{
    // initialize menus
    confMenu.setTitle("TU4 Configuration:", 0, 0);
    confMenu.add(MI_CONF_VIDEO,               "\010 Video Options",              2,  2,/*'v'*/  2);
    confMenu.add(MI_CONF_SOUND,               "\010 Sound Options",              2,  3,/*'s'*/  2);
    confMenu.add(MI_CONF_INPUT,               "\010 Input Options",              2,  4,/*'i'*/  2);
    confMenu.add(MI_CONF_SPEED,               "\010 Speed Options",              2,  5,/*'p'*/  3);
    confMenu.add(MI_CONF_01, new BoolMenuItem("Game Enhancements         %s",    2,  7,/*'e'*/  5, &settingsChanged.enhancements));
    confMenu.add(MI_CONF_GAMEPLAY,            "\010 Enhanced Gameplay Options",  2,  9,/*'g'*/ 11);
    confMenu.add(MI_CONF_INTERFACE,           "\010 Enhanced Interface Options", 2, 10,/*'n'*/ 12);
    confMenu.add(CANCEL,                      "\017 Main Menu",                  2, 12,/*'m'*/  2);
    confMenu.addShortcutKey(CANCEL, ' ');
    confMenu.setClosesMenu(CANCEL);

    /* set the default visibility of the two enhancement menus */
    confMenu.getItemById(MI_CONF_GAMEPLAY)->setVisible(tu4.settings->enhancements);
    confMenu.getItemById(MI_CONF_INTERFACE)->setVisible(tu4.settings->enhancements);

    videoMenu.setTitle("Video Options:", 0, 0);
    videoMenu.add(MI_VIDEO_CONF_GFX,              "\010 Game Graphics Options",  2,  2,/*'g'*/  2);
    videoMenu.add(MI_VIDEO_04,    new IntMenuItem("Scale                x%d", 2,  4,/*'s'*/  0, reinterpret_cast<int *>(&settingsChanged.scale), 1, 5, 1));
    videoMenu.add(MI_VIDEO_05,  (new BoolMenuItem("Mode                 %s",  2,  5,/*'m'*/  0, &settingsChanged.fullscreen))->setValueStrings("Fullscreen", "Window"));
    videoMenu.add(USE_SETTINGS,                   "\010 Use These Settings",  2, 11,/*'u'*/  2);
    videoMenu.add(CANCEL,                         "\010 Cancel",              2, 12,/*'c'*/  2);
    videoMenu.addShortcutKey(CANCEL, ' ');
    videoMenu.setClosesMenu(USE_SETTINGS);
    videoMenu.setClosesMenu(CANCEL);

    gfxMenu.setTitle("Game Graphics Options", 0,0);
    gfxMenu.add(MI_GFX_SCHEME,                        new StringMenuItem("Text Style         %s", 2,  2,/*'t'*/ 0, &settingsChanged.textStyle, tu4.config->schemeNames()));
    gfxMenu.add(MI_GFX_TILE_TRANSPARENCY,               new BoolMenuItem("Transparency Hack  %s", 2,  4,/*'t'*/ 0, &settingsChanged.enhancementsOptions.u4TileTransparencyHack));
    gfxMenu.add(MI_VIDEO_02,                          new StringMenuItem("Gem Layout         %s", 2,  8,/*'e'*/ 1, &settingsChanged.gemLayout, screenGetGemLayoutNames()));
    gfxMenu.add(MI_VIDEO_03,                            new EnumMenuItem("Line Of Sight      %s", 2,  9,/*'l'*/ 0, &settingsChanged.lineOfSight, screenGetLineOfSightStyles()));
    gfxMenu.add(MI_VIDEO_07,                            new BoolMenuItem("Screen Shaking     %s", 2, 10,/*'k'*/ 8, &settingsChanged.screenShakes));
    gfxMenu.add(MI_GFX_RETURN,               "\010 Return to Video Options",              2,  12,/*'r'*/  2);
    gfxMenu.setClosesMenu(MI_GFX_RETURN);


    soundMenu.setTitle("Sound Options:", 0, 0);
    soundMenu.add(MI_SOUND_01,  new IntMenuItem("Music Volume         %s", 2,  2,/*'m'*/  0, &settingsChanged.musicVol, 0, MAX_VOLUME, 1, MENU_OUTPUT_VOLUME));
    soundMenu.add(MI_SOUND_02,  new IntMenuItem("Sound Effect Volume  %s", 2,  3,/*'s'*/  0, &settingsChanged.soundVol, 0, MAX_VOLUME, 1, MENU_OUTPUT_VOLUME));
    soundMenu.add(MI_SOUND_03, new BoolMenuItem("Fading               %s", 2,  4,/*'f'*/  0, &settingsChanged.volumeFades));
    soundMenu.add(USE_SETTINGS,                 "\010 Use These Settings", 2, 11,/*'u'*/  2);
    soundMenu.add(CANCEL,                       "\010 Cancel",             2, 12,/*'c'*/  2);
    soundMenu.addShortcutKey(CANCEL, ' ');
    soundMenu.setClosesMenu(USE_SETTINGS);
    soundMenu.setClosesMenu(CANCEL);

    inputMenu.setTitle("Keyboard Options:", 0, 0);
    inputMenu.add(MI_INPUT_01,  new IntMenuItem("Repeat Delay        %4d msec", 2,  2,/*'d'*/  7, &settingsChanged.keydelay, 100, MAX_KEY_DELAY, 100));
    inputMenu.add(MI_INPUT_02,  new IntMenuItem("Repeat Interval     %4d msec", 2,  3,/*'i'*/  7, &settingsChanged.keyinterval, 10, MAX_KEY_INTERVAL, 10));
    /* "Mouse Options:" is drawn in the updateInputMenu() function */
    inputMenu.add(MI_INPUT_03, new BoolMenuItem("Mouse                %s",      2,  7,/*'m'*/  0, &settingsChanged.mouseOptions.enabled));
    inputMenu.add(USE_SETTINGS,                 "\010 Use These Settings",      2, 11,/*'u'*/  2);
    inputMenu.add(CANCEL,                       "\010 Cancel",                  2, 12,/*'c'*/  2);
    inputMenu.addShortcutKey(CANCEL, ' ');
    inputMenu.setClosesMenu(USE_SETTINGS);
    inputMenu.setClosesMenu(CANCEL);

    speedMenu.setTitle("Speed Options:", 0, 0);
    speedMenu.add(MI_SPEED_01, new IntMenuItem("Game Cycles per Second    %3d",      2,  2,/*'g'*/  0, &settingsChanged.gameCyclesPerSecond, 1, MAX_CYCLES_PER_SECOND, 1));
    speedMenu.add(MI_SPEED_02, new IntMenuItem("Battle Speed              %3d",      2,  3,/*'b'*/  0, &settingsChanged.battleSpeed, 1, MAX_BATTLE_SPEED, 1));
    speedMenu.add(MI_SPEED_03, new IntMenuItem("Spell Effect Length       %s",       2,  4,/*'p'*/  1, &settingsChanged.spellEffectSpeed, 1, MAX_SPELL_EFFECT_SPEED, 1, MENU_OUTPUT_SPELL));
    speedMenu.add(MI_SPEED_04, new IntMenuItem("Camping Length            %3d sec",  2,  5,/*'m'*/  2, &settingsChanged.campTime, 1, MAX_CAMP_TIME, 1));
    speedMenu.add(MI_SPEED_05, new IntMenuItem("Inn Rest Length           %3d sec",  2,  6,/*'i'*/  0, &settingsChanged.innTime, 1, MAX_INN_TIME, 1));
    speedMenu.add(MI_SPEED_06, new IntMenuItem("Shrine Meditation Length  %3d sec",  2,  7,/*'s'*/  0, &settingsChanged.shrineTime, 1, MAX_SHRINE_TIME, 1));
    speedMenu.add(MI_SPEED_07, new IntMenuItem("Screen Shake Interval     %3d msec", 2,  8,/*'r'*/  2, &settingsChanged.shakeInterval, MIN_SHAKE_INTERVAL, MAX_SHAKE_INTERVAL, 10));
    speedMenu.add(USE_SETTINGS,                "\010 Use These Settings",            2, 11,/*'u'*/  2);
    speedMenu.add(CANCEL,                      "\010 Cancel",                        2, 12,/*'c'*/  2);
    speedMenu.addShortcutKey(CANCEL, ' ');
    speedMenu.setClosesMenu(USE_SETTINGS);
    speedMenu.setClosesMenu(CANCEL);

    /* move the BATTLE DIFFICULTY, DEBUG, and AUTOMATIC ACTIONS settings to "enhancementsOptions" */
    gameplayMenu.setTitle                              ("Enhanced Gameplay Options:", 0, 0);
    gameplayMenu.add(MI_GAMEPLAY_01,   new EnumMenuItem("Battle Difficulty          %s", 2,  2,/*'b'*/  0, &settingsChanged.battleDiff, Settings::battleDiffStrings()));
    gameplayMenu.add(MI_GAMEPLAY_02,   new BoolMenuItem("Fixed Chest Traps          %s", 2,  3,/*'t'*/ 12, &settingsChanged.enhancementsOptions.c64chestTraps));
    gameplayMenu.add(MI_GAMEPLAY_03,   new BoolMenuItem("Gazer Spawns Insects       %s", 2,  4,/*'g'*/  0, &settingsChanged.enhancementsOptions.gazerSpawnsInsects));
    gameplayMenu.add(MI_GAMEPLAY_04,   new BoolMenuItem("Gem View Shows Objects     %s", 2,  5,/*'e'*/  1, &settingsChanged.enhancementsOptions.peerShowsObjects));
    gameplayMenu.add(MI_GAMEPLAY_05,   new BoolMenuItem("Slime Divides              %s", 2,  6,/*'s'*/  0, &settingsChanged.enhancementsOptions.slimeDivides));
    gameplayMenu.add(MI_GAMEPLAY_06,   new BoolMenuItem("Debug Mode (Cheats)        %s", 2,  8,/*'d'*/  0, &settingsChanged.debug));
    gameplayMenu.add(USE_SETTINGS,                      "\010 Use These Settings",       2, 11,/*'u'*/  2);
    gameplayMenu.add(CANCEL,                            "\010 Cancel",                   2, 12,/*'c'*/  2);
    gameplayMenu.addShortcutKey(CANCEL, ' ');
    gameplayMenu.setClosesMenu(USE_SETTINGS);
    gameplayMenu.setClosesMenu(CANCEL);

    interfaceMenu.setTitle("Enhanced Interface Options:", 0, 0);
    interfaceMenu.add(MI_INTERFACE_01, new BoolMenuItem("Automatic Actions          %s", 2,  2,/*'a'*/  0, &settingsChanged.shortcutCommands));
    /* "(Open, Jimmy, etc.)" */
    interfaceMenu.add(MI_INTERFACE_02, new BoolMenuItem("Set Active Player          %s", 2,  4,/*'p'*/ 11, &settingsChanged.enhancementsOptions.activePlayer));
    interfaceMenu.add(MI_INTERFACE_03, new BoolMenuItem("Smart 'Enter' Key          %s", 2,  5,/*'e'*/  7, &settingsChanged.enhancementsOptions.smartEnterKey));
    interfaceMenu.add(MI_INTERFACE_04, new BoolMenuItem("Text Colorization          %s", 2,  6,/*'t'*/  0, &settingsChanged.enhancementsOptions.textColorization));
    interfaceMenu.add(MI_INTERFACE_05, new BoolMenuItem("Ultima V Shrines           %s", 2,  7,/*'s'*/  9, &settingsChanged.enhancementsOptions.u5shrines));
    interfaceMenu.add(MI_INTERFACE_06, new BoolMenuItem("Ultima V Spell Mixing      %s", 2,  8,/*'m'*/ 15, &settingsChanged.enhancementsOptions.u5spellMixing));
    interfaceMenu.add(USE_SETTINGS,                     "\010 Use These Settings",       2, 11,/*'u'*/  2);
    interfaceMenu.add(CANCEL,                           "\010 Cancel",                   2, 12,/*'c'*/  2);
    interfaceMenu.addShortcutKey(CANCEL, ' ');
    interfaceMenu.setClosesMenu(USE_SETTINGS);
    interfaceMenu.setClosesMenu(CANCEL);
}

IntroController::~IntroController() {
}

#define MAP_ENABLE
#define MAP_DISABLE

bool IntroController::present() {
    init();
    preloadMap();

    // Play the title animation sequence now, before this controller is
    // pushed onto the event handler's timer queue. Doing this from
    // timerFired() instead would recurse infinitely: updateTitle()'s
    // blocking wait calls tick the same TimedEventMgr queue that invokes
    // our own timerFired() callback.
    if (mode == INTRO_TITLES) {
        if (!updateTitle()) {
            mode = INTRO_MAP;
            beastiesVisible = true;
            musicPlay(introMusic);
            updateScreen();
            MAP_ENABLE;
        }
    }

    listenerId = gs_listen(1<<SENDER_MENU, introNotice, this);
    return true;
}

void IntroController::conclude() {
    gs_unplug(listenerId);
    deleteIntro();
}

/**
 * Initializes intro state and loads in introduction graphics, text
 * and map data from title.exe.
 */
bool IntroController::init() {

    justInitiatedNewGame = false;
    introMusic = MUSIC_TOWNS;

    uint16_t saveGroup = tu4.imageMgr->setResourceGroup(StageIntro);

    // sigData is referenced during Titles initialization
    binData = new IntroBinData();
    binData->load();

    Symbol sym[2];
    tu4.config->internSymbols(sym, 2, "beast0frame00 beast1frame00");
    beastiesImg = tu4.imageMgr->get(BKGD_ANIMATE);  // Assign resource group.
    if (beastiesImg) {
        beastieSub[0] = beastiesImg->subImageIndex[sym[0]];
        beastieSub[1] = beastiesImg->subImageIndex[sym[1]];
    } else {
        beastieSub[0] = 0;
        beastieSub[1] = 0;
    }

    if (tu4.errorMessage)
        bSkipTitles = true;

    if (bSkipTitles)
    {
        // the init() method is called again from within the
        // game via ALT-Q, so return to the menu
        //
#ifndef IOS
        mode = INTRO_MENU;
#else
        mode = INTRO_MAP;
#endif
        beastiesVisible = true;
        beastieOffset = 0;

        musicPlay(introMusic);
    }
    else
    {
        // initialize the titles
        initTitles();
        mode = INTRO_TITLES;
        beastiesVisible = false;
        beastieOffset = -32;
    }

    beastie1Cycle = 0;
    beastie2Cycle = 0;

    sleepCycles = 0;
    scrPos = 0;
    objectStateTable = new IntroObjectState[IntroBinData::INTRO_BASETILE_TABLE_SIZE];

    backgroundArea.reinit();
    menuArea.reinit();
    extendedMenuArea.reinit();
    questionArea.reinit();
    mapArea.reinit();

    // only update the screen if we are returning from game mode
    if (bSkipTitles)
        updateScreen();

    tu4.imageMgr->setResourceGroup(saveGroup);
    return true;
}

bool IntroController::hasInitiatedNewGame()
{
    return this->justInitiatedNewGame;
}

/**
 * Frees up data not needed after introduction.
 */
void IntroController::deleteIntro() {
    delete binData;
    binData = NULL;

    delete [] objectStateTable;
    objectStateTable = NULL;

    tu4.imageMgr->freeResourceGroup(StageIntro);
    beastiesImg = NULL;
}

unsigned char *IntroController::getSigData() {
    ASSERT(binData->sigData != NULL, "intro sig data not loaded");
    return binData->sigData;
}

/**
 * Handles keystrokes during the introduction.
 */
bool IntroController::keyPressed(int key) {
    bool valid = true;

    switch (mode) {

    case INTRO_TITLES:
        // the user pressed a key to abort the sequence
        skipTitles();
        break;

    case INTRO_MAP:
        MAP_DISABLE;
        mode = INTRO_MENU;
        updateScreen();
        break;

    case INTRO_MENU:
        switch (key) {
        case 'i':
            initiateNewGame();
            break;
        case 'j':
            journeyOnward();
            break;
        case 'r':
            mode = INTRO_MAP;
            updateScreen();
            MAP_ENABLE;
            break;
        case 'c': {
            // Make a copy of our settings so we can change them
            settingsChanged = *tu4.settings;
            screenDisableCursor();
            // Blank the map animation area
            for (int row = 26; row < 46; row++)
                for (int col = 2; col < 78; col++)
                    screenPutChar(col, row, ' ', 0x00);
            runMenu(&confMenu, &extendedMenuArea, true);
            screenEnableCursor();

            /* If the text style changed while in the config menu, now that we
             * are back at the main menu reload ALL graphics assets from the
             * new scheme and rebuild the intro. Faithful to xu4's
             * deleteIntro()/screenReInit()/init() cycle, but deferred to here
             * so it runs on a fully-closed menu. init() re-reads intro
             * graphics; the titleDataLoaded cache is invalidated so the title
             * screen reloads from the new scheme's TITLE.ASP. */
            if (textStyleChanged) {
                textStyleChanged = false;
                deleteIntro();      // delete intro stuff
                screenReInit();     // reload all graphics assets (new scheme)
                titleDataLoadedReset();  // invalidate cached title screen art
                // We are re-initing from the main menu — the title sequence
                // has already been shown, so force init() down its skip-titles
                // path. Otherwise init() replays the titles branch, which
                // leaves beastiesVisible=false / beastieOffset=-32 and the
                // beasties never reappear.
                bSkipTitles = true;
                init();             // re-fetch intro graphics for the new style
                mode = INTRO_MENU;
            }

            updateScreen();
            break;
        }
        case 'a':
            about();
            break;
        case 'q':
            tu4.eventHandler->quitGame();
            break;
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        {
            int n = key - '0';
            if (n > MUSIC_NONE && n < MUSIC_MAX)
                musicPlay(introMusic = n);
        }
            break;
        default:
            valid = false;
            break;
        }
        break;

    default:
        ASSERT(0, "key handler called in wrong mode");
        return true;
    }

    return valid || KeyHandler::defaultHandler(key, NULL);
}

/**
 * Draws the small map on the intro screen.
 */
void IntroController::drawMap() {
    if (sleepCycles > 0) {
        drawMapStatic();
        drawMapAnimated();
        sleepCycles--;
    }
    else {
        TileId tileId;
        int frame;
        int x, y;
        unsigned char commandNibble;
        unsigned char dataNibble;
        const unsigned char* script = binData->scriptTable;

        do {
            commandNibble = script[scrPos] >> 4;

            switch(commandNibble) {
                /* 0-4 = set object position and tile frame */
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
                /* ----------------------------------------------------------
                   Set object position and tile frame
                   Format: yi [t(3); x(5)]
                   i = table index
                   x = x coordinate (5 least significant bits of second byte)
                   y = y coordinate
                   t = tile frame (3 most significant bits of second byte)
                   ---------------------------------------------------------- */
                dataNibble = script[scrPos] & 0xf;
                x = script[scrPos+1] & 0x1f;
                y = commandNibble;

                // See if the tile id needs to be recalculated
                tileId = binData->baseTileTable[dataNibble]->getId();
                frame = script[scrPos+1] >> 5;
                if (frame >= binData->baseTileTable[dataNibble]->getFrames()) {
                    frame -= binData->baseTileTable[dataNibble]->getFrames();
                    tileId += 1;
                }

                objectStateTable[dataNibble].x = x;
                objectStateTable[dataNibble].y = y;
                objectStateTable[dataNibble].tile = MapTile(tileId);
                objectStateTable[dataNibble].tile.frame = frame;

                scrPos += 2;
                break;
            case 7:
                /* ---------------
                   Delete object
                   Format: 7i
                   i = table index
                   --------------- */
                dataNibble = script[scrPos] & 0xf;
                objectStateTable[dataNibble].tile = 0;
                scrPos++;
                break;
            case 8:
                /* ----------------------------------------------
                   Redraw intro map and objects, then go to sleep
                   Format: 8c
                   c = cycles to sleep
                   ---------------------------------------------- */
                drawMapStatic();
                drawMapAnimated();

                /* set sleep cycles */
                sleepCycles = script[scrPos] & 0xf;
                scrPos++;
                break;
            case 0xf:
                /* -------------------------------------
                   Jump to the start of the script table
                   Format: f?
                   ? = doesn't matter
                   ------------------------------------- */
                scrPos = 0;
                break;
            default:
                /* invalid command */
                scrPos++;
                break;
            }

        } while (commandNibble != 8);
    }
}

void IntroController::drawMapStatic() {
    int x, y;

    // draw map tiles - drawTile handles scroll animation for water automatically
    for (y = 0; y < INTRO_MAP_HEIGHT; y++)
        for (x = 0; x < INTRO_MAP_WIDTH; x++)
            mapArea.drawTile(binData->introMap[x + (y * INTRO_MAP_WIDTH)], x, y);
}

void IntroController::drawMapAnimated() {
    int i;

    for (i = 0; i < IntroBinData::INTRO_BASETILE_TABLE_SIZE; i++) {
        IntroObjectState& state = objectStateTable[i];
        if (state.tile != 0) {
            // 1. Draw background terrain
            mapArea.drawTile(binData->introMap[state.x + (state.y * INTRO_MAP_WIDTH)], state.x, state.y);
            // 2-4. Draw NPC/object (with or without transparency)
            if (tu4.settings->enhancements &&
                tu4.settings->enhancementsOptions.u4TileTransparencyHack) {
                mapArea.drawTileTransparent(state.tile, state.x, state.y);
            } else {
                mapArea.drawTile(state.tile, state.x, state.y);
            }
        }
    }
    
}

/**
 * Draws the animated beasts in the upper corners of the screen.
 */
void IntroController::drawBeasties() {
    drawBeastie(0, beastieOffset, binData->beastie1FrameTable[beastie1Cycle]);
    drawBeastie(1, beastieOffset, binData->beastie2FrameTable[beastie2Cycle]);
    if (beastieOffset < 0)
        beastieOffset++;
}

/**
 * Animates the "beasties".  Draws beastie animation frames from
 * ANIMATE.ASP at the top-left and top-right of the intro screen.
 */
void IntroController::drawBeastie(int beast, int vertoffset, int frame) {
    ASSERT(beast == 0 || beast == 1, "invalid beast: %d", beast);

    if (!beastiesImg || !beastiesImg->image)
        return;

    const uint8_t* aspData = beastiesImg->image->getAspData();
    if (!aspData)
        return;

    int subIdx = beastieSub[beast] + frame;
    if (subIdx >= beastiesImg->subImageCount)
        return;

    const SubImage* sub = beastiesImg->subImages + subIdx;

    // Source region (1-based in XML, convert to 0-based)
    int srcX = sub->x - 1;
    int srcY = sub->y - 1;
    int srcW = sub->width;
    int srcH = sub->height;

    // Destination: beast 0 at left edge, beast 1 at right edge
    int dstX = beast ? (SCREEN_COLS - srcW) : 0;
    int dstY = vertoffset;  // vertoffset is already in char units

    // Draw the beastie frame
    for (int row = 0; row < srcH; row++) {
        int dy = dstY + row;
        if (dy < 0 || dy >= SCREEN_ROWS) continue;
        for (int col = 0; col < srcW; col++) {
            int dx = dstX + col;
            if (dx < 0 || dx >= SCREEN_COLS) continue;
            int idx = ((srcY + row) * ASP_SCREEN_COLS + (srcX + col)) * 2;
            uint8_t ch   = aspData[idx];
            uint8_t attr = aspData[idx + 1];
            // Only draw non-black cells (skip background)
            if (ch != 0x20 || attr != 0x00)
                screenPutChar(dx, dy, ch, attr);
        }
    }
}

/**
 * Animates the moongate in the tree intro image.  There are two
 * overlays in the part of the image normally covered by the text.  If
 * the frame parameter is "moongate", the moongate overlay is painted
 * over the image.  If frame is "items", the second overlay is
 * painted: the circle without the moongate, but with a small white
 * dot representing the anhk and history book.
 */
void IntroController::animateTree(Symbol frame) {
    const SubImage* subimage;
    ImageInfo *info = tu4.imageMgr->imageInfo(IMG_MOONGATE, &subimage);
    if (!subimage || !info || !info->image)
        return;

    const uint8_t* aspData = info->image->getAspData();
    if (!aspData)
        return;

    // Subimage coordinates (1-based in XML, stored as-is in SubImage)
    int srcX = subimage->x - 1;  // convert to 0-based
    int srcY = subimage->y - 1;
    int srcW = subimage->width;
    int srcH = subimage->height;
    int dstX = subimage->left - 1;  // placement position (1-based to 0-based)
    int dstY = subimage->top - 1;

    if (frame == IMG_MOONGATE) {
        soundPlay(SOUND_GATE_OPEN);
        // Opening: grow upward from bottom, always showing top portion of source
        // Matches xu4: draws top gateH rows of source at screenBottom - gateH
        int screenBottom = dstY + srcH;
        for (int gateH = 1; gateH <= srcH; gateH++) {
            int drawY = screenBottom - gateH;
            for (int row = 0; row < gateH; row++) {
                int sr = srcY + row;  // always from top of source
                for (int col = 0; col < srcW; col++) {
                    int idx = (sr * ASP_SCREEN_COLS + (srcX + col)) * 2;
                    screenPutChar(dstX + col, drawY + row, aspData[idx], aspData[idx + 1]);
                }
            }
            screenSwapBuffers();
            if (EventHandler::wait_msecs(42))
                break;
        }
    } else {
        // Closing: shrink moongate from top down (items revealed behind)
        // Matches xu4: each frame draws full items background, then overlays
        // moongate at decreasing height anchored at the bottom.
        const SubImage* bgSub;
        ImageInfo* bgInfo = tu4.imageMgr->imageInfo(frame, &bgSub);
        const uint8_t* bgData = (bgInfo && bgInfo->image) ? bgInfo->image->getAspData() : NULL;

        // Items subimage source coordinates (1-based in XML, convert to 0-based)
        int itemSrcX = bgSub ? (bgSub->x - 1) : 0;
        int itemSrcY = bgSub ? (bgSub->y - 1) : 0;
        int itemW = bgSub ? bgSub->width : srcW;
        int itemH = bgSub ? bgSub->height : srcH;

        int screenBottom = dstY + srcH;

        for (int gateH = srcH - 1; gateH >= 0; gateH--) {
            // Draw full items background first
            if (bgData && bgSub) {
                for (int row = 0; row < itemH; row++) {
                    for (int col = 0; col < itemW; col++) {
                        int idx = ((itemSrcY + row) * ASP_SCREEN_COLS + (itemSrcX + col)) * 2;
                        screenPutChar(dstX + col, dstY + row, bgData[idx], bgData[idx + 1]);
                    }
                }
            }

            // Overlay moongate at reduced height, anchored at bottom
            if (gateH > 0) {
                int drawY = screenBottom - gateH;
                for (int row = 0; row < gateH; row++) {
                    int sr = srcY + row;
                    for (int col = 0; col < srcW; col++) {
                        int idx = (sr * ASP_SCREEN_COLS + (srcX + col)) * 2;
                        screenPutChar(dstX + col, drawY + row, aspData[idx], aspData[idx + 1]);
                    }
                }
            }

            screenSwapBuffers();
            if (EventHandler::wait_msecs(42))
                break;
        }
    }
}

/**
 * Draws the cards in the character creation sequence with the gypsy.
 */
void IntroController::drawCard(int pos, int card, const uint8_t* origin) {
    static const char *cardNames[] = {
        "honestycard", "compassioncard", "valorcard", "justicecard",
        "sacrificecard", "honorcard", "spiritualitycard", "humilitycard"
    };

    ASSERT(pos == 0 || pos == 1, "invalid pos: %d", pos);
    ASSERT(card < 8, "invalid card: %d", card);

    (void)origin;

    /* The destination slot is determined by the CHOICE, not by where the card
     * is cropped from: choice A (pos 0) goes in the LEFT slot, choice B (pos 1)
     * in the RIGHT slot. The slot screen coordinates come from the abacus
     * image's leftx/rightx/top attributes (0-based, view-relative).
     *
     * ImageView::draw() reads the card subimage from its own (x-1,y-1) and
     * writes at (dx,dy) (offset by the view origin). To draw the card's pixels
     * into the target slot we set dx,dy so the destination lands on the slot,
     * cancelling the card's own crop origin: dx = slotX - (cardSub->x - 1). */
    Symbol cardName = tu4.config->intern(cardNames[card]);
    const SubImage* cardSub;
    if (!tu4.imageMgr->imageInfo(cardName, &cardSub) || !cardSub) {
        backgroundArea.draw(cardName, 0, 0);
        return;
    }

    /* Slot placement from the abacus art (leftx/rightx/top). */
    const SubImage* abacusSub;
    ImageInfo* abacus = tu4.imageMgr->imageInfo(BKGD_ABACUS, &abacusSub);
    int slotX = -1, slotY = -1;
    if (abacus) {
        slotX = pos ? abacus->cardRightX : abacus->cardLeftX;
        slotY = abacus->cardTop;
    }

    /* ImageView::draw() reads the card subimage from its own (x-1,y-1) and
     * writes at (view.x + dx + col, view.y + dy + row) -- i.e. dx,dy ARE the
     * absolute view-relative destination column/row; the source offset is not
     * added to the destination. So pass the slot coords directly. If the
     * abacus has no placement attributes, fall back to the card's own crop
     * location so it lands where it was cropped from. */
    int dx, dy;
    if (slotX >= 0 && slotY >= 0) {
        dx = slotX;
        dy = slotY;
    } else {
        dx = cardSub->x - 1;
        dy = cardSub->y - 1;
    }
    backgroundArea.draw(cardName, dx, dy);
}

/**
 * Draws the beads in the abacus during the character creation sequence
 */
void IntroController::drawAbacusBeads(int row, int selectedVirtue, int rejectedVirtue) {
    ASSERT(row >= 0 && row < 7, "invalid row: %d", row);
    ASSERT(selectedVirtue < 8 && selectedVirtue >= 0, "invalid virtue: %d", selectedVirtue);
    ASSERT(rejectedVirtue < 8 && rejectedVirtue >= 0, "invalid virtue: %d", rejectedVirtue);

    // Get white bead subimage info (contains placement data)
    const SubImage* whiteSub;
    ImageInfo* whiteInfo = tu4.imageMgr->imageInfo(IMG_WHITEBEAD, &whiteSub);
    if (!whiteSub || !whiteInfo || !whiteInfo->image)
        return;

    // Get black bead subimage info
    const SubImage* blackSub;
    ImageInfo* blackInfo = tu4.imageMgr->imageInfo(IMG_BLACKBEAD, &blackSub);
    if (!blackSub || !blackInfo || !blackInfo->image)
        return;

    const uint8_t* whiteData = whiteInfo->image->getAspData();
    const uint8_t* blackData = blackInfo->image->getAspData();
    if (!whiteData || !blackData)
        return;

    // Placement values from subimage attributes (1-based in XML)
    int xstart   = whiteSub->left - 1;    // convert to 0-based
    int ystart   = whiteSub->ystart - 1;
    int yspacing = whiteSub->yspacing;
    int beadW    = whiteSub->width;
    int beadH    = whiteSub->height;
    int xspacing = beadW;

    // Source position of bead graphics within the ASP file (1-based to 0-based)
    int whiteSrcX = whiteSub->x - 1;
    int whiteSrcY = whiteSub->y - 1;
    int blackSrcX = blackSub->x - 1;
    int blackSrcY = blackSub->y - 1;

    // Calculate Y position for this row's rung
    int y = ystart + (row * yspacing);

    // Draw white bead at selected virtue's position
    int wxPos = xstart + (selectedVirtue * xspacing);
    for (int r = 0; r < beadH; r++) {
        for (int c = 0; c < beadW; c++) {
            int idx = ((whiteSrcY + r) * ASP_SCREEN_COLS + (whiteSrcX + c)) * 2;
            screenPutChar(wxPos + c, y + r, whiteData[idx], whiteData[idx + 1]);
        }
    }

    // Draw black bead at rejected virtue's position
    int bxPos = xstart + (rejectedVirtue * xspacing);
    for (int r = 0; r < beadH; r++) {
        for (int c = 0; c < beadW; c++) {
            int idx = ((blackSrcY + r) * ASP_SCREEN_COLS + (blackSrcX + c)) * 2;
            screenPutChar(bxPos + c, y + r, blackData[idx], blackData[idx + 1]);
        }
    }
}

/**
 * Paints the screen.
 */
void IntroController::updateScreen() {
    screenHideCursor();

    switch (mode) {
    case INTRO_MAP:
        backgroundArea.draw(BKGD_INTRO);
        drawMap();
        drawBeasties();
        // display the profile name if a local profile is being used
        {
        const string& pname = tu4.settings->profile;
        if (! pname.empty())
            screenTextAt(SCREEN_COLS-pname.length(), 48, "%s", pname.c_str());
        }
        break;

    case INTRO_MENU:
        // draw the extended background for all option screens
        backgroundArea.draw(BKGD_INTRO);

        // if there is an error message to display, show it
        if (tu4.errorMessage)
        {
            int len = strlen(tu4.errorMessage);
            menuArea.textAt(38 - len / 2, 10, tu4.errorMessage);
            tu4.errorMessage = NULL;

            drawBeasties();
            

            // wait for a couple seconds
            EventHandler::wait_msecs(3000);
            // clear the screen again
            backgroundArea.draw(BKGD_INTRO);
        }

        {
            // Match introt.c menu layout exactly
            // menu_left=2, menu_top=26, menu_width=76
            const int menu_left = 2;
            const int menu_top = 26;
            const int menu_width = 76;

            // Clear menu area
            for (int row = menu_top; row < menu_top + 20; row++)
                for (int col = menu_left; col < menu_left + menu_width; col++)
                    screenPutChar(col, row, ' ', 0x00);

            // "In another world, in a time to come." centered, color 7 (gray)
            const char *tagline = "In another world, in a time to come.";
            int len = strlen(tagline);
            int tx = menu_left + (menu_width - len) / 2;
            for (int i = 0; tagline[i]; i++)
                screenPutChar(tx + i, menu_top + 2, tagline[i], 0x07);

            // "Options:" centered, color 15 (white)
            const char *options_str = "Options:";
            len = strlen(options_str);
            tx = menu_left + (menu_width - len) / 2;
            for (int i = 0; options_str[i]; i++)
                screenPutChar(tx + i, menu_top + 5, options_str[i], 0x0F);

            // Menu items, spaced by 2 rows, left-aligned to center of longest
            static const char *labels[] = {
                "Return to the view",
                "Journey Onward",
                "Initiate New Game",
                "Configure",
                "About"
            };
            int first_len = strlen(labels[0]);
            int align_x = menu_left + (menu_width - first_len) / 2;

            for (int i = 0; i < 5; i++) {
                int item_row = menu_top + 8 + i * 2;
                // First letter yellow (14), rest white (15)
                screenPutChar(align_x, item_row, labels[i][0], 0x0E);
                for (int j = 1; labels[i][j]; j++)
                    screenPutChar(align_x + j, item_row, labels[i][j], 0x0F);
            }
        }
        drawBeasties();

        // draw the cursor last
        screenSetCursorPos(48, 32);
        screenShowCursor();
        break;

    default:
        ASSERT(0, "bad mode in updateScreen");
    }

    screenUpdateCursor();
}

/**
 * Initiate a new savegame by reading the name, sex, then presenting a
 * series of questions to determine the class of the new character.
 */
void IntroController::initiateNewGame() {
    // disable the screen cursor because a text cursor will now be used
    screenDisableCursor();

    // draw the extended background for all option screens
    backgroundArea.draw(BKGD_INTRO);

    // display name prompt and read name from keyboard.
    // Everything is placed in the middle 40 columns of the 80-wide screen
    // (screen cols 20-59). menuArea origin is screen x=2, so the middle
    // band starts at local x = 18 (=20-2) and is 40 wide. The first line
    // is centered within the band (18 + (40-32)/2 = 22); the second line
    // is left-aligned with the first.
    menuArea.textAt(22, 3, "By what name shalt thou be known"); // 32 chars
    menuArea.textAt(22, 4, "in this world and time?");

    // enable the text cursor after setting it's initial position
    // this will avoid drawing in undesirable areas like 0,0
    // (12-char input field centered: 18 + (40-12)/2 = 32)
    menuArea.setCursorPos(32, 7, false);
    menuArea.setCursorFollowsText(true);
    menuArea.enableCursor();

    drawBeasties();

    string nameBuffer = ReadStringController::get(12, &menuArea, "\033");
    if (nameBuffer.length() == 0) {
        // the user didn't enter a name
        menuArea.disableCursor();
        screenEnableCursor();
        updateScreen();
        return;
    }

    // draw the extended background for all option screens
    backgroundArea.draw(BKGD_INTRO);

    // display sex prompt and read sex from keyboard.
    // Left-aligned with the name prompt (local x=22 -> screen col 24).
    menuArea.textAt(22, 3, "Art thou Male or Female?"); // 24 chars, ends local 46

    // the cursor is already enabled, just change its position
    // (answer sits just after the prompt, still inside cols 20-59)
    menuArea.setCursorPos(47, 3, true);

    drawBeasties();

    SexType sex;
    int sexChoice = ReadChoiceController::get("mf");
    if (sexChoice == 'm')
        sex = SEX_MALE;
    else
        sex = SEX_FEMALE;

    // Display entry for a moment.
    menuArea.drawChar(toupper(sexChoice), 47, 3);
    
    EventHandler::wait_msecs(250);

    finishInitiateGame(nameBuffer, sex);
}

void IntroController::finishInitiateGame(const string &nameBuffer, SexType sex)
{
#ifdef IOS
    mode = INTRO_MENU; // ensure we are now in the menu mode, (i.e., stop drawing the map).
#endif
    // no more text entry, so disable the text cursor
    menuArea.disableCursor();

    {
    uint16_t saveGroup = tu4.imageMgr->setResourceGroup(StageIntro);

    // show the lead up story
    showStory();
    if (tu4.stage != StageIntro)
        return;

    // ask questions that determine character class
    startQuestions();
    if (tu4.stage != StageIntro)
        return;

    tu4.imageMgr->setResourceGroup(saveGroup);
    }

    // write out save game an segue into game

    delete tu4.saveGame;
    tu4.saveGame = NULL;    // Make GameController::init() reload the game.

    FILE *saveGameFile = fopen((tu4.settings->getUserPath() + PARTY_SAV).c_str(), "wb");
    if (!saveGameFile) {
        questionArea.disableCursor();
        tu4.errorMessage = "Unable to create save game!";
        updateScreen();
        return;
    }

    {
    SaveGame saveGame;
    SaveGamePlayerRecord avatar;

    avatar.init();
    strcpy(avatar.name, nameBuffer.c_str());
    avatar.sex = sex;
    saveGame.init(&avatar);
    initPlayers(&saveGame);
    saveGame.food = 30000;
    saveGame.gold = 200;
    saveGame.reagents[REAG_GINSENG] = 3;
    saveGame.reagents[REAG_GARLIC] = 4;
    saveGame.torches = 2;
    saveGame.write(saveGameFile);
    }

    fclose(saveGameFile);

    saveGameFile = fopen((tu4.settings->getUserPath() + MONSTERS_SAV).c_str(), "wb");
    if (saveGameFile) {
        saveGameMonstersWrite(NULL, saveGameFile);
        fclose(saveGameFile);
    }
    justInitiatedNewGame = true;

    // show the text thats segues into the main game
    showText(binData->introGypsy[GYP_SEGUE1]);
#ifdef IOS
    U4IOS::switchU4IntroControllerToContinueButton();
#endif
    anyKey.wait();

    showText(binData->introGypsy[GYP_SEGUE2]);
    anyKey.wait();

    // done: exit intro and let game begin
    questionArea.disableCursor();

    if (tu4.stage != StageExitGame)
        tu4.stage = StagePlay;
    tu4.eventHandler->setControllerDone();
}

void IntroController::showStory() {
    beastiesVisible = false;

    questionArea.setCursorFollowsText(true);

    // Clear entire screen to remove map borders from previous screen
    for (int row = 0; row < SCREEN_ROWS; row++)
        for (int col = 0; col < SCREEN_COLS; col++)
            screenPutChar(col, row, ' ', 0x00);

    for (int storyInd = 0; storyInd < 24; storyInd++) {
        if (storyInd == 0)
            backgroundArea.draw(BKGD_TREE, 0, 0, 38);
        else if (storyInd == 6)
            backgroundArea.draw(BKGD_PORTAL, 0, 0, 38);
        else if (storyInd == 11)
            backgroundArea.draw(BKGD_TREE, 0, 0, 38);
        else if (storyInd == 15)
            backgroundArea.draw(BKGD_OUTSIDE, 0, 0, 38);
        else if (storyInd == 17)
            backgroundArea.draw(BKGD_INSIDE, 0, 0, 38);
        else if (storyInd == 20)
            backgroundArea.draw(BKGD_WAGON, 0, 0, 38);
        else if (storyInd == 21)
            backgroundArea.draw(BKGD_GYPSY, 0, 0, 38);
        else if (storyInd == 23)
            backgroundArea.draw(BKGD_ABACUS, 0, 0, 41);

        showText(binData->introText[storyInd]);

        if (storyInd == 3) {
            questionArea.disableCursor();
            animateTree(IMG_MOONGATE);
        } else if (storyInd == 5) {
            questionArea.disableCursor();
            animateTree(IMG_ITEMS);
        }

        // enable the cursor here to avoid drawing in undesirable locations
        questionArea.enableCursor();
        anyKey.wait();
        if (tu4.stage != StageIntro)
            break;
    }
}

/**
 * Starts the gypsys questioning that eventually determines the new
 * characters class.
 */
void IntroController::startQuestions() {
    static uint8_t originTable[6] = {
        4, 4, 56,       // Text mode: left x, y, right x (0-based)
        4, 4, 56,       // (same)
    };
    ReadChoiceController questionController("ab");
    uint8_t* origin = originTable;
    /* Text mode: no origin offset needed */

    questionRound = 0;
    initQuestionTree();

    const vector<string>& gypsyText = binData->introGypsy;
    int i1, i2, n;

    while (tu4.stage == StageIntro) {
        // draw the abacus background, if necessary
        if (questionRound == 0) {
            backgroundArea.draw(BKGD_ABACUS, 0, 0, 41);
            n = GYP_PLACES_FIRST;
        } else {
            n = (questionRound == 6) ? GYP_PLACES_LAST : GYP_PLACES_TWOMORE;
        }

        i1 = questionRound * 2;
        i2 = i1 + 1;

        // draw the cards and show the lead up text

        questionArea.disableCursor();
        questionArea.clear();
        questionArea.textAt(0, 0, gypsyText[n].c_str());
        questionArea.textAt(0, 1, gypsyText[GYP_UPON_TABLE].c_str());
        EventHandler::wait_msecs(1000);

        const string& virtue1 = gypsyText[questionTree[i1] + 4];
        questionArea.textAt(0, 2, "%s and", virtue1.c_str());
        drawCard(0, questionTree[i1], origin);
        EventHandler::wait_msecs(1000);

        questionArea.textAt(virtue1.size() + 4, 2, " %s.  She says",
                            gypsyText[questionTree[i2] + 4].c_str());
        drawCard(1, questionTree[i2], origin);
        questionArea.textAt(0, 3, "\"Consider this:\"");
        questionArea.enableCursor();

#ifdef IOS
        U4IOS::switchU4IntroControllerToContinueButton();
#endif
        // wait for a key
        anyKey.wait();

        // show the question to choose between virtues
        showText(getQuestion(questionTree[i1], questionTree[i2]));

#ifdef IOS
        U4IOS::switchU4IntroControllerToABButtons();
#endif
        // wait for an answer
        tu4.eventHandler->pushController(&questionController);
        int choice = questionController.waitFor();

        // update the question tree
        if (doQuestion(choice == 'a' ? 0 : 1))
            return;
    }
}

/**
 * Get the text for the question giving a choice between virtue v1 and
 * virtue v2 (zero based virtue index, starting at honesty).
 */
string IntroController::getQuestion(int v1, int v2) {
    int i = 0;
    int d = 7;

    ASSERT(v1 < v2, "first virtue must be smaller (v1 = %d, v2 = %d)", v1, v2);

    while (v1 > 0) {
        i += d;
        d--;
        v1--;
        v2--;
    }

    ASSERT((i + v2 - 1) < 28, "calculation failed");

    return binData->introQuestions[i + v2 - 1];
}

/**
 * Starts the game.
 */
void IntroController::journeyOnward() {
    // Return to a running game or attempt to load a saved one.
    if (tu4.saveGame || saveGameLoad()) {
        tu4.stage = StagePlay;
        tu4.eventHandler->setControllerDone();
    } else {
        updateScreen();     // Shows errorMessage set by saveGameLoad().
    }
}

/**
 * Shows an about box.
 */
void IntroController::about() {
    // draw the extended background for all option screens
    backgroundArea.draw(BKGD_INTRO);

    screenHideCursor();

    // Match introt.c about screen content and centering
    // menuArea is 76 wide, 22 tall, at position (2, 26)
    int areaW = 76;
    int areaH = 22;

    static const char *main_lines[] = {
        "",
        "A text mode demake of Ultima IV",
        "based on xu4 (xu4.sourceforge.net)",
        "",
        "tu4 is free software; you can redistribute",
        "it and/or modify it under the terms of the",
        "GNU GPL as published by the FSF. See COPYING.",
    };
    int num_main = 7;

    static const char *copy_lines[] = {
        "Copyright (c) 2026, ASCII Dragon",
        "Copyright (c) 2002-2022, xu4 Team",
        "Copyright (c) 1987, Lord British",
    };
    int num_copy = 3;

    // Total lines: title + main + 1 blank + copyright + 1 blank + footer
    int total_lines = 1 + num_main + 1 + num_copy + 1 + 1;
    int start_row = (areaH - total_lines) / 2;

    // Title centered
    const char *title = "TU4 " VERSION;
    int title_x = (areaW - (int)strlen(title)) / 2;
    menuArea.textAt(title_x, start_row, "%s", title);
    int cur_row = start_row + 1;

    // Find longest main line for centering
    int max_main_len = 0;
    for (int i = 0; i < num_main; i++) {
        int len = strlen(main_lines[i]);
        if (len > max_main_len) max_main_len = len;
    }
    int main_x = (areaW - max_main_len) / 2;

    // Find longest copyright line for centering
    int max_copy_len = 0;
    for (int i = 0; i < num_copy; i++) {
        int len = strlen(copy_lines[i]);
        if (len > max_copy_len) max_copy_len = len;
    }
    int copy_x = (areaW - max_copy_len) / 2;

    for (int i = 0; i < num_main; i++) {
        menuArea.textAt(main_x, cur_row, "%s", main_lines[i]);
        cur_row++;
    }

    cur_row++;  // blank line

    for (int i = 0; i < num_copy; i++) {
        menuArea.textAt(copy_x, cur_row, "%s", copy_lines[i]);
        cur_row++;
    }

    cur_row++;  // blank line

    const char *footer = "Press any key to return...";
    int footer_x = (areaW - (int)strlen(footer)) / 2;
    menuArea.textAt(footer_x, cur_row, "%s", footer);

    drawBeasties();

    anyKey.wait();

    screenShowCursor();
    updateScreen();
}

/**
 * Shows text in the question area.
 */
void IntroController::showText(const string &text) {
    string current = text;
    int lineNo = 0;

    questionArea.clear();

    unsigned long pos = current.find("\n");
    while (pos < current.length()) {
        questionArea.textAt(0, lineNo++, "%s", current.substr(0, pos).c_str());
        current = current.substr(pos+1);
        pos = current.find("\n");
    }

    /* write the last line (possibly only line) */
    questionArea.textAt(0, lineNo++, "%s", current.substr(0, pos).c_str());
}

/**
 * Run a menu and return when the menu has been closed.  Screen
 * updates are handled by observing the menu.
 */
void IntroController::runMenu(Menu *menu, TextView *view, bool withBeasties) {
    menu->reset();
    view->clear();
    menu->show(view);
    if (withBeasties)
        drawBeasties();

    MenuController menuController(menu, view);
    tu4.eventHandler->pushController(&menuController);
    menuController.waitFor();

    // enable the cursor here, after the menu has been established
    view->enableCursor();
    view->disableCursor();
}

/**
 * Timer callback for the intro sequence.  Handles animating the intro
 * map, the beasties, etc..
 *
 * Note: the title animation itself (INTRO_TITLES) runs to completion
 * inside present(), before this controller is pushed onto the timer
 * queue, so mode is always INTRO_MAP or later by the time this fires.
 */
void IntroController::timerFired() {
    screenCycle();
    screenUpdateCursor();

    if (mode == INTRO_MAP) {
        drawMap();

    }

    if (beastiesVisible)
        drawBeasties();

    if (tu4_random(2) && ++beastie1Cycle >= IntroBinData::BEASTIE1_FRAMES)
        beastie1Cycle = 0;
    if (tu4_random(2) && ++beastie2Cycle >= IntroBinData::BEASTIE2_FRAMES)
        beastie2Cycle = 0;

    
}

/**
 * Update the screen when an observed menu is reset or has an item
 * activated.
 * TODO, reduce duped code.
 */
void IntroController::introNotice(int sender, void* eventData, void* user) {
    MenuEvent* event = (MenuEvent*) eventData;
    ((IntroController*) user)->dispatchMenu(event->menu, *event);
}

void IntroController::dispatchMenu(const Menu *menu, MenuEvent &event) {
    if (menu == &confMenu)
        updateConfMenu(event);
    else if (menu == &videoMenu)
        updateVideoMenu(event);
    else if (menu == &gfxMenu)
        updateGfxMenu(event);
    else if (menu == &soundMenu)
        updateSoundMenu(event);
    else if (menu == &inputMenu)
        updateInputMenu(event);
    else if (menu == &speedMenu)
        updateSpeedMenu(event);
    else if (menu == &gameplayMenu)
        updateGameplayMenu(event);
    else if (menu == &interfaceMenu)
        updateInterfaceMenu(event);

    // beasties are always visible on the menus
    drawBeasties();
}

void IntroController::updateConfMenu(MenuEvent &event) {
    if (event.type == MenuEvent::ACTIVATE ||
        event.type == MenuEvent::INCREMENT ||
        event.type == MenuEvent::DECREMENT) {

        // show or hide game enhancement options if enhancements are enabled/disabled
        confMenu.getItemById(MI_CONF_GAMEPLAY)->setVisible(settingsChanged.enhancements);
        confMenu.getItemById(MI_CONF_INTERFACE)->setVisible(settingsChanged.enhancements);

        // save settings
        tu4.settings->setData(settingsChanged);
        tu4.settings->write();

        switch(event.item->getId()) {
        case MI_CONF_VIDEO:
            runMenu(&videoMenu, &extendedMenuArea, true);
            break;
        case MI_VIDEO_CONF_GFX:
            runMenu(&gfxMenu, &extendedMenuArea, true);
            break;
        case MI_CONF_SOUND:
            runMenu(&soundMenu, &extendedMenuArea, true);
            break;
        case MI_CONF_INPUT:
            runMenu(&inputMenu, &extendedMenuArea, true);
            break;
        case MI_CONF_SPEED:
            runMenu(&speedMenu, &extendedMenuArea, true);
            break;
        case MI_CONF_GAMEPLAY:
            runMenu(&gameplayMenu, &extendedMenuArea, true);
            break;
        case MI_CONF_INTERFACE:
            runMenu(&interfaceMenu, &extendedMenuArea, true);
            break;
        case CANCEL:
            // discard settings
            settingsChanged = *tu4.settings;
            break;
        default: break;
        }
    }

    // draw the extended background for all option screens
}

void IntroController::updateVideoMenu(MenuEvent &event) {
    if (event.type == MenuEvent::ACTIVATE ||
        event.type == MenuEvent::INCREMENT ||
        event.type == MenuEvent::DECREMENT) {

        switch(event.item->getId()) {
        case USE_SETTINGS:
            /* save settings (if necessary) */
            if (*tu4.settings != settingsChanged) {
                bool styleChanged = (tu4.settings->textStyle != settingsChanged.textStyle);
                bool scaleChanged = (tu4.settings->scale != settingsChanged.scale) ||
                                    (tu4.settings->fullscreen != settingsChanged.fullscreen);
                tu4.settings->setData(settingsChanged);
                tu4.settings->write();

                /* A text-style change requires reloading ALL graphics assets
                 * and repainting the intro. Defer that heavy work until the
                 * config menu is fully exited (back at the main menu) so we
                 * don't repaint while the menu is still open — see the 'c'
                 * handler in keyPressed(). Scale/fullscreen only resize the
                 * window, which is safe to apply immediately. */
                if (styleChanged)
                    textStyleChanged = true;
                else if (scaleChanged)
                    screenReInit();

                // go back to menu mode
                mode = INTRO_MENU;
            }
            break;
        case MI_VIDEO_CONF_GFX:
            runMenu(&gfxMenu, &extendedMenuArea, true);
            break;
        case CANCEL:
            // discard settings
            settingsChanged = *tu4.settings;
            break;
        default: break;
        }
    }

    // draw the extended background for all option screens
}

void IntroController::updateGfxMenu(MenuEvent &event)
{
    if (event.type == MenuEvent::ACTIVATE ||
        event.type == MenuEvent::INCREMENT ||
        event.type == MenuEvent::DECREMENT) {


        switch(event.item->getId()) {
        case MI_GFX_RETURN:
            runMenu(&videoMenu, &extendedMenuArea, true);
            break;
        default: break;
        }
    }

    // draw the extended background for all option screens
}

void IntroController::updateSoundMenu(MenuEvent &event) {
    if (event.type == MenuEvent::ACTIVATE ||
        event.type == MenuEvent::INCREMENT ||
        event.type == MenuEvent::DECREMENT) {

        switch(event.item->getId()) {
            case MI_SOUND_01:
                musicSetVolume(settingsChanged.musicVol);
                break;
            case MI_SOUND_02:
                soundSetVolume(settingsChanged.soundVol);
                soundPlay(SOUND_FLEE);
                break;
            case USE_SETTINGS:
                // save settings
                tu4.settings->setData(settingsChanged);
                tu4.settings->write();
                musicPlay(introMusic);
                break;
            case CANCEL:
                musicSetVolume(tu4.settings->musicVol);
                soundSetVolume(tu4.settings->soundVol);
                // discard settings
                settingsChanged = *tu4.settings;
                break;
            default: break;
        }
    }

    // draw the extended background for all option screens
}

void IntroController::updateInputMenu(MenuEvent &event) {
    if (event.type == MenuEvent::ACTIVATE ||
        event.type == MenuEvent::INCREMENT ||
        event.type == MenuEvent::DECREMENT) {

        switch(event.item->getId()) {
        case USE_SETTINGS:
            // save settings
            tu4.settings->setData(settingsChanged);
            tu4.settings->write();

            // re-initialize keyboard
            EventHandler::setKeyRepeat(settingsChanged.keydelay, settingsChanged.keyinterval);
#ifndef IOS
            screenShowMouseCursor(tu4.settings->mouseOptions.enabled);
#endif
            break;
        case CANCEL:
            // discard settings
            settingsChanged = *tu4.settings;
            break;
        default: break;
        }
    }

    // draw the extended background for all option screens

    // after drawing the menu, extra menu text can be added here
    extendedMenuArea.textAt(0, 5, "Mouse Options:");
}

void IntroController::updateSpeedMenu(MenuEvent &event) {
    if (event.type == MenuEvent::ACTIVATE ||
        event.type == MenuEvent::INCREMENT ||
        event.type == MenuEvent::DECREMENT) {

        switch(event.item->getId()) {
        case USE_SETTINGS:
            // save settings
            tu4.settings->setData(settingsChanged);
            tu4.settings->write();

            // re-initialize events
            tu4.eventHandler->setTimerInterval(1000 /
                                        tu4.settings->gameCyclesPerSecond);
            break;
        case CANCEL:
            // discard settings
            settingsChanged = *tu4.settings;
            break;
        default: break;
        }
    }

    // draw the extended background for all option screens
}

void IntroController::updateGameplayMenu(MenuEvent &event) {
    if (event.type == MenuEvent::ACTIVATE ||
        event.type == MenuEvent::INCREMENT ||
        event.type == MenuEvent::DECREMENT) {

        switch(event.item->getId()) {
        case USE_SETTINGS:
            // save settings
            tu4.settings->setData(settingsChanged);
            tu4.settings->write();
            break;
        case CANCEL:
            // discard settings
            settingsChanged = *tu4.settings;
            break;
        default: break;
        }
    }

    // draw the extended background for all option screens
}

void IntroController::updateInterfaceMenu(MenuEvent &event) {
    if (event.type == MenuEvent::ACTIVATE ||
        event.type == MenuEvent::INCREMENT ||
        event.type == MenuEvent::DECREMENT) {

        switch(event.item->getId()) {
            case USE_SETTINGS:
                // save settings
                tu4.settings->setData(settingsChanged);
                tu4.settings->write();
                break;
            case CANCEL:
                // discard settings
                settingsChanged = *tu4.settings;
                break;
            default: break;
        }
    }

    // draw the extended background for all option screens

    // after drawing the menu, extra menu text can be added here
    extendedMenuArea.textAt(2, 3, "  (Open, Jimmy, etc.)");
}

/**
 * Initializes the question tree.  The tree starts off with the first
 * eight entries set to the numbers 0-7 in a random order.
 */
void IntroController::initQuestionTree() {
    int i, tmp, r;

    for (i = 0; i < 8; i++)
        questionTree[i] = i;

    for (i = 0; i < 8; i++) {
        r = tu4_random(8);
        tmp = questionTree[r];
        questionTree[r] = questionTree[i];
        questionTree[i] = tmp;
    }
    answerInd = 8;

    if (questionTree[0] > questionTree[1]) {
        tmp = questionTree[0];
        questionTree[0] = questionTree[1];
        questionTree[1] = tmp;
    }

}

/**
 * Updates the question tree with the given answer, and advances to
 * the next round.
 * @return true if all questions have been answered, false otherwise
 */
bool IntroController::doQuestion(int answer) {
    if (!answer)
        questionTree[answerInd] = questionTree[questionRound * 2];
    else
        questionTree[answerInd] = questionTree[questionRound * 2 + 1];

    drawAbacusBeads(questionRound, questionTree[answerInd],
                    questionTree[questionRound * 2 + ((answer) ? 0 : 1)]);

    answerInd++;
    questionRound++;

    if (questionRound > 6)
        return true;

    if (questionTree[questionRound * 2] > questionTree[questionRound * 2 + 1]) {
        int tmp = questionTree[questionRound * 2];
        questionTree[questionRound * 2] = questionTree[questionRound * 2 + 1];
        questionTree[questionRound * 2 + 1] = tmp;
    }

    return false;
}

/**
 * Build the initial avatar player record from the answers to the
 * gypsy's questions.
 */
void IntroController::initPlayers(SaveGame *saveGame) {
    int i, p;
    static const struct {
        WeaponType weapon;
        ArmorType armor;
        int level, xp, x, y;
    } initValuesForClass[] = {
        { WEAP_STAFF,  ARMR_CLOTH,   2, 125, 231, 136 }, /* CLASS_MAGE */
        { WEAP_SLING,  ARMR_CLOTH,   3, 240,  83, 105 }, /* CLASS_BARD */
        { WEAP_AXE,    ARMR_LEATHER, 3, 205,  35, 221 }, /* CLASS_FIGHTER */
        { WEAP_DAGGER, ARMR_CLOTH,   2, 175,  59,  44 }, /* CLASS_DRUID */
        { WEAP_MACE,   ARMR_LEATHER, 2, 110, 158,  21 }, /* CLASS_TINKER */
        { WEAP_SWORD,  ARMR_CHAIN,   3, 325, 105, 183 }, /* CLASS_PALADIN */
        { WEAP_SWORD,  ARMR_LEATHER, 2, 150,  23, 129 }, /* CLASS_RANGER */
        { WEAP_STAFF,  ARMR_CLOTH,   1,   5, 186, 171 }  /* CLASS_SHEPHERD */
    };
    static const struct {
        const char *name;
        int str, dex, intel;
        SexType sex;
    } initValuesForNpcClass[] = {
        { "Mariah",    9, 12, 20, SEX_FEMALE }, /* CLASS_MAGE */
        { "Iolo",     16, 19, 13, SEX_MALE },   /* CLASS_BARD */
        { "Geoffrey", 20, 15, 11, SEX_MALE },   /* CLASS_FIGHTER */
        { "Jaana",    17, 16, 13, SEX_FEMALE }, /* CLASS_DRUID */
        { "Julia",    15, 16, 12, SEX_FEMALE }, /* CLASS_TINKER */
        { "Dupre",    17, 14, 17, SEX_MALE },   /* CLASS_PALADIN */
        { "Shamino",  16, 15, 15, SEX_MALE },   /* CLASS_RANGER */
        { "Katrina",  11, 12, 10, SEX_FEMALE }  /* CLASS_SHEPHERD */
    };

    saveGame->players[0].klass = static_cast<ClassType>(questionTree[14]);

    ASSERT(saveGame->players[0].klass < 8, "bad class: %d", saveGame->players[0].klass);

    saveGame->players[0].weapon = initValuesForClass[saveGame->players[0].klass].weapon;
    saveGame->players[0].armor = initValuesForClass[saveGame->players[0].klass].armor;
    saveGame->players[0].xp = initValuesForClass[saveGame->players[0].klass].xp;
    saveGame->x = initValuesForClass[saveGame->players[0].klass].x;
    saveGame->y = initValuesForClass[saveGame->players[0].klass].y;

    saveGame->players[0].str = 15;
    saveGame->players[0].dex = 15;
    saveGame->players[0].intel = 15;

    for (i = 0; i < VIRT_MAX; i++)
        saveGame->karma[i] = 50;

    for (i = 8; i < 15; i++) {
        saveGame->karma[questionTree[i]] += 5;
        switch (questionTree[i]) {
        case VIRT_HONESTY:
            saveGame->players[0].intel += 3;
            break;
        case VIRT_COMPASSION:
            saveGame->players[0].dex += 3;
            break;
        case VIRT_VALOR:
            saveGame->players[0].str += 3;
            break;
        case VIRT_JUSTICE:
            saveGame->players[0].intel++;
            saveGame->players[0].dex++;
            break;
        case VIRT_SACRIFICE:
            saveGame->players[0].dex++;
            saveGame->players[0].str++;
            break;
        case VIRT_HONOR:
            saveGame->players[0].intel++;
            saveGame->players[0].str++;
            break;
        case VIRT_SPIRITUALITY:
            saveGame->players[0].intel++;
            saveGame->players[0].dex++;
            saveGame->players[0].str++;
            break;
        case VIRT_HUMILITY:
            /* no stats for you! */
            break;
        }
    }

    PartyMember player(NULL, &saveGame->players[0]);
    saveGame->players[0].hp = saveGame->players[0].hpMax = player.getMaxLevel() * 100;
    saveGame->players[0].mp = player.getMaxMp();

    p = 1;
    for (i = 0; i < VIRT_MAX; i++) {
        player = PartyMember(NULL, &saveGame->players[i]);

        /* Initial setup for party members that aren't in your group yet... */
        if (i != saveGame->players[0].klass) {
            saveGame->players[p].klass = static_cast<ClassType>(i);
            saveGame->players[p].xp = initValuesForClass[i].xp;
            saveGame->players[p].str = initValuesForNpcClass[i].str;
            saveGame->players[p].dex = initValuesForNpcClass[i].dex;
            saveGame->players[p].intel = initValuesForNpcClass[i].intel;
            saveGame->players[p].weapon = initValuesForClass[i].weapon;
            saveGame->players[p].armor = initValuesForClass[i].armor;
            strcpy(saveGame->players[p].name, initValuesForNpcClass[i].name);
            saveGame->players[p].sex = initValuesForNpcClass[i].sex;
            saveGame->players[p].hp = saveGame->players[p].hpMax = initValuesForClass[i].level * 100;
            saveGame->players[p].mp = player.getMaxMp();
            p++;
        }
    }
}


/**
 * Preload map tiles
 */
void IntroController::preloadMap()
{
    int x, y, i;

    // draw unmodified map
    for (y = 0; y < INTRO_MAP_HEIGHT; y++)
        for (x = 0; x < INTRO_MAP_WIDTH; x++)
            mapArea.loadTile(binData->introMap[x + (y * INTRO_MAP_WIDTH)]);

    // draw animated objects
    for (i = 0; i < IntroBinData::INTRO_BASETILE_TABLE_SIZE; i++) {
        if (objectStateTable[i].tile != 0)
            mapArea.loadTile(objectStateTable[i].tile);
    }
}

//
// Title Animation - Text Mode (data-driven from XML subimage coordinates)
// Reads animation region positions from graphics-text.xml subimages on TITLE.ASP.
// Delay/duration are hardcoded; positions are per-theme.
//

// Title screen ASP data (loaded once in init, used by animation)
static uint8_t titleAspData[8000];
static int sigCoords[1000][2];
static int numSigCoords = 0;
static bool titleDataLoaded = false;

// Invalidate the cached title-screen art (titleAspData / titleReg) so it is
// re-read from the current scheme's TITLE.ASP on the next loadTitleData().
// Called after a text-style change reloads all graphics assets.
void titleDataLoadedReset() {
    titleDataLoaded = false;
}

// Animation region coordinates (0-based, loaded from 1-based XML subimages)
struct TitleRegion {
    int x, y, w, h;
};
static TitleRegion titleReg[7];  // signature, and, bar, origin, present, title, subtitle

enum {
    TR_SIGNATURE = 0,
    TR_AND,
    TR_BAR,
    TR_ORIGIN,
    TR_PRESENT,
    TR_TITLE,
    TR_SUBTITLE
};

static void loadTitleData() {
    if (titleDataLoaded) return;

    // Load TITLE.ASP image data
    ImageInfo* info = tu4.imageMgr->get(BKGD_INTRO);
    if (info && info->image && info->image->getAspData()) {
        memcpy(titleAspData, info->image->getAspData(), 8000);
    } else {
        memset(titleAspData, 0, 8000);
    }

    // Load subimage coordinates (1-based in XML, convert to 0-based)
    static const char* subNames[7] = {
        "intro_signature", "intro_and", "intro_bar", "intro_origin",
        "intro_present", "intro_title", "intro_subtitle"
    };

    if (info) {
        for (int i = 0; i < 7; i++) {
            Symbol sym = tu4.config->intern(subNames[i]);
            std::map<Symbol, int>::iterator it = info->subImageIndex.find(sym);
            if (it != info->subImageIndex.end()) {
                const SubImage* sub = info->subImages + it->second;
                titleReg[i].x = sub->x - 1;      // convert 1-based to 0-based
                titleReg[i].y = sub->y - 1;
                titleReg[i].w = sub->width;
                titleReg[i].h = sub->height;
            } else {
                titleReg[i] = {0, 0, ASP_SCREEN_COLS, 1};  // fallback
            }
        }
    } else {
        // Hardcoded fallback (0-based)
        titleReg[TR_SIGNATURE] = {0, 1, ASP_SCREEN_COLS, 3};
        titleReg[TR_AND]       = {0, 4, ASP_SCREEN_COLS, 1};
        titleReg[TR_BAR]       = {20, 8, 41, 1};
        titleReg[TR_ORIGIN]    = {0, 5, ASP_SCREEN_COLS, 3};
        titleReg[TR_PRESENT]   = {37, 9, 8, 1};
        titleReg[TR_TITLE]     = {0, 9, ASP_SCREEN_COLS, 11};
        titleReg[TR_SUBTITLE]  = {0, 20, ASP_SCREEN_COLS, 4};
    }

    // Load sigdata.txt from the ACTIVE theme's directory, searching the same
    // resource paths as the theme's .ASP assets (u4find_graphics ->
    // graphics/<theme>/ under ., ~/.local/share/tu4, /usr/share/tu4, ...).
    std::string sigPath = u4find_graphics(tu4.settings->textStyle + "/sigdata.txt");
    FILE* f = sigPath.empty() ? NULL : fopen(sigPath.c_str(), "r");
    numSigCoords = 0;
    if (f) {
        char line[64];
        while (fgets(line, sizeof(line), f) && numSigCoords < 1000) {
            int x, y;
            char *dot = strchr(line, '.');
            if (dot && !strchr(line, ','))
                *dot = ',';
            if (sscanf(line, "%d,%d", &x, &y) == 2) {
                sigCoords[numSigCoords][0] = x;
                sigCoords[numSigCoords][1] = y;
                numSigCoords++;
            }
        }
        fclose(f);
    }

    titleDataLoaded = true;
}

// Helper: draw a character from TITLE.ASP data at screen position (0-based)
static void titlePutCell(int col, int row) {
    if (col < 0 || col >= SCREEN_COLS || row < 0 || row >= SCREEN_ROWS) return;
    int idx = (row * ASP_SCREEN_COLS + col) * 2;
    screenPutChar(col, row, titleAspData[idx], titleAspData[idx + 1]);
}

// Helper: check if a cell in TITLE.ASP has visible content (not plain
// background). A cell is treated as background when it renders fully
// black, which covers every title-screen background encoding we ship:
//   - space 0x20 with a black background nibble (any fg; the glyph is
//     empty so fg colour never shows) -- includes the old 0x20/attr 7/bg 0
//     convention and the new Playscii gray-on-black 0x20/0x07 fill;
//   - full block 0xDB with a black foreground nibble (FMTITLE.ASP uses
//     0xDB/0x00 for its black background);
//   - any char whose fg and bg are both black.
// Without this, opaque-black (0xDB/0x00) title backgrounds were counted
// as content, overflowing the scatter buffer and revealing only ~half
// the actual title before the cell cap was hit.
static bool titleCellVisible(int col, int row) {
    int idx = (row * ASP_SCREEN_COLS + col) * 2;
    uint8_t ch = titleAspData[idx];
    uint8_t attr = titleAspData[idx + 1];
    uint8_t fg = attr & 0x0F;
    uint8_t bg = (attr >> 4) & 0x0F;
    if (ch == 0x20 && bg == 0)   return false;  // space -> only bg shows
    if (ch == 0xDB && fg == 0)   return false;  // full block -> only fg shows
    if (fg == 0 && bg == 0)      return false;  // both black
    return true;
}

/**
 * Sleep for msec while routing input to this IntroController (so any
 * keypress triggers keyPressed() -> skipTitles(), matching introt.c's
 * CHECK_TITLE() polling behavior). Returns true if the game is exiting.
 */
bool IntroController::titleWait(unsigned int msec) {
    return EventHandler::wait_msecs(msec, this);
}

void IntroController::initTitles()
{
    loadTitleData();

    // Clear screen to black
    for (int row = 0; row < SCREEN_ROWS; row++)
        for (int col = 0; col < SCREEN_COLS; col++)
            screenPutChar(col, row, ' ', 0x00);

    screenSwapBuffers();
}

/**
 * Play the full title animation sequence, blocking until it completes
 * or the user quits. Matches introt.c timing exactly: each element is
 * drawn incrementally with a short delay and screen present after
 * every step, rather than being paced by the coarser ~250ms game-cycle
 * timer tick.
 *
 * Returns true when the game is exiting (propagated from wait_msecs),
 * false on normal completion.
 */
bool IntroController::updateTitle()
{
    loadTitleData();

    /* --- SIGNATURE: reveal one coord at a time --- */
    {
        /* Signature drawing duration: adjust the numerator (ms) to set total
         * duration. With vsync off, total ≈ numerator ms. With vsync on,
         * each point takes at least ~16ms so actual duration will be longer. */
        int delay_ms = numSigCoords ? (2100 / numSigCoords) : 1;
        if (delay_ms < 1) delay_ms = 1;

        for (int i = 0; i < numSigCoords; i++) {
            int x = sigCoords[i][0] - 1;  // 1-based to 0-based
            int y = sigCoords[i][1] - 1;
            titlePutCell(x, y);
            screenSwapBuffers();
            if (bSkipTitles) break;
            if (titleWait(delay_ms)) return true;
        }
    }

    if (!bSkipTitles) {
        if (titleWait(1000)) return true;
    }

    /* --- AND: instant reveal --- */
    {
        const TitleRegion& r = titleReg[TR_AND];
        for (int row = r.y; row < r.y + r.h; row++)
            for (int col = r.x; col < r.x + r.w; col++)
                if (titleCellVisible(col, row))
                    titlePutCell(col, row);
        screenSwapBuffers();
    }

    if (!bSkipTitles) {
        if (titleWait(1000)) return true;
    }

    /* --- BAR: animate left to right, one column at a time --- */
    if (!bSkipTitles) {
        const TitleRegion& r = titleReg[TR_BAR];
        for (int i = 0; i < r.w; i++) {
            int col = r.x + i;
            for (int row = r.y; row < r.y + r.h; row++)
                if (titleCellVisible(col, row))
                    titlePutCell(col, row);
            screenSwapBuffers();
            if (bSkipTitles) break;
            if (titleWait(12)) return true;
        }
    } else {
        const TitleRegion& r = titleReg[TR_BAR];
        for (int row = r.y; row < r.y + r.h; row++)
            for (int col = r.x; col < r.x + r.w; col++)
                if (titleCellVisible(col, row))
                    titlePutCell(col, row);
        screenSwapBuffers();
    }

    /* --- ORIGIN: slide up from bottom, one row-step at a time.
       PRESENT appears together with the final step (matching the
       original DOS intro where both animations complete at once). --- */
    if (!bSkipTitles) {
        if (titleWait(1000)) return true;

        const TitleRegion& r = titleReg[TR_ORIGIN];
        const TitleRegion& rp = titleReg[TR_PRESENT];
        for (int step = 1; step <= r.h; step++) {
            for (int row = r.y; row < r.y + r.h; row++)
                for (int col = r.x; col < r.x + r.w; col++)
                    screenPutChar(col, row, ' ', 0x00);

            for (int src = 0; src < step; src++) {
                int body_row = r.y + src;
                int dst_row = r.y + r.h - step + src;
                for (int col = r.x; col < r.x + r.w; col++)
                    if (titleCellVisible(col, body_row))
                        titlePutCell(col, dst_row);
            }

            /* Reveal PRESENT alongside the final ORIGIN step */
            if (step == r.h) {
                for (int row = rp.y; row < rp.y + rp.h; row++)
                    for (int col = rp.x; col < rp.x + rp.w; col++)
                        titlePutCell(col, row);
            }

            screenSwapBuffers();
            if (bSkipTitles) break;
            if (titleWait(100)) return true;
        }
    }
    {
        const TitleRegion& r = titleReg[TR_ORIGIN];
        for (int row = r.y; row < r.y + r.h; row++)
            for (int col = r.x; col < r.x + r.w; col++)
                if (titleCellVisible(col, row))
                    titlePutCell(col, row);
        screenSwapBuffers();
    }

    /* --- PRESENT: ensure it's drawn even if titles were skipped
       or the ORIGIN loop above didn't run --- */
    {
        const TitleRegion& rp = titleReg[TR_PRESENT];
        for (int row = rp.y; row < rp.y + rp.h; row++)
            for (int col = rp.x; col < rp.x + rp.w; col++)
                titlePutCell(col, row);
        screenSwapBuffers();
    }

    if (!bSkipTitles) {
        if (titleWait(100)) return true;
        soundPlay(SOUND_TITLE_FADE);
    }

    /* --- TITLE: ULTIMA IV, random scatter reveal over ~5 seconds --- */
    if (!bSkipTitles) {
        const TitleRegion& rp = titleReg[TR_PRESENT];
        const TitleRegion& rt = titleReg[TR_TITLE];
        /* Buffer sized for the full 80x12 title region so no theme can
           overflow the scatter cap (background cells are excluded by
           titleCellVisible, so this is only ever partially filled). */
        static int titleCells[960][2];
        int numTitleCells = 0;

        for (int row = rt.y; row < rt.y + rt.h; row++) {
            for (int col = rt.x; col < rt.x + rt.w; col++) {
                if (row >= rp.y && row < rp.y + rp.h &&
                    col >= rp.x && col < rp.x + rp.w)
                    continue;
                if (titleCellVisible(col, row)) {
                    titleCells[numTitleCells][0] = col;
                    titleCells[numTitleCells][1] = row;
                    numTitleCells++;
                    if (numTitleCells >= 960) break;
                }
            }
            if (numTitleCells >= 960) break;
        }

        /* Fisher-Yates shuffle, fixed seed for reproducibility */
        srand(42);
        for (int i = numTitleCells - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            int tmp0 = titleCells[i][0], tmp1 = titleCells[i][1];
            titleCells[i][0] = titleCells[j][0]; titleCells[i][1] = titleCells[j][1];
            titleCells[j][0] = tmp0; titleCells[j][1] = tmp1;
        }

        uint32_t startTime = getTicks();
        int duration = 5000;
        int revealed = 0;

        while (revealed < numTitleCells) {
            uint32_t elapsed = getTicks() - startTime;
            int target = (elapsed >= (uint32_t)duration)
                            ? numTitleCells
                            : (int)((long long)numTitleCells * elapsed / duration);

            while (revealed < target && revealed < numTitleCells) {
                titlePutCell(titleCells[revealed][0], titleCells[revealed][1]);
                revealed++;
            }

            /* Re-draw PRESENT on top (shares row space with title) */
            for (int row = rp.y; row < rp.y + rp.h; row++)
                for (int col = rp.x; col < rp.x + rp.w; col++)
                    titlePutCell(col, row);

            screenSwapBuffers();
            if (bSkipTitles) break;
            if (titleWait(30)) return true;
        }
    }
    {
        const TitleRegion& rt = titleReg[TR_TITLE];
        for (int row = rt.y; row < rt.y + rt.h; row++)
            for (int col = rt.x; col < rt.x + rt.w; col++)
                if (titleCellVisible(col, row))
                    titlePutCell(col, row);
        screenSwapBuffers();
    }

    /* --- SUBTITLE: expand from center, top/bottom halves spread apart --- */
    if (!bSkipTitles) {
        if (titleWait(1000)) return true;

        const TitleRegion& r = titleReg[TR_SUBTITLE];
        int center = r.y + r.h / 2;

        for (int step = 1; step <= r.h; step++) {
            int topH = (step + 1) / 2;
            int botH = step / 2;

            for (int row = r.y; row < r.y + r.h; row++)
                for (int col = r.x; col < r.x + r.w; col++)
                    screenPutChar(col, row, ' ', 0x00);

            for (int src = 0; src < topH; src++) {
                int body_row = r.y + src;
                int dst_row = center - topH + src;
                if (dst_row < r.y) dst_row = r.y;
                for (int col = r.x; col < r.x + r.w; col++)
                    if (titleCellVisible(col, body_row))
                        titlePutCell(col, dst_row);
            }
            for (int src = 0; src < botH; src++) {
                int body_row = r.y + r.h - botH + src;
                int dst_row = center + src;
                if (dst_row >= r.y + r.h) dst_row = r.y + r.h - 1;
                for (int col = r.x; col < r.x + r.w; col++)
                    if (titleCellVisible(col, body_row))
                        titlePutCell(col, dst_row);
            }
            screenSwapBuffers();
            if (bSkipTitles) break;
            if (titleWait(100)) return true;
        }
    }
    {
        const TitleRegion& r = titleReg[TR_SUBTITLE];
        for (int row = r.y; row < r.y + r.h; row++)
            for (int col = r.x; col < r.x + r.w; col++)
                if (titleCellVisible(col, row))
                    titlePutCell(col, row);
        screenSwapBuffers();
    }

    /* --- MAP: border expands from center outward, revealing map tiles --- */
    if (!bSkipTitles) {
        if (titleWait(1000)) return true;

        const int mapOx = 2, mapOy = 26;      // matches mapArea position
        const int mapW = INTRO_MAP_WIDTH * 4; // 76 chars
        const int mapCenterCol = mapOx + mapW / 2;
        const int totalSteps = mapW / 2;      // 38 steps
        const int borderTop = 24, borderBot = 47;

        preloadMap();

        for (int step = 1; step <= totalSteps; step++) {
            int revealLeft = mapCenterCol - step;
            int revealRight = mapCenterCol + step - 1;

            // Draw map tiles within the reveal window, clipped column-by-
            // column to the window edges (matching introt.c exactly --
            // NOT a coarse "skip whole tile" check, since a tile can be
            // partially inside the window while the reveal is expanding).
            {
                ImageInfo* shapesInfo = tu4.imageMgr->get(BKGD_SHAPES);
                const uint8_t* shapes = (shapesInfo && shapesInfo->image)
                                            ? shapesInfo->image->getAspData() : NULL;
                if (shapes) {
                    for (int my = 0; my < INTRO_MAP_HEIGHT; my++) {
                        for (int mx = 0; mx < INTRO_MAP_WIDTH; mx++) {
                            int tileLeft = mapOx + mx * 4;
                            int tileRight = tileLeft + 3;
                            if (tileRight < revealLeft || tileLeft > revealRight)
                                continue;

                            const MapTile &mt = binData->introMap[mx + my * INTRO_MAP_WIDTH];
                            uint8_t uid = tu4.config->usaveIds()->ultimaId(mt);
                            const uint8_t* tileData = shapes + (uid * 32);

                            for (int r = 0; r < 4; r++) {
                                for (int c = 0; c < 4; c++) {
                                    int screenCol = tileLeft + c;
                                    if (screenCol < revealLeft || screenCol > revealRight)
                                        continue;
                                    int bi = r * 8 + c * 2;
                                    screenPutChar(screenCol, mapOy + my * 4 + r,
                                                 tileData[bi], tileData[bi + 1]);
                                }
                            }
                        }
                    }
                }
            }

            // Draw border at current reveal edges: copy from the FIXED
            // border source columns (0-1 left, 78-79 right) in TITLE.ASP
            // to the MOVING destination position bl/br (matching introt.c).
            int bl = revealLeft - 2;
            int br = revealRight + 1;
            for (int row = borderTop; row <= borderBot; row++) {
                if (bl >= 0) {
                    int idx0 = (row * ASP_SCREEN_COLS + 0) * 2;
                    int idx1 = (row * ASP_SCREEN_COLS + 1) * 2;
                    screenPutChar(bl,     row, titleAspData[idx0], titleAspData[idx0 + 1]);
                    screenPutChar(bl + 1, row, titleAspData[idx1], titleAspData[idx1 + 1]);
                }
                if (br + 1 < SCREEN_COLS) {
                    int idx0 = (row * ASP_SCREEN_COLS + (ASP_SCREEN_COLS - 2)) * 2;
                    int idx1 = (row * ASP_SCREEN_COLS + (ASP_SCREEN_COLS - 1)) * 2;
                    screenPutChar(br,     row, titleAspData[idx0], titleAspData[idx0 + 1]);
                    screenPutChar(br + 1, row, titleAspData[idx1], titleAspData[idx1 + 1]);
                }
            }
            // Top/bottom border rows between the side borders (drawn from
            // their own fixed source rows, same rows since they don't move)
            for (int col = bl + 2; col < br && col < SCREEN_COLS; col++) {
                if (col < 0) continue;
                titlePutCell(col, borderTop);
                titlePutCell(col, borderTop + 1);
                titlePutCell(col, borderBot - 1);
                titlePutCell(col, borderBot);
            }

            screenSwapBuffers();
            if (bSkipTitles) break;
            if (titleWait(100)) return true;
        }

        // Final: draw the complete border frame
        for (int row = borderTop; row <= borderBot; row++) {
            for (int col = 0; col < SCREEN_COLS; col++) {
                if (col <= 1 || col >= SCREEN_COLS - 2 || row <= borderTop + 1 || row >= borderBot - 1)
                    titlePutCell(col, row);
            }
        }
        screenSwapBuffers();
    }

    titleDataLoaded = false;
    return false;
}


//
// skip the remaining titles
//
void IntroController::skipTitles()
{
    bSkipTitles = true;
    soundStop();

    // Immediately draw the full title screen (all regions)
    loadTitleData();
    int maxRow = titleReg[TR_SUBTITLE].y + titleReg[TR_SUBTITLE].h;
    for (int row = 0; row < maxRow && row < SCREEN_ROWS; row++)
        for (int col = 0; col < SCREEN_COLS; col++)
            titlePutCell(col, row);
    screenSwapBuffers();
}

#ifdef IOS
// Try to put the intro music back at just the correct moment on iOS;
// don't play it at the very beginning.
void IntroController::tryTriggerIntroMusic() {
    if (mode == INTRO_MAP)
        musicPlay(introMusic);
}
#endif
