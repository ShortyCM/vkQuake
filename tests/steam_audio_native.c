/* Native SDK regression tests: real HRTF, speaker panning/decoding, moving occluders, transmission,
 * convolution/reverb (including unaligned PFFFT channels), and baked pathing.
 * These use the same frame size, order and native API as the engine renderer. */
#include "../ThirdParty/SteamAudio/include/phonon.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../Quake/snd_crossover.h"

#define FRAME 512
#define CHECK(x)                                                  \
	do                                                            \
	{                                                             \
		if (!(x))                                                 \
		{                                                         \
			fprintf (stderr, "FAIL line %d: %s\n", __LINE__, #x); \
			exit (1);                                             \
		}                                                         \
	} while (0)
#define OK(x) CHECK ((x) == IPL_STATUS_SUCCESS)
static int validation_errors;

static void IPLCALL log_message (IPLLogLevel level, const char *message)
{
	if (level == IPL_LOGLEVEL_ERROR)
	{
		fprintf (stderr, "SDK: %s\n", message);
		validation_errors++;
	}
}

static IPLCoordinateSpace3 space (float x, float y, float z)
{
	IPLCoordinateSpace3 result = {{1, 0, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 0}};
	result.origin = (IPLVector3){x, y, z};
	return result;
}

static double energy (const float *values, int size)
{
	double sum = 0;
	for (int i = 0; i < size; i++)
	{
		CHECK (isfinite (values[i]));
		sum += (double)values[i] * values[i];
	}
	return sum;
}

static void crossover_response (int rate, int cutoff, double frequency, double response[3])
{
	snd_crossover_coeffs_t coeffs;
	snd_crossover_t		   state = {0};
	double				   input_energy = 0, output_energy[3] = {0};
	S_Crossover_Init (&coeffs, rate, cutoff);
	for (int i = 0; i < 2 * rate; i++)
	{
		const double				 input = sin (6.28318530717958647692 * frequency * i / rate);
		const snd_crossover_sample_t sample = S_Crossover_Process (&coeffs, &state, input);
		CHECK (isfinite (sample.low) && isfinite (sample.high));
		if (i >= rate)
		{
			input_energy += input * input;
			output_energy[0] += sample.low * sample.low;
			output_energy[1] += sample.high * sample.high;
			output_energy[2] += (sample.low + sample.high) * (sample.low + sample.high);
		}
	}
	for (int i = 0; i < 3; i++)
		response[i] = sqrt (output_energy[i] / input_energy);
}

static void test_crossover (void)
{
	CHECK (S_Crossover_Frequency (NAN) == 80 && S_Crossover_Frequency (INFINITY) == 80);
	CHECK (S_Crossover_Frequency (-1000) == 60 && S_Crossover_Frequency (1000) == 120);
	CHECK (S_Crossover_Frequency (84.9f) == 80 && S_Crossover_Frequency (85) == 90);
	for (int rate = 44100; rate <= 48000; rate += 3900)
		for (int cutoff = 60; cutoff <= 120; cutoff += 10)
		{
			double bass[3], center[3], treble[3];
			CHECK (S_Crossover_Frequency ((float)cutoff) == cutoff);
			crossover_response (rate, cutoff, cutoff / 4.0, bass);
			crossover_response (rate, cutoff, cutoff, center);
			crossover_response (rate, cutoff, cutoff * 4.0, treble);
			CHECK (bass[0] > 0.995 && bass[0] < 1.001 && bass[1] < 0.005);
			CHECK (treble[1] > 0.995 && treble[1] < 1.001 && treble[0] < 0.005);
			CHECK (fabs (center[0] - 0.5) < 0.001 && fabs (center[1] - 0.5) < 0.001);
			CHECK (fabs (bass[2] - 1) < 0.004 && fabs (center[2] - 1) < 0.004 && fabs (treble[2] - 1) < 0.004);

			snd_crossover_coeffs_t coeffs;
			snd_crossover_t		   mono = {0}, stereo[3][2] = {0}, continuous = {0}, blocked = {0};
			snd_crossover_sample_t whole[4 * FRAME], blocks[4 * FRAME];
			S_Crossover_Init (&coeffs, rate, cutoff);
			for (int i = 0; i < 4 * FRAME; i++)
			{
				const double				 input = sin (i * 0.013) + 0.25 * sin (i * 0.75);
				const snd_crossover_sample_t sample = S_Crossover_Process (&coeffs, &mono, input);
				const snd_crossover_stereo_t same = S_Crossover_Stereo (&coeffs, stereo[0], input, input);
				const snd_crossover_stereo_t left = S_Crossover_Stereo (&coeffs, stereo[1], input, 0);
				const snd_crossover_stereo_t opposite = S_Crossover_Stereo (&coeffs, stereo[2], input, -input);
				CHECK (same.left == sample.high && same.right == sample.high && same.bass == sample.low);
				CHECK (left.left == sample.high && left.right == 0 && left.bass == sample.low * 0.5);
				CHECK (opposite.left == sample.high && opposite.right == -sample.high && opposite.bass == 0);
				whole[i] = S_Crossover_Process (&coeffs, &continuous, input);
			}
			for (int block = 0; block < 4; block++)
				for (int j = 0; j < FRAME; j++)
				{
					const int i = block * FRAME + j;
					blocks[i] = S_Crossover_Process (&coeffs, &blocked, sin (i * 0.013) + 0.25 * sin (i * 0.75));
				}
			CHECK (memcmp (whole, blocks, sizeof (whole)) == 0);
			CHECK (S_Crossover_Active (&blocked));
			S_Crossover_Reset (&blocked);
			CHECK (!S_Crossover_Active (&blocked));
			const snd_crossover_sample_t silence = S_Crossover_Process (&coeffs, &blocked, 0);
			CHECK (silence.low == 0 && silence.high == 0);
			for (int i = 0; i < rate; i++)
				S_Crossover_Process (&coeffs, &continuous, 0);
			CHECK (!S_Crossover_Active (&continuous));
			printf (
				"PASS LR4 %d Hz / %d Hz crossover: LP %.3f, HP %.3f, sum %.3f; stereo averaging, continuity, reset\n", rate, cutoff, center[0], center[1],
				center[2]);
		}
}

static void test_crossover_routing (IPLContext context, IPLAudioSettings *audio)
{
	snd_crossover_coeffs_t coeffs;
	S_Crossover_Init (&coeffs, audio->samplingRate, 80);
	const IPLSpeakerLayoutType layouts[] = {IPL_SPEAKERLAYOUTTYPE_SURROUND_5_1, IPL_SPEAKERLAYOUTTYPE_SURROUND_7_1};
	for (int layout = 0; layout < 2; layout++)
	{
		const int				 channels = layout ? 8 : 6;
		IPLPanningEffect		 effect = NULL;
		IPLPanningEffectSettings settings = {{layouts[layout], 0, NULL}};
		OK (iplPanningEffectCreate (context, audio, &settings, &effect));
		double reference_bass = 0;
		for (int angle = 0; angle < 8; angle++)
		{
			float			high[FRAME], low[FRAME], output[8][FRAME], *data[8];
			snd_crossover_t state = {0};
			for (int i = 0; i < FRAME; i++)
			{
				const double				 input = 0.25 * sin (i * 0.02) + 0.1 * sin (i * 0.2);
				const snd_crossover_sample_t sample = S_Crossover_Process (&coeffs, &state, input);
				high[i] = (float)sample.high;
				low[i] = (float)sample.low;
			}
			for (int channel = 0; channel < 8; channel++)
				data[channel] = output[channel];
			float				  *input_data[] = {high};
			IPLAudioBuffer		   in = {1, FRAME, input_data}, out = {channels, FRAME, data};
			const float			   radians = (float)(angle * 0.7853981633974483);
			IPLPanningEffectParams params = {{sinf (radians), 0, -cosf (radians)}};
			// Settle panning interpolation so only the source crossover is measured.
			iplPanningEffectApply (effect, &params, &in, &out);
			iplPanningEffectApply (effect, &params, &in, &out);
			CHECK (energy (output[3], FRAME) == 0);
			memcpy (output[3], low, sizeof (low));
			const double bass = energy (output[3], FRAME);
			if (!angle)
				reference_bass = bass;
			CHECK (bass > 0 && bass == reference_bass);
			double main_energy = 0;
			for (int channel = 0; channel < channels; channel++)
				if (channel != 3)
					main_energy += energy (output[channel], FRAME);
			CHECK (fabs (main_energy / energy (high, FRAME) - 1) < 0.001);
		}
		iplPanningEffectRelease (&effect);
		printf ("PASS %d Hz / %d channels: mono bass gain independent of direction; high band panned\n", audio->samplingRate, channels);
	}

	// The indirect bass decoder must recover mono source level using native SH normalization.
	IPLAmbisonicsEncodeEffect		  encode = NULL;
	IPLAmbisonicsEncodeEffectSettings encode_settings = {1};
	IPLAmbisonicsDecodeEffect		  decode = NULL;
	IPLAmbisonicsDecodeEffectSettings decode_settings = {{IPL_SPEAKERLAYOUTTYPE_MONO, 0, NULL}, NULL, 1};
	OK (iplAmbisonicsEncodeEffectCreate (context, audio, &encode_settings, &encode));
	OK (iplAmbisonicsDecodeEffectCreate (context, audio, &decode_settings, &decode));
	for (int angle = 0; angle < 4; angle++)
	{
		float		   input[FRAME], reference[FRAME], ambi[4][FRAME], low[4][FRAME], output[FRAME];
		float		  *input_data[] = {input}, *ambi_data[] = {ambi[0], ambi[1], ambi[2], ambi[3]};
		float		  *low_data[] = {low[0], low[1], low[2], low[3]}, *output_data[] = {output};
		IPLAudioBuffer in = {1, FRAME, input_data}, encoded = {4, FRAME, ambi_data}, bass = {4, FRAME, low_data}, out = {1, FRAME, output_data};
		IPLAmbisonicsEncodeEffectParams params = {{(float)(angle & 1), 0, (float)(angle & 2 ? 1 : -1)}, 1};
		IPLAmbisonicsDecodeEffectParams decode_params = {0};
		decode_params.order = 1;
		decode_params.orientation = space (0, 0, 0);
		snd_crossover_t state[4] = {0}, mono = {0};
		for (int i = 0; i < FRAME; i++)
		{
			input[i] = 0.25f * sinf (i * 0.02f);
			reference[i] = (float)S_Crossover_Process (&coeffs, &mono, input[i]).low;
		}
		iplAmbisonicsEncodeEffectApply (encode, &params, &in, &encoded);
		iplAmbisonicsEncodeEffectApply (encode, &params, &in, &encoded);
		for (int channel = 0; channel < 4; channel++)
			for (int i = 0; i < FRAME; i++)
				low[channel][i] = (float)S_Crossover_Process (&coeffs, &state[channel], ambi[channel][i]).low;
		iplAmbisonicsDecodeEffectApply (decode, &decode_params, &bass, &out);
		for (int i = 0; i < FRAME; i++)
			CHECK (fabs (output[i] - reference[i]) < 1e-5);
	}
	iplAmbisonicsEncodeEffectRelease (&encode);
	iplAmbisonicsDecodeEffectRelease (&decode);
	printf ("PASS %d Hz: low-band Ambisonics decoded to mono at source gain\n", audio->samplingRate);
}

static void test_hrtf (IPLContext context, IPLAudioSettings *audio, IPLHRTF hrtf)
{
	IPLBinauralEffect		  effect = NULL;
	IPLBinauralEffectSettings settings = {hrtf};
	float					  input[FRAME] = {0}, output[6][2][FRAME];
	float					 *in_data[] = {input};
	IPLAudioBuffer			  in = {1, FRAME, in_data};
	IPLVector3				  directions[] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, -1, 0}};
	OK (iplBinauralEffectCreate (context, audio, &settings, &effect));
	for (int d = 0; d < 6; d++)
	{
		IPLBinauralEffectParams params = {0};
		float				   *out_data[] = {output[d][0], output[d][1]};
		IPLAudioBuffer			out = {2, FRAME, out_data};
		params.direction = directions[d];
		params.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
		params.spatialBlend = 1;
		params.hrtf = hrtf;
		iplBinauralEffectReset (effect);
		input[0] = 1;
		iplBinauralEffectApply (effect, &params, &in, &out);
		CHECK (energy (output[d][0], FRAME) + energy (output[d][1], FRAME) > 1e-6);
	}
	CHECK (energy (output[0][0], FRAME) > energy (output[0][1], FRAME));
	CHECK (energy (output[1][1], FRAME) > energy (output[1][0], FRAME));
	double front_back = 0, up_down = 0;
	for (int i = 0; i < FRAME; i++)
	{
		double a = output[2][0][i] - output[3][0][i];
		double b = output[4][0][i] - output[5][0][i];
		front_back += a * a;
		up_down += b * b;
	}
	CHECK (front_back > 1e-4 && up_down > 1e-4);
	iplBinauralEffectRelease (&effect);
	puts ("PASS HRTF: left/right, front/back, elevation");
}

