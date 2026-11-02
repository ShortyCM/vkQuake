/* Matched 24 dB/octave Linkwitz-Riley low/high crossover.
 * Copyright (C) 2026 vkQuake contributors. GPL-2.0-or-later. */
#ifndef SND_CROSSOVER_H
#define SND_CROSSOVER_H

#include <math.h>
#include <string.h>

typedef struct
{
	double b[2][3], a1, a2;
} snd_crossover_coeffs_t;

typedef struct
{
	double z1[2][2], z2[2][2];
} snd_crossover_t;

typedef struct
{
	double low, high;
} snd_crossover_sample_t;

typedef struct
{
	double left, right, bass;
} snd_crossover_stereo_t;

static inline int S_Crossover_Frequency (float value)
{
	if (!isfinite (value))
		return 80;
	if (value < 60)
		value = 60;
	if (value > 120)
		value = 120;
	return 10 * (int)(value / 10.0f + 0.5f);
}

static inline void S_Crossover_Init (snd_crossover_coeffs_t *coeffs, int rate, int frequency)
{
	/* Two cascaded Butterworth biquads per branch give LR4 responses:
	 * -6 dB at the crossover, equal phase, and a flat summed magnitude. */
	const double omega = 6.28318530717958647692 * frequency / rate;
	const double cosine = cos (omega);
	const double alpha = sin (omega) / sqrt (2.0);
	const double scale = 1.0 / (1.0 + alpha);
	coeffs->b[0][0] = coeffs->b[0][2] = (1.0 - cosine) * 0.5 * scale;
	coeffs->b[0][1] = (1.0 - cosine) * scale;
	coeffs->b[1][0] = coeffs->b[1][2] = (1.0 + cosine) * 0.5 * scale;
	coeffs->b[1][1] = -(1.0 + cosine) * scale;
	coeffs->a1 = -2.0 * cosine * scale;
	coeffs->a2 = (1.0 - alpha) * scale;
}

static inline void S_Crossover_Reset (snd_crossover_t *state)
{
	memset (state, 0, sizeof (*state));
}

static inline int S_Crossover_Active (const snd_crossover_t *state)
{
	for (int band = 0; band < 2; band++)
		for (int stage = 0; stage < 2; stage++)
			if (fabs (state->z1[band][stage]) > 1e-20 || fabs (state->z2[band][stage]) > 1e-20)
				return 1;
	return 0;
}

static inline snd_crossover_sample_t S_Crossover_Process (const snd_crossover_coeffs_t *coeffs, snd_crossover_t *state, double input)
{
	double output[2] = {input, input};
	for (int band = 0; band < 2; band++)
		for (int stage = 0; stage < 2; stage++)
		{
			const double sample = coeffs->b[band][0] * output[band] + state->z1[band][stage];
			state->z1[band][stage] = coeffs->b[band][1] * output[band] - coeffs->a1 * sample + state->z2[band][stage];
			state->z2[band][stage] = coeffs->b[band][2] * output[band] - coeffs->a2 * sample;
			/* Suppress subnormal arithmetic when a silent input drains the tail. */
			if (fabs (state->z1[band][stage]) < 1e-24)
				state->z1[band][stage] = 0;
			if (fabs (state->z2[band][stage]) < 1e-24)
				state->z2[band][stage] = 0;
			output[band] = sample;
		}
	snd_crossover_sample_t result = {output[0], output[1]};
	return result;
}

static inline snd_crossover_stereo_t S_Crossover_Stereo (const snd_crossover_coeffs_t *coeffs, snd_crossover_t state[2], double left, double right)
{
	const snd_crossover_sample_t l = S_Crossover_Process (coeffs, &state[0], left);
	const snd_crossover_sample_t r = S_Crossover_Process (coeffs, &state[1], right);
	snd_crossover_stereo_t		 result = {l.high, r.high, 0.5 * (l.low + r.low)};
	return result;
}

#endif
