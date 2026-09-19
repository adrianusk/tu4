/*
 * tu4.cpp
 * Text-mode Ultima IV (80x50, 16-color, CP437)
 */

/** \mainpage tu4 Main Page
 *
 * \section intro_sec Introduction
 *
 * tu4 is a text-mode interface for xu4 (Ultima IV Recreated).
 * It uses an 80x50 character display with 16 EGA colors and CP437 charset.
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <ctime>
#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif
#include "xu4.h"
#include "config.h"
#include "debug.h"
#include "error.h"
#include "game.h"
#include "intro.h"
#include "progress_bar.h"
#include "screen.h"
#include "settings.h"
#include "u4file.h"
#include "sound.h"
#include "utils.h"

#if defined(_WIN32) && defined(DEBUG)
#include "win32console.c"
#endif

#ifdef DEBUG
extern int gameSave(const char*);
#endif

bool verbose = false;


enum OptionsFlag {
    OPT_FULLSCREEN = 1,
    OPT_NO_INTRO   = 2,
    OPT_NO_AUDIO   = 4,
    OPT_VERBOSE    = 8,
    OPT_RECORD     = 0x10,
    OPT_REPLAY     = 0x20,
    OPT_TEST_SAVE  = 0x80
};

struct Options {
    uint16_t flags;
    uint16_t used;
    uint32_t scale;
    const char* module;
    const char* profile;
    const char* recordFile;
};

#define strEqual(A,B)       (strcmp(A,B) == 0)
#define strEqualAlt(A,B,C)  (strEqual(A,B) || strEqual(A,C))

/*
 * Return non-zero if the program should continue.
 */
int parseOptions(Options* opt, int argc, char** argv) {
    int i;
    for (i = 0; i < argc; i++) {
        if (strEqualAlt(argv[i], "-s", "--scale"))
        {
            if (++i >= argc)
                goto missing_value;
            opt->scale = strtoul(argv[i], NULL, 0);
        }
#ifdef CONF_MODULE
        else if (strEqualAlt(argv[i], "-m", "--module"))
        {
            if (++i >= argc)
                goto missing_value;
            opt->module = argv[i];
        }
#endif
        else if (strEqualAlt(argv[i], "-p", "--profile"))
        {
            if (++i >= argc)
                goto missing_value;
            opt->profile = argv[i];
        }
        else if (strEqualAlt(argv[i], "-i", "--skip-intro"))
        {
            opt->flags |= OPT_NO_INTRO;
            opt->used  |= OPT_NO_INTRO;
        }
        else if (strEqualAlt(argv[i], "-v", "--verbose"))
        {
            opt->flags |= OPT_VERBOSE;
            opt->used  |= OPT_VERBOSE;
        }
        else if (strEqualAlt(argv[i], "-f", "--fullscreen"))
        {
            opt->flags |= OPT_FULLSCREEN;
            opt->used  |= OPT_FULLSCREEN;
        }
        else if (strEqualAlt(argv[i], "-q", "--quiet"))
        {
            opt->flags |= OPT_NO_AUDIO;
            opt->used  |= OPT_NO_AUDIO;
        }
        else if (strEqualAlt(argv[i], "-h", "--help"))
        {
            printf("tu4: Ultima IV Text Mode\n"
                   "v%s (%s)\n\n", VERSION, __DATE__ );
            printf(
            "Options:\n"
            "  -f, --fullscreen        Run in fullscreen mode.\n"
            "  -h, --help              Print this message and quit.\n"
            "  -i, --skip-intro        Skip the intro. and load the last saved game.\n"
#ifdef CONF_MODULE
            "  -m, --module <file>     Specify game module (default is Ultima-IV).\n"
#endif
            "  -p, --profile <string>  Use another set of settings and save files.\n"
            "  -q, --quiet             Disable audio.\n"
            "  -s, --scale <int>       Specify display scaling factor (1-5).\n"
            "  -v, --verbose           Enable verbose console output.\n"
#ifdef DEBUG
            "\nDEBUG Options:\n"
            "  -c, --capture <file>    Record user input.\n"
            "  -r, --replay <file>     Play using recorded input.\n"
            "      --test-save         Save to /tmp/tu4/ and quit.\n"
#endif
            "\n");

            return 0;
        }
#ifdef DEBUG
        else if (strEqualAlt(argv[i], "-c", "--capture"))
        {
            if (++i >= argc)
                goto missing_value;
            opt->recordFile = argv[i];
            opt->flags |= OPT_RECORD;
            opt->used  |= OPT_RECORD;
        }
        else if (strEqualAlt(argv[i], "-r", "--replay"))
        {
            if (++i >= argc)
                goto missing_value;
            opt->recordFile = argv[i];
            opt->flags |= OPT_REPLAY;
            opt->used  |= OPT_REPLAY;
        }
        else if (strEqual(argv[i], "--test-save"))
        {
            opt->flags |= OPT_TEST_SAVE;
        }
#endif
        else {
            errorFatal("Unrecognized argument: %s\n\n"
                   "Use --help for a list of supported arguments.", argv[i]);
            return 0;
        }
    }
    return 1;

missing_value:
    errorFatal("%s requires a value. See --help for more detail.", argv[i-1]);
    return 0;
}