static void test_speakers (IPLContext context, IPLAudioSettings *audio)
{
	const IPLSpeakerLayoutType layouts[] = {
		IPL_SPEAKERLAYOUTTYPE_STEREO, IPL_SPEAKERLAYOUTTYPE_QUADRAPHONIC, IPL_SPEAKERLAYOUTTYPE_SURROUND_5_1, IPL_SPEAKERLAYOUTTYPE_SURROUND_7_1};
	const int		 channels[] = {2, 4, 6, 8};
	const IPLVector3 directions[] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}};
	float			 input[FRAME];
	for (int i = 0; i < FRAME; i++)
		input[i] = 0.1f * sinf (i * 0.04f);
	float		  *input_data[] = {input};
	IPLAudioBuffer in = {1, FRAME, input_data};
	for (int layout = 0; layout < 4; layout++)
	{
		IPLPanningEffect				  panning = NULL;
		IPLPanningEffectSettings		  panning_settings = {{layouts[layout], 0, NULL}};
		IPLAmbisonicsDecodeEffect		  decode = NULL;
		IPLAmbisonicsDecodeEffectSettings decode_settings = {{layouts[layout], 0, NULL}, NULL, 1};
		OK (iplPanningEffectCreate (context, audio, &panning_settings, &panning));
		// Speaker decoding must work without loading or supplying an HRTF.
		OK (iplAmbisonicsDecodeEffectCreate (context, audio, &decode_settings, &decode));
		for (int direction = 0; direction < 4; direction++)
		{
			float  output[8][FRAME] = {0};
			float *output_data[8];
			for (int channel = 0; channel < 8; channel++)
			{
				output_data[channel] = output[channel];
				if (channel >= channels[layout])
					for (int i = 0; i < FRAME; i++)
						output[channel][i] = 12345;
			}
			IPLAudioBuffer		   out = {channels[layout], FRAME, output_data};
			IPLPanningEffectParams params = {directions[direction]};
			iplPanningEffectReset (panning);
			iplPanningEffectApply (panning, &params, &in, &out);
			double e[8] = {0}, total = 0;
			for (int channel = 0; channel < channels[layout]; channel++)
				total += e[channel] = energy (output[channel], FRAME);
			CHECK (total > 1e-6);
			for (int channel = channels[layout]; channel < 8; channel++)
				for (int i = 0; i < FRAME; i++)
					CHECK (output[channel][i] == 12345);
			double left = e[0], right = e[1];
			if (channels[layout] == 4)
			{
				left += e[2];
				right += e[3];
			}
			if (channels[layout] >= 6)
			{
				left += e[4] + e[6];
				right += e[5] + e[7];
				CHECK (e[3] == 0); // SDK panning excludes LFE; the source crossover supplies its low band separately.
			}
			if (direction < 2)
				CHECK (direction == 0 ? left > right : right > left);
			if (direction == 3 && channels[layout] > 2)
				CHECK ((channels[layout] == 4 ? e[2] + e[3] : e[4] + e[5]) > e[0] + e[1]);
			if (direction == 2 && channels[layout] >= 6)
				CHECK (e[2] > e[0] && e[2] > e[1]);

			// Reflections/reverb use Ambisonics, independent of direct panning.
			float		   ambisonics[4][FRAME] = {0};
			float		  *ambi_data[] = {ambisonics[0], ambisonics[1], ambisonics[2], ambisonics[3]};
			IPLAudioBuffer ambi = {4, FRAME, ambi_data};
			memcpy (ambisonics[0], input, sizeof (input));
			IPLAmbisonicsDecodeEffectParams decode_params = {0};
			decode_params.order = 1;
			decode_params.orientation = space (0, 0, 0);
			decode_params.binaural = IPL_FALSE;
			iplAmbisonicsDecodeEffectApply (decode, &decode_params, &ambi, &out);
			total = 0;
			for (int channel = 0; channel < channels[layout]; channel++)
				total += energy (output[channel], FRAME);
			CHECK (total > 1e-6);
			if (channels[layout] > 2)
				CHECK (energy (output[channels[layout] - 1], FRAME) > 1e-7);
		}
		// A moving source must crossfade within this block, not stay at its
		// previous direction until the next block (SDK 4.8.1 regression).
		float  output[8][FRAME] = {0};
		float *output_data[8];
		for (int channel = 0; channel < 8; channel++)
			output_data[channel] = output[channel];
		IPLAudioBuffer		   out = {channels[layout], FRAME, output_data};
		IPLPanningEffectParams params = {directions[0]};
		iplPanningEffectApply (panning, &params, &in, &out);
		iplPanningEffectApply (panning, &params, &in, &out);
		params.direction = directions[1];
		iplPanningEffectApply (panning, &params, &in, &out);
		for (int endpoint = 0; endpoint < 2; endpoint++)
		{
			const int sample = endpoint ? FRAME - 1 : 1;
			double	  left = fabs (output[0][sample]), right = fabs (output[1][sample]);
			if (channels[layout] == 4)
			{
				left += fabs (output[2][sample]);
				right += fabs (output[3][sample]);
			}
			if (channels[layout] >= 6)
			{
				left += fabs (output[4][sample]) + fabs (output[6][sample]);
				right += fabs (output[5][sample]) + fabs (output[7][sample]);
			}
			CHECK (endpoint ? right > left : left > right);
		}
		iplAmbisonicsDecodeEffectRelease (&decode);
		iplPanningEffectRelease (&panning);
		printf ("PASS %d-channel speakers: direct panning, channel order, and Ambisonics decode\n", channels[layout]);
	}
}

