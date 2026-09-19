/*
 * sound_sdl2.cpp - Audio backend using SDL2_mixer
 *
 * Handles music playback (.it, .mid, .mp3) and sound effects (.ogg).
 * Based on the existing sound_sdl.cpp, updated for SDL2/SDL2_mixer API.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include "sound.h"
#include "config.h"
#include "context.h"
#include "debug.h"
#include "error.h"
#include "event.h"
#include "settings.h"
#include "xu4.h"

// Use Channel 1 for sound effects
#define FX_CHANNEL  1
#define NLOOPS -1

static bool audioFunctional = false;
static bool musicEnabled = false;
static int currentTrack;
static Mix_Music* playing = NULL;
static std::vector<Mix_Chunk *> soundChunk;

/*
 * Initialize sound & music service.
 */
int soundInit(void)
{
    int audio_rate = 22050;
    Uint16 audio_format = AUDIO_S16LSB;
    int audio_channels = 2;
    int audio_buffers = 1024;

    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            errorWarning("unable to init SDL audio subsystem: %s", SDL_GetError());
            audioFunctional = false;
            return 0;
        }
    }

    if (Mix_OpenAudio(audio_rate, audio_format, audio_channels,
                      audio_buffers) < 0) {
        errorWarning("Unable to open audio: %s", Mix_GetError());
        audioFunctional = false;
        return 0;
    }
    audioFunctional = true;

    Mix_AllocateChannels(16);

    /* Set up the volume */
    musicEnabled = xu4.settings->musicVol > 0;
    musicSetVolume(xu4.settings->musicVol);
    soundSetVolume(xu4.settings->soundVol);

    soundChunk.resize(SOUND_MAX, NULL);

    currentTrack = MUSIC_NONE;
    playing = NULL;

    return 1;
}

/**
 * Callback to restart music if it stopped.
 */
static void music_callback(void *data) {
    (void)data;
    xu4.eventHandler->getTimer()->remove(&music_callback);

    bool mplaying = Mix_PlayingMusic();
    if (musicEnabled) {
        if (!mplaying)
            musicPlayLocale();
    } else {
        if (mplaying)
            Mix_HaltMusic();
    }
}

void soundDelete(void)
{
    if (!audioFunctional)
        return;

    xu4.eventHandler->getTimer()->remove(&music_callback);

    /* Free sound effects */
    for (size_t i = 0; i < soundChunk.size(); i++) {
        if (soundChunk[i]) {
            Mix_FreeChunk(soundChunk[i]);
            soundChunk[i] = NULL;
        }
    }
    soundChunk.clear();

    if (playing) {
        Mix_FreeMusic(playing);
        playing = NULL;
    }

    Mix_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audioFunctional = false;
}

static bool sound_load(Sound sound) {
    if (soundChunk[sound] == NULL) {
        const char* pathname = xu4.config->soundFile(sound);
        if (pathname) {
            soundChunk[sound] = Mix_LoadWAV(pathname);
            if (!soundChunk[sound]) {
                errorWarning("Unable to load sound file %s: %s",
                             pathname, Mix_GetError());
                return false;
            }
        }
    }
    return true;
}

void soundPlay(Sound sound, bool onlyOnce, int specificDurationInTicks) {
    if (sound >= SOUND_MAX)
        return;
    if (!audioFunctional || !xu4.settings->soundVol)
        return;

    if (soundChunk[sound] == NULL) {
        if (!sound_load(sound))
            return;
    }

    if (!onlyOnce || !Mix_Playing(FX_CHANNEL)) {
        Mix_PlayChannelTimed(FX_CHANNEL, soundChunk[sound],
                    specificDurationInTicks == -1 ? 0 : -1,
                    specificDurationInTicks);
    }
}

void soundStop() {
    if (!audioFunctional || !xu4.settings->soundVol)
        return;
    if (Mix_Playing(FX_CHANNEL))
        Mix_HaltChannel(FX_CHANNEL);
}

static bool music_load(int music) {
    if (music >= MUSIC_MAX)
        return false;

    /* Already loaded and playing */
    if (music == currentTrack) {
        if (Mix_PlayingMusic())
            return false;
        else
            return true;
    }

    const char* pathname = xu4.config->musicFile(music);
    if (!pathname)
        return false;

    if (playing)
        Mix_FreeMusic(playing);
    playing = Mix_LoadMUS(pathname);
    if (!playing) {
        errorWarning("unable to load music file %s: %s", pathname, Mix_GetError());
        return false;
    }

    currentTrack = music;
    return true;
}

void musicPlay(int track)
{
    if (!audioFunctional || !musicEnabled)
        return;

    if (music_load(track))
        Mix_PlayMusic(playing, NLOOPS);
}

void musicPlayLocale()
{
    musicPlay(c->location->map->music);
}

void musicStop()
{
    if (audioFunctional)
        Mix_HaltMusic();
}

void musicFadeOut(int msec)
{
    if (!audioFunctional)
        return;

    if (Mix_PlayingMusic()) {
        if (xu4.settings->volumeFades) {
            Mix_FadeOutMusic(msec);
        } else
            Mix_HaltMusic();
    }
}

void musicFadeIn(int msec, bool loadFromMap)
{
    if (!audioFunctional || !musicEnabled)
        return;

    if (!Mix_PlayingMusic()) {
        if (loadFromMap || !playing)
            music_load(c->location->map->music);

        if (xu4.settings->volumeFades)
            Mix_FadeInMusic(playing, NLOOPS, msec);
        else
            musicPlayLocale();
    }
}

void musicSetVolume(int volume)
{
    if (audioFunctional)
        Mix_VolumeMusic(int((float)MIX_MAX_VOLUME / MAX_VOLUME * volume));
}

int musicVolumeDec()
{
    if (xu4.settings->musicVol > 0)
        musicSetVolume(--xu4.settings->musicVol);
    return (xu4.settings->musicVol * 100 / MAX_VOLUME);
}

int musicVolumeInc()
{
    if (xu4.settings->musicVol < MAX_VOLUME)
        musicSetVolume(++xu4.settings->musicVol);
    return (xu4.settings->musicVol * 100 / MAX_VOLUME);
}

bool musicToggle()
{
    if (!audioFunctional)
        return false;

    xu4.eventHandler->getTimer()->remove(&music_callback);

    musicEnabled = !musicEnabled;
    if (musicEnabled)
        musicFadeIn(1000, true);
    else
        musicFadeOut(1000);

    xu4.eventHandler->getTimer()->add(&music_callback, xu4.settings->gameCyclesPerSecond);
    return musicEnabled;
}

void soundSetVolume(int volume) {
    if (audioFunctional)
        Mix_Volume(FX_CHANNEL, int((float)MIX_MAX_VOLUME / MAX_VOLUME * volume));
}

int soundVolumeDec()
{
    if (xu4.settings->soundVol > 0)
        soundSetVolume(--xu4.settings->soundVol);
    return (xu4.settings->soundVol * 100 / MAX_VOLUME);
}

int soundVolumeInc()
{
    if (xu4.settings->soundVol < MAX_VOLUME)
        soundSetVolume(++xu4.settings->soundVol);
    return (xu4.settings->soundVol * 100 / MAX_VOLUME);
}