#ifdef DEBUG
void servicesFree(XU4GameServices*);
#endif

/*
 * Check that required ASP files exist for a given theme.
 * Returns true if all required files are present, false otherwise.
 * Paths are derived from the textStyle setting in tu4rc.
 */
bool checkAssets(const std::string& textStyle) {
    static const char* required[] = {
        "MYSHAPES.ASP",
        "START.ASP",
        "TITLE.ASP",
        NULL
    };
    static const char* optional[] = {
        "TREE.ASP",
        "PORTAL.ASP",
        "OUTSIDE.ASP",
        "INSIDE.ASP",
        "WAGON.ASP",
        "GYPSY.ASP",
        "ABACUS.ASP",
        "HONCOM.ASP",
        "VALJUS.ASP",
        "SACHONOR.ASP",
        "SPIRHUM.ASP",
        "KEY7.ASP",
        "HONESTY.ASP",
        "COMPASSN.ASP",
        "VALOR.ASP",
        "JUSTICE.ASP",
        "SACRIFIC.ASP",
        "HONOR.ASP",
        "SPIRIT.ASP",
        "HUMILITY.ASP",
        "TRUTH.ASP",
        "LOVE.ASP",
        "COURAGE.ASP",
        "STONCRCL.ASP",
        "RUNE_0.ASP",
        "RUNE_1.ASP",
        "RUNE_2.ASP",
        "RUNE_3.ASP",
        "RUNE_4.ASP",
        "RUNE_5.ASP",
        NULL
    };

    /* Resolve files via the same resource-path search the image loader uses
     * (u4find_graphics -> graphicsPaths x rootResourcePaths), so generated
     * assets in ~/.local/share/tu4/graphics/<theme>/ and shipped assets in
     * /usr/share/tu4/graphics/<theme>/ are both found — not just CWD. */
    std::string themeSub = textStyle + "/";

    /* Check required files — if any missing, theme is not usable */
    for (int i = 0; required[i]; i++) {
        if (u4find_graphics(themeSub + required[i]).empty())
            return false;
    }

    /* Warn about missing optional files */
    for (int i = 0; optional[i]; i++) {
        if (u4find_graphics(themeSub + optional[i]).empty())
            printf("Warning: optional file missing: graphics/%s%s\n",
                   themeSub.c_str(), optional[i]);
    }

    return true;
}

