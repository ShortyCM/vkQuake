/* Native SDK regression tests: real HRTF, moving occluders, transmission,
 * convolution/reverb (including unaligned PFFFT channels), and baked pathing.
 * These use the same frame size, order and native API as the engine renderer. */
#include "../ThirdParty/SteamAudio/include/phonon.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main (void)
{
	setvbuf (stdout, NULL, _IONBF, 0);
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
		hrtf_settings.type = IPL_HRTFTYPE_DEFAULT;
		hrtf_settings.volume = 1;
		OK (iplHRTFCreate (context, &audio, &hrtf_settings, &hrtf));
		printf ("Testing %d Hz\n", audio.samplingRate);
		test_hrtf (context, &audio, hrtf);
		test_simulation (context, &audio);
		iplHRTFRelease (&hrtf);
		iplContextRelease (&context);
	}
	CHECK (validation_errors == 0);
	puts ("All native Steam Audio tests passed.");
	return 0;
}
