/* Steam Audio renderer. Called with snd_mutex held, except Register/ClearMap/Shutdown.
 * The simulation worker owns a copy of geometry and never accesses engine state.
 * Copyright (C) 2026 vkQuake contributors. GPL-2.0-or-later. */
#ifndef SND_STEAMAUDIO_H
#define SND_STEAMAUDIO_H

#define STEAMAUDIO_FRAME_SIZE	512
#define STEAMAUDIO_MAX_CHANNELS 8

typedef enum
{
	SND_RENDERER_STANDARD,
	SND_RENDERER_HRTF,
	SND_RENDERER_STEREO,
	SND_RENDERER_QUAD,
	SND_RENDERER_5_1,
	SND_RENDERER_7_1,
	SND_RENDERER_COUNT
} snd_renderer_t;

extern cvar_t snd_renderer;
extern cvar_t snd_steam_crossover;

snd_renderer_t S_SteamAudio_Renderer (void);
const char	  *S_SteamAudio_RendererName (snd_renderer_t renderer);
int			   S_SteamAudio_OutputChannels (void);
int			   S_SteamAudio_Crossover (void);

void	 S_SteamAudio_Register (void);
void	 S_SteamAudio_Update (void);
void	 S_SteamAudio_ClearMap (void);
void	 S_SteamAudio_Shutdown (void);
void	 S_SteamAudio_Reset (void);
void	 S_SteamAudio_StartSound (channel_t *channel);
qboolean S_SteamAudio_Active (void);
void	 S_SteamAudio_Mix (int buffer[STEAMAUDIO_FRAME_SIZE][STEAMAUDIO_MAX_CHANNELS], qboolean pause_loops);

#endif