void servicesInit(XU4GameServices* gs, Options* opt) {
    if (opt->flags & OPT_VERBOSE)
        verbose = true;

    if (!u4fsetup())
    {
        errorFatal(
            "tu4 requires the PC version of Ultima IV (freeware) to be present,\n"
            "but the game data was not found.\n"
            "\n"
            "Ultima IV is available for free (legal freeware) from:\n"
            "    https://www.gog.com/game/ultima_4\n"
            "\n"
            "Put EITHER the unzipped \"ultima4\" folder, OR the ultima4.zip file\n"
            "(the zip need NOT be unpacked), in one of these locations:\n"
#ifdef _WIN32
            "    - the folder you run tu4 from\n"
            "    - C:\\ , C:\\DOS , or C:\\GAMES\n"
            "\n"
            "Example: create C:\\GAMES\\ultima4\\ and copy the data files there,\n"
            "or drop ultima4.zip into the folder next to tu4.exe.\n"
#elif defined(__linux__)
            "    - the current directory you run tu4 from\n"
            "    - %s/.local/share/tu4/\n"
            "    - /usr/share/tu4/  or  /usr/local/share/tu4/\n"
            "\n"
            "Example (recommended for an installed package):\n"
            "    mkdir -p ~/.local/share/tu4\n"
            "    cp ultima4.zip ~/.local/share/tu4/\n"
            "  (or unzip it there as ~/.local/share/tu4/ultima4/)\n"
#else
            "    - the current directory you run tu4 from\n"
            "    - /usr/share/tu4/  or  /usr/local/share/tu4/\n"
            "\n"
            "Example: unzip it as /usr/local/share/tu4/ultima4/, or place\n"
            "ultima4.zip in the folder you run tu4 from.\n"
#endif
            "\n"
            "The optional u4upgrad.zip may be placed alongside it.\n"
#ifdef __linux__
            , getenv("HOME") ? getenv("HOME") : "$HOME"
#endif
            );
    }

    /* Setup the message bus early to make it available to other services. */
    notify_init(&gs->notifyBus, 8);

    /* initialize the settings */
    gs->settings = new Settings;
    gs->settings->init(opt->profile);

    /* Check ASP asset files based on textStyle from settings */
    if (!checkAssets(gs->settings->textStyle)) {
        if (gs->settings->textStyle != DEFAULT_TEXT_STYLE) {
            printf("Warning: theme \"%s\" not found, falling back to default \"%s\"\n",
                   gs->settings->textStyle.c_str(), DEFAULT_TEXT_STYLE);
            gs->settings->textStyle = DEFAULT_TEXT_STYLE;
        }
        if (!checkAssets(gs->settings->textStyle)) {
            /* U4 data is present (u4fsetup passed above), but the U4-derived
             * theme .ASP assets have not been generated yet. They are NOT
             * shipped; the user regenerates them from their own Ultima IV data
             * with tu4-setup. */
            errorFatal(
                "The \"%s\" theme's graphics have not been generated yet.\n"
                "\n"
                "tu4 ships only the art that cannot be derived from Ultima IV;\n"
                "the rest is generated from YOUR Ultima IV data. Run:\n"
                "\n"
                "    tu4-setup --all\n"
                "\n"
                "It finds your Ultima IV data automatically and writes the\n"
                "generated assets to ~/.local/share/tu4/graphics/%s/.\n"
                "Then start tu4 again.",
                DEFAULT_TEXT_STYLE, DEFAULT_TEXT_STYLE);
        }
    }

    /* update the settings based upon command-line arguments */
    if (opt->used & OPT_FULLSCREEN)
        gs->settings->fullscreen = (opt->flags & OPT_FULLSCREEN) ? true : false;
    if (opt->scale)
        gs->settings->scale = opt->scale;

    Debug::initGlobal("debug/global.txt");

    gs->config = configInit(opt->module ? opt->module : "Ultima-IV.mod");
    screenInit();
    Tile::initSymbols(gs->config);

    if (! (opt->flags & OPT_NO_AUDIO))
        soundInit();

    gs->eventHandler = new EventHandler(1000/gs->settings->gameCyclesPerSecond,
                            1000/gs->settings->screenAnimationFramesPerSecond);

#ifdef DEBUG
    if (opt->flags & OPT_REPLAY) {
        uint32_t seed = gs->eventHandler->replay(opt->recordFile);
        if (! seed) {
            servicesFree(gs);
            errorFatal("Cannot open recorded input from %s", opt->recordFile);
        }
        xu4_srandom(seed);
    } else if (opt->flags & OPT_RECORD) {
        uint32_t seed = time(NULL);
        if (! gs->eventHandler->beginRecording(opt->recordFile, seed)) {
            servicesFree(gs);
            errorFatal("Cannot open recording file %s", opt->recordFile);
        }
        xu4_srandom(seed);
    } else
#endif
        xu4_srandom(time(NULL));

    gs->stage = (opt->flags & OPT_NO_INTRO) ? StagePlay : StageIntro;
}