static void add_mesh (IPLScene scene, IPLVector3 *vertices, int nv, IPLTriangle *triangles, int nt)
{
	IPLMaterial			  material = {{0.10f, 0.15f, 0.20f}, 0.65f, {0.25f, 0.10f, 0.04f}};
	int					  indices[12] = {0};
	IPLStaticMesh		  mesh = NULL;
	IPLStaticMeshSettings settings = {nv, nt, 1, vertices, triangles, indices, &material};
	OK (iplStaticMeshCreate (scene, &settings, &mesh));
	iplStaticMeshAdd (mesh, scene);
	iplSceneCommit (scene);
	iplStaticMeshRelease (&mesh);
}

static double test_reflection (IPLContext context, IPLAudioSettings *audio, IPLSource source)
{
	IPLReflectionEffect			effect = NULL;
	IPLReflectionEffectSettings settings = {IPL_REFLECTIONEFFECTTYPE_CONVOLUTION, audio->samplingRate, 4};
	IPLSimulationOutputs		outputs = {0};
	float						input[FRAME] = {0}, output[4][FRAME];
	float					   *in_data[] = {input}, *out_data[] = {output[0], output[1], output[2], output[3]};
	IPLAudioBuffer				in = {1, FRAME, in_data}, out = {4, FRAME, out_data};
	double						total = 0, tail = 0;
	OK (iplReflectionEffectCreate (context, audio, &settings, &effect));
	iplSourceGetOutputs (source, IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs);
	outputs.reflections.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
	CHECK (outputs.reflections.ir != NULL && outputs.reflections.numChannels == 4);
	for (int frame = 0; frame < (audio->samplingRate / FRAME) + 4; frame++)
	{
		input[0] = frame == 0 ? 1.0f : 0.0f;
		iplReflectionEffectApply (effect, &outputs.reflections, &in, &out, NULL);
		for (int ch = 0; ch < 4; ch++)
		{
			double e = energy (output[ch], FRAME);
			total += e;
			if (frame > 8)
				tail += e;
		}
	}
	CHECK (total > 1e-7 && tail > 1e-10);
	iplReflectionEffectRelease (&effect);
	return tail;
}

