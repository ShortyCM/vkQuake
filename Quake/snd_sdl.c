/*
 * snd_sdl.c - SDL audio driver for Hexen II: Hammer of Thyrion (uHexen2)
 * based on implementations found in the quakeforge and ioquake3 projects.
 *
 * Copyright (C) 1999-2005 Id Software, Inc.
 * Copyright (C) 2005-2012 O.Sezer <sezero@users.sourceforge.net>
 * Copyright (C) 2010-2014 QuakeSpasm developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "quakedef.h"

#ifndef USE_SDL3

static int buffersize;

static void SDLCALL paint_audio (void *unused, Uint8 *stream, int len)
{
	int pos;

	if (!shm)
	{ /* shouldn't happen, but just in case */
		memset (stream, 0, len);
		return;
	}

	pos = (shm->samplepos * (shm->samplebits / 8));
	if (pos >= buffersize)
		shm->samplepos = pos = 0;

	while (len > 0)
	{
		const int count = q_min (len, buffersize - pos);
		memcpy (stream, shm->buffer + pos, count);
		stream += count;
		len -= count;
		pos = (pos + count) % buffersize;
	}
	shm->samplepos = pos / (shm->samplebits / 8);
}

qboolean SNDDMA_Init (dma_t *dma)
{
	SDL_AudioSpec desired;
	int			  tmp;
	char		  drivername[128];

	if (SDL_InitSubSystem (SDL_INIT_AUDIO) < 0)
	{
		Con_Printf ("Couldn't init SDL audio: %s\n", SDL_GetError ());
		return false;
	}

	/* Set up the desired format */
	desired.freq = snd_mixspeed.value;
	desired.format = (loadas8bit.value) ? AUDIO_U8 : AUDIO_S16SYS;
	desired.channels = S_SteamAudio_OutputChannels ();
	if (desired.freq <= 11025)
		desired.samples = 256;
	else if (desired.freq <= 22050)
		desired.samples = 512;
	else if (desired.freq <= 44100)
		desired.samples = 1024;
	else if (desired.freq <= 56000)
		desired.samples = 2048; /* for 48 kHz */
	else
		desired.samples = 4096; /* for 96 kHz */
	desired.callback = paint_audio;
	desired.userdata = NULL;

	/* Open the audio device */
	if (SDL_OpenAudio (&desired, NULL) == -1)
	{
		Con_Printf ("Couldn't open SDL audio: %s\n", SDL_GetError ());
		SDL_QuitSubSystem (SDL_INIT_AUDIO);
		return false;
	}

	memset ((void *)dma, 0, sizeof (dma_t));
	shm = dma;

	/* Fill the audio DMA information block */
	/* Since we passed NULL as the 'obtained' spec to SDL_OpenAudio(),
	 * SDL will convert to hardware format for us if needed, hence we
	 * directly use the desired values here. */
	shm->samplebits = (desired.format & 0xFF); /* first byte of format is bits */
	shm->signed8 = (desired.format == AUDIO_S8);
	shm->speed = desired.freq;
	shm->channels = desired.channels;
	tmp = Q_nextPow2 (desired.samples * 10);
	shm->samples = tmp * desired.channels;
	shm->samplepos = 0;
	shm->submission_chunk = 1;

	Con_Printf ("SDL audio spec  : %d Hz, %d samples, %d channels\n", desired.freq, desired.samples, desired.channels);
	{
		const char *driver = SDL_GetCurrentAudioDriver ();
		const char *device = SDL_GetAudioDeviceName (0, SDL_FALSE);
		q_snprintf (drivername, sizeof (drivername), "%s - %s", driver != NULL ? driver : "(UNKNOWN)", device != NULL ? device : "(UNKNOWN)");
	}
	buffersize = shm->samples * (shm->samplebits / 8);
	Con_Printf ("SDL audio driver: %s, %d bytes buffer\n", drivername, buffersize);

	shm->buffer = (unsigned char *)Mem_Alloc (buffersize);
	if (!shm->buffer)
	{
		SDL_CloseAudio ();
		SDL_QuitSubSystem (SDL_INIT_AUDIO);
		shm = NULL;
		Con_Printf ("Failed allocating memory for SDL audio\n");
		return false;
	}

	SDL_PauseAudio (0);

	return true;
}

int SNDDMA_GetDMAPos (void)
{
	return shm->samplepos;
}

void SNDDMA_Shutdown (void)
{
	if (shm)
	{
		Con_Printf ("Shutting down SDL sound\n");
		SDL_CloseAudio ();
		SDL_QuitSubSystem (SDL_INIT_AUDIO);
		if (shm->buffer)
			Mem_Free (shm->buffer);
		shm->buffer = NULL;
		shm = NULL;
	}
}

void SNDDMA_LockBuffer (void)
{
	SDL_LockAudio ();
}

void SNDDMA_Submit (void)
{
	SDL_UnlockAudio ();
}

void SNDDMA_BlockSound (void)
{
	SDL_PauseAudio (1);
}

void SNDDMA_UnblockSound (void)
{
	SDL_PauseAudio (0);
}

#endif
