/*
 * sound_sdl3.cpp - Audio backend using SDL3_mixer
 *
 * SDL3_mixer uses a completely different API from SDL2_mixer:
 * - MIX_Audio replaces both Mix_Chunk and Mix_Music
 * - MIX_Track replaces channels (allocated individually)
 * - MIX_CreateMixerDevice replaces Mix_OpenAudio
 * - MIX_LoadAudio replaces both Mix_LoadWAV and Mix_LoadMUS
 */

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include "sound.h"
#include "config.h"
#include "context.h"
#include "debug.h"
#include "error.h"
#include "event.h"
#include "settings.h"
#include "xu4.h"

static bool audioFunctional = false;
static bool musicEnabled = false;
static int currentTrack = MUSIC_NONE;

static MIX_Mixer* mixer = NULL;
static MIX_Track* musicTrack = NULL;
static MIX_Track* fxTrack = NULL;

static MIX_Audio* musicAudio = NULL;
static MIX_Audio* soundAudio[SOUND_MAX];

/*
 * Initialize sound & music service.
 */
int soundInit(void)
{
    if (!MIX_Init()) {
        errorWarning("MIX_Init failed: %s", SDL_GetError());
        audioFunctional = false;
        return 0;
    }

    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = 44100;

    mixer = MIX_CreateMixerDevice(&spec);
    if (!mixer) {
        errorWarning("MIX_CreateMixerDevice failed: %s", SDL_GetError());
        audioFunctional = false;
        return 0;
    }

    /* Create tracks: one for music, one for sound effects */
    musicTrack = MIX_CreateTrack(mixer);
    fxTrack = MIX_CreateTrack(mixer);

    if (!musicTrack || !fxTrack) {
        errorWarning("MIX_CreateTrack failed: %s", SDL_GetError());
        MIX_DestroyMixer(mixer);
        mixer = NULL;
        audioFunctional = false;
        return 0;
    }

    audioFunctional = true;

    /* Initialize sound effect slots */
    memset(soundAudio, 0, sizeof(soundAudio));
    musicAudio = NULL;

    /* Set up volume */
    musicEnabled = xu4.settings->musicVol > 0;
    musicSetVolume(xu4.settings->musicVol);
    soundSetVolume(xu4.settings->soundVol);

    return 1;
}

/**
 * Callback to restart music if it stopped.
 */
static void music_callback(void *data) {
    (void)data;
    xu4.eventHandler->getTimer()->remove(&music_callback);

    if (musicEnabled) {
        if (!MIX_TrackPlaying(musicTrack))
            musicPlayLocale();
    } else {
        if (MIX_TrackPlaying(musicTrack))
            MIX_StopTrack(musicTrack, 0);
    }
}

void soundDelete(void)
{
    if (!audioFunctional)
        return;

    xu4.eventHandler->getTimer()->remove(&music_callback);

    /* Free sound effects */
    for (int i = 0; i < SOUND_MAX; i++) {
        if (soundAudio[i]) {
            MIX_DestroyAudio(soundAudio[i]);
            soundAudio[i] = NULL;
        }
    }

    /* Free music */
    if (musicAudio) {
        MIX_DestroyAudio(musicAudio);
        musicAudio = NULL;
    }

    /* Destroy mixer (also destroys tracks) */
    if (mixer) {
        MIX_DestroyMixer(mixer);
        mixer = NULL;
        musicTrack = NULL;
        fxTrack = NULL;
    }

    MIX_Quit();
    audioFunctional = false;
}

static bool sound_load(Sound sound) {
    if (soundAudio[sound] == NULL) {
        const char* pathname = xu4.config->soundFile(sound);
        if (pathname) {
            soundAudio[sound] = MIX_LoadAudio(pathname);
            if (!soundAudio[sound]) {
                errorWarning("Unable to load sound file %s: %s",
                             pathname, SDL_GetError());
                return false;
            }
        }
    }
    return soundAudio[sound] != NULL;
}

void soundPlay(Sound sound, bool onlyOnce, int specificDurationInTicks) {
    (void)specificDurationInTicks;

    if (sound >= SOUND_MAX)
        return;
    if (!audioFunctional || !xu4.settings->soundVol)
        return;

    if (!sound_load(sound))
        return;

    if (!onlyOnce || !MIX_TrackPlaying(fxTrack)) {
        MIX_SetTrackAudio(fxTrack, soundAudio[sound]);
        MIX_PlayTrack(fxTrack, NULL);
    }
}

void soundStop() {
    if (!audioFunctional)
        return;
    if (MIX_TrackPlaying(fxTrack))
        MIX_StopTrack(fxTrack, 0);
}

static bool music_load(int music) {
    if (music >= MUSIC_MAX || music < 0)
        return false;

    /* Already loaded and playing */
    if (music == currentTrack) {
        if (MIX_TrackPlaying(musicTrack))
            return false;
        return true;
    }

    const char* pathname = xu4.config->musicFile(music);
    if (!pathname)
        return false;

    /* Free previous music */
    if (musicAudio) {
        MIX_StopTrack(musicTrack, 0);
        MIX_DestroyAudio(musicAudio);
        musicAudio = NULL;
    }

    musicAudio = MIX_LoadAudio(pathname);
    if (!musicAudio) {
        errorWarning("unable to load music file %s: %s", pathname, SDL_GetError());
        return false;
    }

    currentTrack = music;
    return true;
}

void musicPlay(int track)
{
    if (!audioFunctional || !musicEnabled)
        return;

    if (music_load(track)) {
        MIX_SetTrackAudio(musicTrack, musicAudio);
        /* Loop by setting loop property — use NULL props for defaults (loops) */
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);  /* -1 = infinite loop */
        MIX_PlayTrack(musicTrack, props);
        SDL_DestroyProperties(props);
    }
}

void musicPlayLocale()
{
    musicPlay(c->location->map->music);
}

void musicStop()
{
    if (audioFunctional && MIX_TrackPlaying(musicTrack))
        MIX_StopTrack(musicTrack, 0);
}

void musicFadeOut(int msec)
{
    if (!audioFunctional)
        return;

    if (MIX_TrackPlaying(musicTrack)) {
        if (xu4.settings->volumeFades) {
            /* Convert msec to frames for fade-out */
            int frames = MIX_TrackMSToFrames(musicTrack, msec);
            MIX_StopTrack(musicTrack, frames);
        } else {
            MIX_StopTrack(musicTrack, 0);
        }
    }
}

void musicFadeIn(int msec, bool loadFromMap)
{
    if (!audioFunctional || !musicEnabled)
        return;

    if (!MIX_TrackPlaying(musicTrack)) {
        if (loadFromMap || !musicAudio)
            music_load(c->location->map->music);

        if (musicAudio) {
            MIX_SetTrackAudio(musicTrack, musicAudio);

            SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
            if (xu4.settings->volumeFades) {
                int frames = MIX_TrackMSToFrames(musicTrack, msec);
                SDL_SetNumberProperty(props, MIX_PROP_PLAY_FADEIN_FRAMES_NUMBER, frames);
            }
            MIX_PlayTrack(musicTrack, props);
            SDL_DestroyProperties(props);
        }
    }
}

void musicSetVolume(int volume)
{
    if (audioFunctional && musicTrack) {
        float gain = (float)volume / (float)MAX_VOLUME;
        MIX_SetTrackGain(musicTrack, gain);
    }
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
    if (audioFunctional && fxTrack) {
        float gain = (float)volume / (float)MAX_VOLUME;
        MIX_SetTrackGain(fxTrack, gain);
    }
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