static void test_simulation (IPLContext context, IPLAudioSettings *audio)
{
	IPLScene				 scene = NULL, wall_scene = NULL;
	IPLSceneSettings		 scene_settings = {0};
	IPLInstancedMesh		 wall = NULL;
	IPLInstancedMeshSettings instance_settings = {0};
	IPLMatrix4x4			 transform = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
	IPLVector3				 room_vertices[] = {{-5, -3, -5}, {5, -3, -5}, {5, 3, -5}, {-5, 3, -5}, {-5, -3, 5}, {5, -3, 5}, {5, 3, 5}, {-5, 3, 5}};
	IPLTriangle				 room_triangles[] = {{{0, 1, 2}}, {{0, 2, 3}}, {{5, 4, 7}}, {{5, 7, 6}}, {{4, 0, 3}}, {{4, 3, 7}},
												 {{1, 5, 6}}, {{1, 6, 2}}, {{3, 2, 6}}, {{3, 6, 7}}, {{4, 5, 1}}, {{4, 1, 0}}};
	IPLVector3				 wall_vertices[] = {{0, -3, -3}, {0, 3, -3}, {0, 3, 3}, {0, -3, 3}};
	IPLTriangle				 wall_triangles[] = {{{0, 1, 2}}, {{0, 2, 3}}};
	scene_settings.type = IPL_SCENETYPE_DEFAULT;
	OK (iplSceneCreate (context, &scene_settings, &scene));
	OK (iplSceneCreate (context, &scene_settings, &wall_scene));
	add_mesh (scene, room_vertices, 8, room_triangles, 12);
	add_mesh (wall_scene, wall_vertices, 4, wall_triangles, 2);
	instance_settings.subScene = wall_scene;
	instance_settings.transform = transform;
	OK (iplInstancedMeshCreate (scene, &instance_settings, &wall));

	IPLSimulator			  simulator = NULL;
	IPLSimulationSettings	  settings = {0};
	IPLSource				  sources[2] = {0};
	IPLSourceSettings		  source_settings = {IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING};
	IPLSimulationSharedInputs shared = {0};
	IPLSimulationInputs		  inputs[2] = {0};
	settings.flags = source_settings.flags;
	settings.sceneType = IPL_SCENETYPE_DEFAULT;
	settings.reflectionType = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
	settings.maxNumOcclusionSamples = 8;
	settings.maxNumRays = 2048;
	settings.numDiffuseSamples = 32;
	settings.maxDuration = 1;
	settings.maxOrder = 1;
	settings.maxNumSources = 2;
	settings.numThreads = 2;
	settings.numVisSamples = 1;
	settings.samplingRate = audio->samplingRate;
	settings.frameSize = FRAME;
	OK (iplSimulatorCreate (context, &settings, &simulator));
	iplSimulatorSetScene (simulator, scene);
	for (int i = 0; i < 2; i++)
	{
		OK (iplSourceCreate (simulator, &source_settings, &sources[i]));
		iplSourceAdd (sources[i], simulator);
		inputs[i].flags = source_settings.flags;
		inputs[i].source = space (i == 0 ? -2.0f : 2.0f, 0, 0);
		inputs[i].distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_INVERSEDISTANCE;
		inputs[i].distanceAttenuationModel.minDistance = 1;
		inputs[i].directFlags = IPL_DIRECTSIMULATIONFLAGS_OCCLUSION | IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION;
		inputs[i].occlusionType = IPL_OCCLUSIONTYPE_RAYCAST;
		inputs[i].numTransmissionRays = 16;
		inputs[i].reverbScale[0] = inputs[i].reverbScale[1] = inputs[i].reverbScale[2] = 1;
		inputs[i].visRadius = 0;
		inputs[i].visThreshold = 0.5f;
		inputs[i].visRange = 50;
		inputs[i].pathingOrder = 1;
		inputs[i].enableValidation = IPL_TRUE;
		inputs[i].findAlternatePaths = IPL_TRUE;
		iplSourceSetInputs (sources[i], settings.flags, &inputs[i]);
	}
	iplSimulatorCommit (simulator);
	shared.listener = space (2, 0, 0);
	shared.numRays = 2048;
	shared.numBounces = 16;
	shared.duration = 1;
	shared.order = 1;
	shared.irradianceMinDistance = 0.5f;
	iplSimulatorSetSharedInputs (simulator, settings.flags, &shared);
	IPLSimulationOutputs outputs = {0};
	iplSimulatorRunDirect (simulator);
	iplSourceGetOutputs (sources[0], IPL_SIMULATIONFLAGS_DIRECT, &outputs);
	CHECK (outputs.direct.occlusion > 0.99f);
	iplInstancedMeshAdd (wall, scene);
	iplSceneCommit (scene);
	iplSimulatorRunDirect (simulator);
	iplSourceGetOutputs (sources[0], IPL_SIMULATIONFLAGS_DIRECT, &outputs);
	CHECK (outputs.direct.occlusion < 0.01f);
	CHECK (outputs.direct.transmission[0] > outputs.direct.transmission[2]);
	CHECK (outputs.direct.transmission[0] > 0 && outputs.direct.transmission[0] < 1);
	transform.elements[0][3] = 8;
	iplInstancedMeshUpdateTransform (wall, scene, transform);
	iplSceneCommit (scene);
	iplSimulatorRunDirect (simulator);
	iplSourceGetOutputs (sources[0], IPL_SIMULATIONFLAGS_DIRECT, &outputs);
	CHECK (outputs.direct.occlusion > 0.99f);
	puts ("PASS occlusion, frequency-dependent transmission, moving brush transform");

	iplInstancedMeshRemove (wall, scene);
	iplSceneCommit (scene);
	for (int i = 0; i < 3; i++)
		iplSimulatorRunReflections (simulator);
	double source_tail = test_reflection (context, audio, sources[0]);
	double room_tail = test_reflection (context, audio, sources[1]);
	printf ("PASS reflections and listener reverb: tail energy %g / %g\n", source_tail, room_tail);

	transform.elements[0][3] = 0;
	iplInstancedMeshUpdateTransform (wall, scene, transform);
	iplInstancedMeshAdd (wall, scene);
	iplSceneCommit (scene);
	IPLProbeBatch batch = NULL;
	IPLVector3	  points[] = {{-2, 0, 0}, {-2, 0, 4}, {2, 0, 4}, {2, 0, 0}};
	OK (iplProbeBatchCreate (context, &batch));
	for (int i = 0; i < 4; i++)
	{
		IPLSphere sphere = {points[i], 2.5f};
		iplProbeBatchAddProbe (batch, sphere);
	}
	iplProbeBatchCommit (batch);
	IPLPathBakeParams bake = {0};
	bake.scene = scene;
	bake.probeBatch = batch;
	bake.identifier.type = IPL_BAKEDDATATYPE_PATHING;
	bake.identifier.variation = IPL_BAKEDDATAVARIATION_DYNAMIC;
	bake.numSamples = 1;
	bake.threshold = 0.5f;
	bake.visRange = 50;
	bake.pathRange = 100;
	bake.numThreads = 2;
	iplPathBakerBake (context, &bake, NULL, NULL);
	IPLSerializedObject			serialized = NULL;
	IPLSerializedObjectSettings serialized_settings = {0};
	OK (iplSerializedObjectCreate (context, &serialized_settings, &serialized));
	iplProbeBatchSave (batch, serialized);
	iplProbeBatchRelease (&batch);
	OK (iplProbeBatchLoad (context, serialized, &batch));
	iplProbeBatchCommit (batch);
	CHECK (iplProbeBatchGetNumProbes (batch) == 4);
	iplSerializedObjectRelease (&serialized);
	iplSimulatorAddProbeBatch (simulator, batch);
	iplSimulatorCommit (simulator);
	inputs[0].pathingProbes = batch;
	inputs[1].flags &= ~IPL_SIMULATIONFLAGS_PATHING;
	for (int i = 0; i < 2; i++)
		iplSourceSetInputs (sources[i], settings.flags, &inputs[i]);
	iplSimulatorRunPathing (simulator);
	iplSourceGetOutputs (sources[0], IPL_SIMULATIONFLAGS_PATHING, &outputs);
	CHECK (outputs.pathing.shCoeffs && energy (outputs.pathing.shCoeffs, 4) > 1e-8);
	double				  open_path = energy (outputs.pathing.shCoeffs, 4);
	IPLPathEffect		  path_effect = NULL;
	IPLPathEffectSettings path_settings = {0};
	path_settings.maxOrder = 1;
	OK (iplPathEffectCreate (context, audio, &path_settings, &path_effect));
	float		   path_input[FRAME] = {1}, path_output[4][FRAME];
	float		  *path_in_data[] = {path_input};
	float		  *path_out_data[] = {path_output[0], path_output[1], path_output[2], path_output[3]};
	IPLAudioBuffer path_in = {1, FRAME, path_in_data}, path_out = {4, FRAME, path_out_data};
	outputs.pathing.order = 1;
	iplPathEffectApply (path_effect, &outputs.pathing, &path_in, &path_out);
	double path_energy = 0;
	for (int i = 0; i < 4; i++)
		path_energy += energy (path_output[i], FRAME);
	CHECK (path_energy > 1e-8);
	iplPathEffectRelease (&path_effect);
	transform.elements[2][3] = 2;
	iplInstancedMeshUpdateTransform (wall, scene, transform);
	iplSceneCommit (scene);
	iplSimulatorRunPathing (simulator);
	iplSourceGetOutputs (sources[0], IPL_SIMULATIONFLAGS_PATHING, &outputs);
	CHECK (energy (outputs.pathing.shCoeffs, 4) < open_path * 0.01);
	puts ("PASS baked pathing round-trip and dynamic path validation");
	for (int i = 0; i < 2; i++)
		iplSourceRelease (&sources[i]);
	iplSimulatorRelease (&simulator);
	iplProbeBatchRelease (&batch);
	iplInstancedMeshRelease (&wall);
	iplSceneRelease (&wall_scene);
	iplSceneRelease (&scene);
}

