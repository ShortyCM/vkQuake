/* Steam Audio renderer. Called with snd_mutex held, except Register/ClearMap/Shutdown.
 * The simulation worker owns a copy of geometry and never accesses engine state.
 * Copyright (C) 2026 vkQuake contributors. GPL-2.0-or-later. */
#ifndef SND_STEAMAUDIO_H
#define SND_STEAMAUDIO_H

#define STEAMAUDIO_FRAME_SIZE 512

extern cvar_t snd_renderer;

void	 S_SteamAudio_Register (void);
void	 S_SteamAudio_Update (void);
void	 S_SteamAudio_ClearMap (void);
void	 S_SteamAudio_Shutdown (void);
void	 S_SteamAudio_Reset (void);
void	 S_SteamAudio_StartSound (channel_t *channel);
qboolean S_SteamAudio_Active (void);
void	 S_SteamAudio_Mix (portable_samplepair_t *buffer, qboolean pause_loops);

#endif