void servicesFree(XU4GameServices* gs) {
    delete gs->game;
    delete gs->intro;
    delete gs->saveGame;
    delete gs->eventHandler;
    soundDelete();
    screenDelete();
    configFree(gs->config);
    delete gs->settings;
    notify_free(&gs->notifyBus);
    u4fcleanup();
}

XU4GameServices xu4;


/*
 * Change the working directory to the directory containing this
 * executable, so relative asset paths (".", "graphics/", data files,
 * the window icon) resolve no matter where tu4 was launched from
 * (e.g. double-clicked from a file manager, where CWD is $HOME).
 * No-op on platforms without a known exe-path mechanism.
 */
static void chdirToExecutableDir(const char* argv0) {
#if defined(__linux__)
    (void)argv0;
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        /* strip the trailing "/tu4" component */
        char* slash = strrchr(buf, '/');
        if (slash) {
            *slash = '\0';
            if (chdir(buf) != 0 && verbose)
                fprintf(stderr, "chdir(%s) failed\n", buf);
        }
    }
#else
    (void)argv0;
#endif
}

int main(int argc, char *argv[]) {
#if defined(_WIN32) && defined(DEBUG)
    redirectIOToConsole();
#endif

    /*
     * Make all relative paths (".", "graphics/", data files, the window
     * icon) resolve regardless of the launch working directory -- e.g.
     * when the executable is double-clicked from a file manager, where
     * CWD is typically $HOME rather than the install dir. Change into the
     * directory that contains this executable before any asset/path setup.
     */
    chdirToExecutableDir(argv[0]);

    {
    Options opt;

    /* Parse arguments before setup in case the user only wants some help. */
    memset(&opt, 0, sizeof opt);
    if (! parseOptions(&opt, argc-1, argv+1))
        return 0;

    memset(&xu4, 0, sizeof xu4);
    servicesInit(&xu4, &opt);

#ifdef DEBUG
    if (opt.flags & OPT_TEST_SAVE) {
        int status;
        xu4.game = new GameController();
        if (xu4.game->initContext()) {
            gameSave("/tmp/tu4/");
            status = 0;
        } else {
            printf("initContext failed!\n");
            status = 1;
        }
        xu4.stage = StageExitGame;
        servicesFree(&xu4);
        return status;
    }
#endif
    }

    /* Text-mode progress bar: simple character-based display */
    {
        /* Draw a text progress bar at center of the screen */
        int barWidth = 30;
        int barX = (SCREEN_COLS - barWidth) / 2;
        int barY = 25;

        screenTextAt(barX + 10, barY - 1, "Loading...");

        /* Draw bar frame: [                              ] */
        screenTextAt(barX, barY, "[");
        screenTextAt(barX + barWidth + 1, barY, "]");

        /* Fill progress */
        for (int i = 0; i < barWidth; i++) {
            screenTextAt(barX + 1 + i, barY, "=");
        }
    }

    while( xu4.stage != StageExitGame )
    {
        if( xu4.stage == StageIntro ) {
            /* Show the introduction */
            if (! xu4.intro)
                xu4.intro = new IntroController;
            xu4.eventHandler->runController(xu4.intro);
        } else {
            /* Play the game! */
            if (! xu4.game)
                xu4.game = new GameController();
            xu4.eventHandler->runController(xu4.game);
        }
    }

    servicesFree(&xu4);
    return 0;
}