int main (int argc, char **argv)
{
	setvbuf (stdout, NULL, _IONBF, 0);
	test_crossover ();
	const int lfe_only = argc == 2 && strcmp (argv[1], "--lfe-only") == 0;
	for (int rate = 0; rate < 2; rate++)
	{
		IPLContext		   context = NULL;
		IPLContextSettings settings = {0};
		IPLAudioSettings   audio = {rate ? 48000 : 44100, FRAME};
		IPLHRTFSettings	   hrtf_settings = {0};
		IPLHRTF			   hrtf = NULL;
		settings.version = STEAMAUDIO_VERSION;
		settings.logCallback = log_message;
		settings.simdLevel = IPL_SIMDLEVEL_AVX2;
		settings.flags = IPL_CONTEXTFLAGS_VALIDATION;
		OK (iplContextCreate (&settings, &context));
		test_crossover_routing (context, &audio);
		if (lfe_only)
		{
			iplContextRelease (&context);
			continue;
		}
		hrtf_settings.type = IPL_HRTFTYPE_DEFAULT;
		hrtf_settings.volume = 1;
		OK (iplHRTFCreate (context, &audio, &hrtf_settings, &hrtf));
		printf ("Testing %d Hz\n", audio.samplingRate);
		test_hrtf (context, &audio, hrtf);
		test_speakers (context, &audio);
		test_simulation (context, &audio);
		iplHRTFRelease (&hrtf);
		iplContextRelease (&context);
	}
	CHECK (validation_errors == 0);
	puts (lfe_only ? "Crossover and LFE routing tests passed." : "All native Steam Audio tests passed.");
	return 0;
}
