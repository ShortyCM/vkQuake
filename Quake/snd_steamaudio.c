/*
 * Steam Audio 4 native renderer for vkQuake.
 * Copyright (C) 2026 vkQuake contributors. GPL-2.0-or-later.
 *
 * DSP is on the engine's mixing thread, never the SDL device callback. A worker
 * owns the scenes, simulators, probes and their inputs. The mailbox mutex only
 * protects snapshot/result copies, never a bake or a simulation. Reflection IRs
 * use the SDK's internal triple buffer; path coefficients are copied by value.
 * All voices have direct simulation and headphone or speaker spatialization. The loudest voices additionally
 * have individual reflections/pathing; the remainder feed listener reverb.
 */
#include "quakedef.h"
#include "snd_steamaudio.h"
#include "snd_crossover.h"
#include "../ThirdParty/SteamAudio/include/phonon.h"
#include "snd_steamaudio_api.h"
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#define SA_SCALE		   (0.0254f) /* Quake units -> metres (one unit = one inch). */
#define SA_ORDER		   1
#define SA_AMBI_CHANNELS   4
#define SA_MAX_REFLECTIONS 32
#define SA_IR_SECONDS	   2.0f
#define SA_CACHE_VERSION   2
#define SA_MAX_PROBES	   2048

cvar_t snd_renderer = {"snd_renderer", "0", CVAR_ARCHIVE};
cvar_t snd_steam_crossover = {"snd_steam_crossover", "80", CVAR_ARCHIVE};

int S_SteamAudio_Crossover (void)
{
	return S_Crossover_Frequency (snd_steam_crossover.value);
}

static void SA_CrossoverChanged (cvar_t *variable)
{
	const int frequency = S_SteamAudio_Crossover ();
	if (variable->value != frequency)
		Cvar_SetValueQuick (variable, frequency);
}

snd_renderer_t S_SteamAudio_Renderer (void)
{
	if (!isfinite (snd_renderer.value) || snd_renderer.value < 0 || snd_renderer.value >= SND_RENDERER_COUNT)
		return SND_RENDERER_STANDARD;
	return (snd_renderer_t)(int)snd_renderer.value;
}

const char *S_SteamAudio_RendererName (snd_renderer_t renderer)
{
	static const char *names[SND_RENDERER_COUNT] = {"Standard stereo speakers", "Steam Audio HRTF Headphones", "Steam Audio Stereo Speakers",
													"Steam Audio 4.0 Speakers", "Steam Audio 5.1 Speakers",	   "Steam Audio 7.1 Speakers"};
	return names[renderer >= 0 && renderer < SND_RENDERER_COUNT ? renderer : SND_RENDERER_STANDARD];
}

int S_SteamAudio_OutputChannels (void)
{
	static const int channels[SND_RENDERER_COUNT] = {2, 2, 2, 4, 6, 8};
	return channels[S_SteamAudio_Renderer ()];
}

static cvar_t sa_occlusion = {"snd_steam_occlusion", "1", CVAR_ARCHIVE};
static cvar_t sa_transmission = {"snd_steam_transmission", "1", CVAR_ARCHIVE};
static cvar_t sa_reflections = {"snd_steam_reflections", "1", CVAR_ARCHIVE};
static cvar_t sa_reverb = {"snd_steam_reverb", "1", CVAR_ARCHIVE};
static cvar_t sa_pathing = {"snd_steam_pathing", "1", CVAR_ARCHIVE};
static cvar_t sa_reflection_gain = {"snd_steam_reflection_gain", "0.7", CVAR_ARCHIVE};
static cvar_t sa_reverb_gain = {"snd_steam_reverb_gain", "0.25", CVAR_ARCHIVE};
static cvar_t sa_path_gain = {"snd_steam_path_gain", "0.7", CVAR_ARCHIVE};
static cvar_t sa_sources = {"snd_steam_sources", "16", CVAR_ARCHIVE};
static cvar_t sa_rays = {"snd_steam_rays", "2048", CVAR_ARCHIVE};
static cvar_t sa_bounces = {"snd_steam_bounces", "16", CVAR_ARCHIVE};
static cvar_t sa_rate = {"snd_steam_update_hz", "20", CVAR_ARCHIVE};
static cvar_t sa_spacing = {"snd_steam_probe_spacing", "128", CVAR_ARCHIVE};
static cvar_t sa_cache = {"snd_steam_cache", "1", CVAR_ARCHIVE};

typedef struct
{
	IPLVector3	*vertices;
	IPLTriangle *triangles;
	IPLint32	*materials;
	int			 num_vertices, num_triangles;
} sa_mesh_t;

typedef struct
{
	int			 model;
	IPLMatrix4x4 transform;
} sa_brush_t;

typedef struct
{
	uint64_t   serial;
	IPLVector3 origin;
	float	   attenuation, volume;
	qboolean   active, local;
} sa_source_t;

typedef struct
{
	IPLCoordinateSpace3 listener;
	sa_source_t			sources[MAX_CHANNELS];
	sa_brush_t		   *brushes;
	int					num_brushes;
	int					rays, bounces, interval;
	qboolean			occlusion, transmission, reflections, reverb, pathing;
} sa_snapshot_t;

typedef struct
{
	uint64_t			  serial;
	IPLDirectEffectParams direct;
} sa_direct_result_t;

typedef struct
{
	int						  channel;
	uint64_t				  serial;
	IPLReflectionEffectParams reflections;
	IPLPathEffectParams		  path;
	float					  sh[SA_AMBI_CHANNELS];
	qboolean				  valid, path_valid;
} sa_indirect_result_t;

typedef struct
{
	IPLBinauralEffect binaural;
	IPLPanningEffect  panning;
	IPLDirectEffect	  direct;
	snd_crossover_t	  crossover;
	uint64_t		  serial;
	IPLVector3		  direction;
	int				  tail;
} sa_voice_t;

typedef struct
{
	IPLReflectionEffect reflection;
	IPLPathEffect		path;
	uint64_t			serial;
	int					tail;
} sa_indirect_voice_t;

/* Texture names are the only material metadata available in legacy BSPs.
 * Unknown materials deliberately use a conservative masonry approximation. */
static IPLMaterial sa_materials[] = {
	{{0.10f, 0.15f, 0.20f}, 0.65f, {0.08f, 0.025f, 0.008f}}, /* masonry */
	{{0.05f, 0.07f, 0.09f}, 0.25f, {0.12f, 0.04f, 0.015f}},	 /* metal */
	{{0.15f, 0.25f, 0.35f}, 0.50f, {0.20f, 0.10f, 0.04f}},	 /* wood */
	{{0.10f, 0.05f, 0.03f}, 0.05f, {0.35f, 0.20f, 0.10f}},	 /* glass */
	{{0.25f, 0.50f, 0.65f}, 0.90f, {0.05f, 0.02f, 0.005f}},	 /* earth */
	{{0.30f, 0.50f, 0.70f}, 0.65f, {0.60f, 0.45f, 0.30f}},	 /* fence */
};

static struct
{
	void					 *library;
	IPLContext				  context;
	IPLHRTF					  hrtf;
	IPLAudioSettings		  audio;
	snd_renderer_t			  renderer;
	int						  channels;
	IPLSpeakerLayout		  speaker_layout;
	IPLAmbisonicsDecodeEffect decode;
	IPLAmbisonicsDecodeEffect bass_decode;
	snd_crossover_coeffs_t	  crossover;
	snd_crossover_t			  indirect_crossover[SA_AMBI_CHANNELS];
	int						  crossover_frequency;
	sa_voice_t				  voices[MAX_CHANNELS];
	sa_indirect_voice_t		  indirect[SA_MAX_REFLECTIONS + 1];
	SDL_Mutex				 *mutex;
	SDL_Thread				 *thread;
	qboolean				  quit, worker_done, ready, failed, cache_hit, force_rebuild;
	int						  progress, probes, triangles, instances, updates, reflection_updates, path_updates;
	char					  message[256];
	qmodel_t				 *world, *models[MAX_MODELS]; /* main-thread identity only */
	sa_mesh_t				 *meshes;
	int						  num_models, max_brushes, num_reflections;
	IPLSphere				 *probe_points;
	int						  num_probe_points;
	float					  probe_spacing;
	uint64_t				  map_hash;
	char					  cache_path[MAX_OSPATH];
	qboolean				  use_cache;
	sa_snapshot_t			  pending;
	sa_direct_result_t		  results[MAX_CHANNELS];
	sa_indirect_result_t	  indirect_results[SA_MAX_REFLECTIONS + 1];
} sa;

static uint64_t				sa_serial;
static sa_snapshot_t		sa_mix_snapshot;
static sa_direct_result_t	sa_mix_direct[MAX_CHANNELS];
static sa_indirect_result_t sa_mix_indirect[SA_MAX_REFLECTIONS + 1];
static float				sa_mono[MAX_CHANNELS][STEAMAUDIO_FRAME_SIZE];
static float				sa_reverb_input[STEAMAUDIO_FRAME_SIZE];
static float				sa_direct_buffer[STEAMAUDIO_FRAME_SIZE];
static float				sa_output[STEAMAUDIO_MAX_CHANNELS][STEAMAUDIO_FRAME_SIZE];
static float				sa_ambi[SA_AMBI_CHANNELS][STEAMAUDIO_FRAME_SIZE];
static float				sa_wet[SA_AMBI_CHANNELS][STEAMAUDIO_FRAME_SIZE];
static float				sa_mix[STEAMAUDIO_MAX_CHANNELS][STEAMAUDIO_FRAME_SIZE];

static IPLVector3 SA_Vector (const vec3_t v)
{
	/* Proper rotation: Quake +X forward,+Y left,+Z up -> SA -Z,+X right,+Y up. */
	IPLVector3 result = {-v[1], v[2], -v[0]};
	return result;
}

static IPLVector3 SA_Position (const vec3_t v)
{
	IPLVector3 p = SA_Vector (v);
	p.x *= SA_SCALE;
	p.y *= SA_SCALE;
	p.z *= SA_SCALE;
	return p;
}

static float SA_Distance (IPLVector3 a, IPLVector3 b)
{
	float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
	return sqrtf (x * x + y * y + z * z);
}

static float IPLCALL SA_Attenuation (float distance, void *user)
{
	const sa_source_t *source = user;
	return CLAMP (0.0f, 1.0f - distance * source->attenuation / SA_SCALE, 1.0f);
}

static uint64_t SA_Hash (uint64_t hash, const void *data, size_t size)
{
	const byte *bytes = data;
	for (size_t i = 0; i < size; i++)
		hash = (hash ^ bytes[i]) * UINT64_C (1099511628211);
	return hash;
}

static qboolean SA_Quit (void)
{
	qboolean quit;
	SDL_LockMutex (sa.mutex);
	quit = sa.quit;
	SDL_UnlockMutex (sa.mutex);
	return quit;
}

static void SA_Message (const char *message)
{
	SDL_LockMutex (sa.mutex);
	q_strlcpy (sa.message, message, sizeof (sa.message));
	SDL_UnlockMutex (sa.mutex);
}

static void IPLCALL SA_Log (IPLLogLevel level, const char *message)
{
	if (level <= IPL_LOGLEVEL_WARNING)
		Sys_Printf ("Steam Audio: %s\n", message);
}

static void IPLCALL SA_Progress (float progress, void *user)
{
	SDL_LockMutex (sa.mutex);
	sa.progress = (int)(100 * progress);
	SDL_UnlockMutex (sa.mutex);
}

static IPLAudioBuffer SA_Buffer (int channels, float **data)
{
	IPLAudioBuffer buffer = {channels, STEAMAUDIO_FRAME_SIZE, data};
	return buffer;
}

/* Cache wrapper validates schema, SDK version, map/settings identity, size and
 * payload checksum before handing bytes to the SDK's deserializer. */
typedef struct
{
	uint32_t magic, version, sdk, reserved;
	uint64_t map, size, checksum;
} sa_cache_header_t;

static IPLSerializedObject SA_ReadCache (const char *suffix, void **storage)
{
	char						path[MAX_OSPATH];
	sa_cache_header_t			header;
	IPLSerializedObject			object = NULL;
	IPLSerializedObjectSettings settings = {0};
	FILE					   *file;
	*storage = NULL;
	if (!sa.use_cache || sa.force_rebuild)
		return NULL;
	q_snprintf (path, sizeof (path), "%s.%s", sa.cache_path, suffix);
	file = Sys_fopen (path, "rb");
	if (!file)
		return NULL;
	if (fread (&header, sizeof (header), 1, file) != 1 || header.magic != 0x51415343 || header.version != SA_CACHE_VERSION ||
		header.sdk != STEAMAUDIO_VERSION || header.map != sa.map_hash || header.size == 0 || header.size > 512 * 1024 * 1024 ||
		Sys_filelength (file) != (qfilesize_t)(sizeof (header) + header.size))
		goto done;
	settings.size = (size_t)header.size;
	settings.data = Mem_Alloc (settings.size);
	if (fread (settings.data, settings.size, 1, file) == 1 && SA_Hash (UINT64_C (14695981039346656037), settings.data, settings.size) == header.checksum)
		iplSerializedObjectCreate (sa.context, &settings, &object);
	/* SerializedObject borrows this memory until deserialization finishes. */
	if (object)
		*storage = settings.data;
	else
		Mem_Free (settings.data);
done:
	fclose (file);
	return object;
}

static void SA_WriteCache (const char *suffix, IPLSerializedObject object)
{
	char			  path[MAX_OSPATH], temp[MAX_OSPATH];
	sa_cache_header_t header = {0};
	FILE			 *file;
	if (!sa.use_cache || SA_Quit ())
		return;
	header.magic = 0x51415343;
	header.version = SA_CACHE_VERSION;
	header.sdk = STEAMAUDIO_VERSION;
	header.map = sa.map_hash;
	header.size = iplSerializedObjectGetSize (object);
	header.checksum = SA_Hash (UINT64_C (14695981039346656037), iplSerializedObjectGetData (object), (size_t)header.size);
	q_snprintf (path, sizeof (path), "%s.%s", sa.cache_path, suffix);
#ifdef _WIN32
	q_snprintf (temp, sizeof (temp), "%s.%lu.tmp", path, (unsigned long)_getpid ());
#else
	q_snprintf (temp, sizeof (temp), "%s.%lu.tmp", path, (unsigned long)getpid ());
#endif
	file = Sys_fopen (temp, "wb");
	if (!file)
	{
		SA_Message ("cache is not writable; acoustics remain available for this session");
		return;
	}
	qboolean ok = fwrite (&header, sizeof (header), 1, file) == 1 && fwrite (iplSerializedObjectGetData (object), (size_t)header.size, 1, file) == 1;
	if (fclose (file) != 0)
		ok = false;
	if (!ok || Sys_rename (temp, path))
	{
		Sys_remove (temp);
		SA_Message ("could not save acoustic cache; will retry on the next map load");
	}
}

static int SA_Material (const msurface_t *surface)
{
	char name[17];
	q_strlcpy (name, surface->texinfo->texture->name, sizeof (name));
	for (char *c = name; *c; c++)
		*c = (char)tolower ((unsigned char)*c);
	if (surface->flags & SURF_DRAWFENCE)
		return 5;
	if (strstr (name, "metal") || strstr (name, "iron") || strstr (name, "steel"))
		return 1;
	if (strstr (name, "wood") || strstr (name, "crate") || strstr (name, "timber"))
		return 2;
	if (strstr (name, "glass") || strstr (name, "window"))
		return 3;
	if (strstr (name, "dirt") || strstr (name, "earth") || strstr (name, "grass"))
		return 4;
	return 0;
}

static void SA_ExportMesh (qmodel_t *model, sa_mesh_t *mesh)
{
	int num_vertices = 0, num_triangles = 0;
	for (int i = 0; i < model->nummodelsurfaces; i++)
	{
		msurface_t *surface = &model->surfaces[model->firstmodelsurface + i];
		if (surface->numedges < 3 || (surface->flags & (SURF_DRAWSKY | SURF_DRAWTURB)))
			continue;
		num_vertices += surface->numedges;
		num_triangles += surface->numedges - 2;
	}
	mesh->vertices = Mem_Alloc ((size_t)num_vertices * sizeof (*mesh->vertices));
	mesh->triangles = Mem_Alloc ((size_t)num_triangles * sizeof (*mesh->triangles));
	mesh->materials = Mem_Alloc ((size_t)num_triangles * sizeof (*mesh->materials));
	for (int i = 0; i < model->nummodelsurfaces; i++)
	{
		msurface_t *surface = &model->surfaces[model->firstmodelsurface + i];
		int			base = mesh->num_vertices, material;
		if (surface->numedges < 3 || (surface->flags & (SURF_DRAWSKY | SURF_DRAWTURB)))
			continue;
		material = SA_Material (surface);
		for (int j = 0; j < surface->numedges; j++)
		{
			int		 edge = model->surfedges[surface->firstedge + j];
			unsigned vertex = model->edges[abs (edge)].v[edge < 0];
			mesh->vertices[mesh->num_vertices++] = SA_Position (model->vertexes[vertex].position);
		}
		/* Check actual polygon normal: BSP face winding is opposite to the SDK's
		 * CCW convention on some loaders. Orient triangles into empty space. */
		IPLVector3 a = mesh->vertices[base], b = mesh->vertices[base + 1], c = mesh->vertices[base + 2];
		IPLVector3 normal = SA_Vector (surface->plane->normal);
		float sign = ((b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y)) * normal.x + ((b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z)) * normal.y +
					 ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * normal.z;
		qboolean reverse = (sign < 0) != ((surface->flags & SURF_PLANEBACK) != 0);
		for (int j = 1; j < surface->numedges - 1; j++)
		{
			IPLTriangle *tri = &mesh->triangles[mesh->num_triangles];
			tri->indices[0] = base;
			tri->indices[1] = base + j + (reverse ? 1 : 0);
			tri->indices[2] = base + j + (reverse ? 0 : 1);
			mesh->materials[mesh->num_triangles++] = material;
		}
	}
}

static qboolean SA_CreateScene (sa_mesh_t *mesh, IPLScene *scene)
{
	IPLSceneSettings	  settings = {0};
	IPLStaticMeshSettings geometry = {0};
	IPLStaticMesh		  object = NULL;
	settings.type = IPL_SCENETYPE_DEFAULT;
	if (iplSceneCreate (sa.context, &settings, scene) != IPL_STATUS_SUCCESS)
		return false;
	if (!mesh->num_triangles)
		return true;
	geometry.numVertices = mesh->num_vertices;
	geometry.numTriangles = mesh->num_triangles;
	geometry.vertices = mesh->vertices;
	geometry.triangles = mesh->triangles;
	geometry.materialIndices = mesh->materials;
	geometry.numMaterials = countof (sa_materials);
	geometry.materials = sa_materials;
	if (iplStaticMeshCreate (*scene, &geometry, &object) != IPL_STATUS_SUCCESS)
		return false;
	iplStaticMeshAdd (object, *scene);
	iplSceneCommit (*scene);
	iplStaticMeshRelease (&object);
	return true;
}

static qboolean SA_InsertProbe (qmodel_t *world, vec3_t point, float spacing, unsigned *seen, qboolean reservoir)
{
	IPLVector3 position = SA_Position (point);
	for (int i = 0; i < sa.num_probe_points; i++)
	{
		IPLVector3 other = sa.probe_points[i].center;
		if (SA_Distance (position, other) < spacing * SA_SCALE * 0.75f)
		{
			vec3_t	end = {-other.z / SA_SCALE, -other.x / SA_SCALE, other.y / SA_SCALE};
			trace_t trace = {0};
			trace.fraction = 1;
			trace.allsolid = true;
			SV_RecursiveHullCheck (world->hulls, point, end, &trace, CONTENTMASK_ANYSOLID);
			if (!trace.startsolid && trace.fraction == 1)
				return true; /* An existing, visible probe covers this small leaf. */
		}
	}
	unsigned index = sa.num_probe_points;
	(*seen)++;
	if (index == SA_MAX_PROBES)
	{
		if (!reservoir)
			return false;
		/* Extreme maps still sample the entire BSP, never just its first leaves.
		 * Deterministic reservoir sampling keeps the cache identity stable. */
		uint32_t random = *seen * UINT32_C (747796405) + UINT32_C (2891336453);
		random = ((random >> ((random >> 28) + 4)) ^ random) * UINT32_C (277803737);
		index = ((random >> 22) ^ random) % *seen;
		if (index >= SA_MAX_PROBES)
			return true;
	}
	else
		sa.num_probe_points++;
	sa.probe_points[index].center = position;
	sa.probe_points[index].radius = spacing * SA_SCALE * 1.75f;
	return true;
}

/* BSP leaf boxes provide bounded candidate regions even on maps with enormous
 * outside voids. Test each candidate against the real BSP, not just its box.
 * Add a valid leaf centre when a narrow corridor contains no grid points. */
static void SA_GenerateProbes (qmodel_t *world)
{
	float spacing = CLAMP (64.0f, sa_spacing.value, 512.0f);
	sa.probe_points = Mem_Alloc (SA_MAX_PROBES * sizeof (*sa.probe_points));
	for (int attempt = 0; attempt < 12; attempt++, spacing *= 1.5f)
	{
		unsigned seen = 0;
		qboolean overflow = false;
		sa.num_probe_points = 0;
		sa.probe_spacing = spacing;
		for (int l = 1; l <= world->numleafs && !overflow; l++)
		{
			mleaf_t *leaf = &world->leafs[l];
			if (leaf->contents == CONTENTS_SOLID || leaf->contents == CONTENTS_SKY)
				continue;
			qboolean found_grid = false;
			vec3_t	 p;
			for (p[0] = ceilf (leaf->minmaxs[0] / spacing) * spacing; p[0] < leaf->minmaxs[3] && !overflow; p[0] += spacing)
				for (p[1] = ceilf (leaf->minmaxs[1] / spacing) * spacing; p[1] < leaf->minmaxs[4] && !overflow; p[1] += spacing)
					for (p[2] = ceilf ((leaf->minmaxs[2] - 32) / spacing) * spacing + 32; p[2] < leaf->minmaxs[5] && !overflow; p[2] += spacing)
						if (Mod_PointInLeaf (p, world) == leaf)
						{
							found_grid = true;
							overflow = !SA_InsertProbe (world, p, spacing, &seen, attempt == 11);
						}
			if (!found_grid && !overflow)
			{
				for (int candidate = 0; candidate < 27; candidate++)
				{
					int n = candidate;
					for (int axis = 0; axis < 3; axis++, n /= 3)
						p[axis] = leaf->minmaxs[axis] + (leaf->minmaxs[axis + 3] - leaf->minmaxs[axis]) * (0.25f + 0.25f * (n % 3));
					if (Mod_PointInLeaf (p, world) == leaf)
					{
						overflow = !SA_InsertProbe (world, p, spacing, &seen, attempt == 11);
						break;
					}
				}
			}
		}
		if (!overflow)
			break;
	}
}

typedef struct
{
	IPLScene	  scene;
	IPLProbeBatch batch;
	qboolean	  finished;
} sa_bake_t;

static int SA_BakeThread (void *data)
{
	sa_bake_t		   *bake = data;
	IPLProbeBatch		batch = NULL;
	void			   *cache_data;
	IPLSerializedObject object = SA_ReadCache ("probes", &cache_data);
	if (object)
	{
		if (iplProbeBatchLoad (sa.context, object, &batch) == IPL_STATUS_SUCCESS)
		{
			/* Loading restores probes/data; commit builds the spatial lookup. */
			iplProbeBatchCommit (batch);
			SDL_LockMutex (sa.mutex);
			sa.cache_hit = true;
			sa.progress = 100;
			SDL_UnlockMutex (sa.mutex);
		}
		iplSerializedObjectRelease (&object);
		Mem_Free (cache_data);
	}
	if (!batch && !SA_Quit () && sa.num_probe_points > 0 && iplProbeBatchCreate (sa.context, &batch) == IPL_STATUS_SUCCESS)
	{
		IPLPathBakeParams params = {0};
		for (int i = 0; i < sa.num_probe_points; i++)
			iplProbeBatchAddProbe (batch, sa.probe_points[i]);
		iplProbeBatchCommit (batch);
		params.scene = bake->scene;
		params.probeBatch = batch;
		params.identifier.type = IPL_BAKEDDATATYPE_PATHING;
		params.identifier.variation = IPL_BAKEDDATAVARIATION_DYNAMIC;
		params.numSamples = 1;
		params.radius = 0.25f;
		params.threshold = 0.5f;
		params.visRange = 50.0f;
		params.pathRange = 100.0f;
		params.numThreads = CLAMP (1, SDL_GetNumLogicalCPUCores () / 2, 4);
		SA_Message ("generating pathing cache in the background");
		iplPathBakerBake (sa.context, &params, SA_Progress, NULL);
		if (!SA_Quit ())
		{
			IPLSerializedObjectSettings settings = {0};
			if (iplSerializedObjectCreate (sa.context, &settings, &object) == IPL_STATUS_SUCCESS)
			{
				iplProbeBatchSave (batch, object);
				SA_WriteCache ("probes", object);
				iplSerializedObjectRelease (&object);
			}
		}
	}
	SDL_LockMutex (sa.mutex);
	bake->batch = batch;
	bake->finished = true;
	sa.probes = batch ? iplProbeBatchGetNumProbes (batch) : 0;
	SDL_UnlockMutex (sa.mutex);
	return 0;
}

static IPLCoordinateSpace3 SA_SourceSpace (IPLVector3 origin)
{
	IPLCoordinateSpace3 space = {{1, 0, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 0}};
	space.origin = origin;
	return space;
}

static IPLSimulationInputs SA_Inputs (sa_source_t *source, const sa_snapshot_t *snapshot, IPLProbeBatch probes)
{
	IPLSimulationInputs inputs = {0};
	inputs.source = SA_SourceSpace (source->origin);
	inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_CALLBACK;
	inputs.distanceAttenuationModel.callback = SA_Attenuation;
	inputs.distanceAttenuationModel.userData = source;
	inputs.distanceAttenuationModel.dirty = IPL_TRUE;
	inputs.occlusionType = IPL_OCCLUSIONTYPE_VOLUMETRIC;
	inputs.occlusionRadius = 0.25f;
	inputs.numOcclusionSamples = 8;
	inputs.numTransmissionRays = 16;
	inputs.directFlags = IPL_DIRECTSIMULATIONFLAGS_DISTANCEATTENUATION | IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION;
	if (snapshot->occlusion)
		inputs.directFlags |= IPL_DIRECTSIMULATIONFLAGS_OCCLUSION;
	if (snapshot->transmission)
		inputs.directFlags |= IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION;
	inputs.reverbScale[0] = inputs.reverbScale[1] = inputs.reverbScale[2] = 1.0f;
	inputs.pathingProbes = probes;
	inputs.visRadius = 0.25f;
	inputs.visThreshold = 0.5f;
	inputs.visRange = 50.0f;
	inputs.pathingOrder = SA_ORDER;
	inputs.enableValidation = IPL_TRUE;
	inputs.findAlternatePaths = IPL_TRUE;
	return inputs;
}

static int SA_Worker (void *unused)
{
	IPLScene			  scene = NULL;
	IPLScene			 *scenes = Mem_Alloc (sa.num_models * sizeof (*scenes));
	IPLInstancedMesh	  world_instance = NULL;
	IPLInstancedMesh	 *instances = Mem_Alloc (sa.max_brushes * sizeof (*instances));
	sa_brush_t			 *brushes = Mem_Alloc (sa.max_brushes * sizeof (*brushes));
	IPLSimulator		  direct_sim = NULL, indirect_sim = NULL;
	IPLSource			 *direct_sources = Mem_Alloc (MAX_CHANNELS * sizeof (*direct_sources));
	IPLSource			  indirect_sources[SA_MAX_REFLECTIONS + 1] = {0};
	sa_source_t			  slot_sources[SA_MAX_REFLECTIONS + 1] = {0};
	int					  slot_channels[SA_MAX_REFLECTIONS + 1];
	double				  slot_expiry[SA_MAX_REFLECTIONS + 1] = {0};
	sa_snapshot_t		 *snapshot = Mem_Alloc (sizeof (*snapshot));
	sa_direct_result_t	 *direct_results = Mem_Alloc (sizeof (sa.results));
	sa_indirect_result_t  indirect_results[SA_MAX_REFLECTIONS + 1] = {0};
	SDL_Thread			 *bake_thread = NULL;
	sa_bake_t			  bake = {0};
	qboolean			  probes_added = false;
	IPLSceneSettings	  scene_settings = {0};
	IPLSimulationSettings settings = {0};
	IPLSerializedObject	  object = NULL;
	void				 *cache_data;
	int					  num_instances = 0;
	memset (slot_channels, -1, sizeof (slot_channels));
	snapshot->brushes = Mem_Alloc (sa.max_brushes * sizeof (*snapshot->brushes));
	scene_settings.type = IPL_SCENETYPE_DEFAULT;
	object = SA_ReadCache ("scene", &cache_data);
	if (object)
	{
		iplSceneLoad (sa.context, &scene_settings, object, NULL, NULL, &scenes[0]);
		iplSerializedObjectRelease (&object);
		Mem_Free (cache_data);
		if (scenes[0])
			iplSceneCommit (scenes[0]);
	}
	if (!scenes[0])
	{
		IPLSerializedObjectSettings serialized_settings = {0};
		if (!SA_CreateScene (&sa.meshes[0], &scenes[0]))
			goto failed;
		if (iplSerializedObjectCreate (sa.context, &serialized_settings, &object) == IPL_STATUS_SUCCESS)
		{
			iplSceneSave (scenes[0], object);
			SA_WriteCache ("scene", object);
			iplSerializedObjectRelease (&object);
		}
	}
	for (int i = 1; i < sa.num_models; i++)
	{
		if (SA_Quit ())
			goto done;
		if (!SA_CreateScene (&sa.meshes[i], &scenes[i]))
			goto failed;
	}
	if (iplSceneCreate (sa.context, &scene_settings, &scene) != IPL_STATUS_SUCCESS)
		goto failed;
	IPLInstancedMeshSettings instance_settings = {0};
	instance_settings.subScene = scenes[0];
	for (int i = 0; i < 4; i++)
		instance_settings.transform.elements[i][i] = 1;
	if (iplInstancedMeshCreate (scene, &instance_settings, &world_instance) != IPL_STATUS_SUCCESS)
		goto failed;
	iplInstancedMeshAdd (world_instance, scene);
	iplSceneCommit (scene);
	settings.flags = IPL_SIMULATIONFLAGS_DIRECT;
	settings.sceneType = IPL_SCENETYPE_DEFAULT;
	settings.maxNumOcclusionSamples = 8;
	settings.samplingRate = sa.audio.samplingRate;
	settings.frameSize = STEAMAUDIO_FRAME_SIZE;
	if (iplSimulatorCreate (sa.context, &settings, &direct_sim) != IPL_STATUS_SUCCESS)
		goto failed;
	iplSimulatorSetScene (direct_sim, scene);
	settings.flags = IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING;
	settings.reflectionType = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
	settings.maxNumRays = 8192;
	settings.numDiffuseSamples = 32;
	settings.maxDuration = SA_IR_SECONDS;
	settings.maxOrder = SA_ORDER;
	settings.maxNumSources = sa.num_reflections + 1;
	settings.numThreads = CLAMP (1, SDL_GetNumLogicalCPUCores () / 2, 4);
	settings.numVisSamples = 1;
	if (iplSimulatorCreate (sa.context, &settings, &indirect_sim) != IPL_STATUS_SUCCESS)
		goto failed;
	iplSimulatorSetScene (indirect_sim, scene);
	for (int i = 0; i <= sa.num_reflections; i++)
	{
		IPLSourceSettings source_settings = {IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING};
		if (iplSourceCreate (indirect_sim, &source_settings, &indirect_sources[i]) != IPL_STATUS_SUCCESS)
			goto failed;
		iplSourceAdd (indirect_sources[i], indirect_sim);
	}
	iplSimulatorCommit (indirect_sim);
	bake.scene = scenes[0];
	bake_thread = SDL_CreateThread (SA_BakeThread, "Steam Audio bake", &bake);
	if (!bake_thread)
		SA_Message ("could not start pathing bake; direct sound and reflections remain active");
	SDL_LockMutex (sa.mutex);
	sa.ready = true;
	SDL_UnlockMutex (sa.mutex);
	while (!SA_Quit ())
	{
		double					  started = Sys_DoubleTime ();
		IPLSimulationSharedInputs shared = {0};
		SDL_LockMutex (sa.mutex);
		sa_brush_t *snapshot_brushes = snapshot->brushes;
		*snapshot = sa.pending;
		snapshot->brushes = snapshot_brushes;
		memcpy (snapshot->brushes, sa.pending.brushes, sa.pending.num_brushes * sizeof (*snapshot->brushes));
		qboolean bake_finished = bake.finished;
		SDL_UnlockMutex (sa.mutex);
		if (bake_finished && !probes_added)
		{
			if (bake.batch)
			{
				iplSimulatorAddProbeBatch (indirect_sim, bake.batch);
				iplSimulatorCommit (indirect_sim);
			}
			probes_added = true;
			SA_Message (bake.batch ? "pathing cache ready" : "pathing data unavailable for this map");
		}
		qboolean scene_changed = false;
		for (int i = 0; i < sa.max_brushes; i++)
		{
			sa_brush_t *next = &snapshot->brushes[i];
			int			model = (i < snapshot->num_brushes) ? next->model : 0;
			if (model != brushes[i].model)
			{
				if (instances[i])
				{
					iplInstancedMeshRemove (instances[i], scene);
					iplInstancedMeshRelease (&instances[i]);
					num_instances--;
				}
				if (model > 0 && model < sa.num_models)
				{
					IPLInstancedMeshSettings inst = {0};
					inst.subScene = scenes[model];
					inst.transform = next->transform;
					if (iplInstancedMeshCreate (scene, &inst, &instances[i]) == IPL_STATUS_SUCCESS)
					{
						iplInstancedMeshAdd (instances[i], scene);
						num_instances++;
					}
				}
				brushes[i].model = model;
				scene_changed = true;
			}
			if (instances[i] && memcmp (&brushes[i].transform, &next->transform, sizeof (next->transform)))
			{
				iplInstancedMeshUpdateTransform (instances[i], scene, next->transform);
				brushes[i].transform = next->transform;
				scene_changed = true;
			}
		}
		if (scene_changed)
			iplSceneCommit (scene);
		qboolean sources_changed = false;
		for (int i = 0; i < MAX_CHANNELS; i++)
		{
			sa_source_t *source = &snapshot->sources[i];
			if (source->active && !source->local && !direct_sources[i])
			{
				IPLSourceSettings source_settings = {IPL_SIMULATIONFLAGS_DIRECT};
				if (iplSourceCreate (direct_sim, &source_settings, &direct_sources[i]) != IPL_STATUS_SUCCESS)
					continue;
				iplSourceAdd (direct_sources[i], direct_sim);
				sources_changed = true;
			}
			if (direct_sources[i])
			{
				IPLSimulationInputs inputs = SA_Inputs (source, snapshot, NULL);
				inputs.flags = source->active && !source->local ? IPL_SIMULATIONFLAGS_DIRECT : 0;
				iplSourceSetInputs (direct_sources[i], IPL_SIMULATIONFLAGS_DIRECT, &inputs);
			}
		}
		if (sources_changed)
			iplSimulatorCommit (direct_sim);
		shared.listener = snapshot->listener;
		shared.numRays = snapshot->rays;
		shared.numBounces = snapshot->bounces;
		shared.duration = SA_IR_SECONDS;
		shared.order = SA_ORDER;
		shared.irradianceMinDistance = 0.5f;
		iplSimulatorSetSharedInputs (direct_sim, IPL_SIMULATIONFLAGS_DIRECT, &shared);
		iplSimulatorRunDirect (direct_sim);
		memset (direct_results, 0, sizeof (sa.results));
		for (int i = 0; i < MAX_CHANNELS; i++)
			if (direct_sources[i] && snapshot->sources[i].active && !snapshot->sources[i].local)
			{
				IPLSimulationOutputs outputs = {0};
				iplSourceGetOutputs (direct_sources[i], IPL_SIMULATIONFLAGS_DIRECT, &outputs);
				direct_results[i].serial = snapshot->sources[i].serial;
				direct_results[i].direct = outputs.direct;
			}
		SDL_LockMutex (sa.mutex);
		memcpy (sa.results, direct_results, sizeof (sa.results));
		sa.updates++;
		sa.instances = num_instances;
		SDL_UnlockMutex (sa.mutex);

		/* Choose individual indirect voices by audibility, keeping existing slots
		 * stable. All other voices still have direct simulation and HRTF. */
		int	  selected[SA_MAX_REFLECTIONS];
		float scores[SA_MAX_REFLECTIONS] = {0};
		memset (selected, -1, sizeof (selected));
		for (int i = NUM_AMBIENTS; i < MAX_CHANNELS; i++)
		{
			sa_source_t *source = &snapshot->sources[i];
			if (!source->active || source->local)
				continue;
			float score = source->volume * SA_Attenuation (SA_Distance (source->origin, snapshot->listener.origin), source);
			for (int j = 0; j < sa.num_reflections; j++)
				if (score > scores[j])
				{
					for (int k = sa.num_reflections - 1; k > j; k--)
					{
						selected[k] = selected[k - 1];
						scores[k] = scores[k - 1];
					}
					selected[j] = i;
					scores[j] = score;
					break;
				}
		}
		qboolean keep[SA_MAX_REFLECTIONS] = {0};
		for (int i = 0; i < sa.num_reflections; i++)
			for (int j = 0; j < sa.num_reflections; j++)
				if (selected[j] >= 0 && selected[j] == slot_channels[i] && snapshot->sources[selected[j]].serial == slot_sources[i].serial)
					keep[i] = true;
		for (int j = 0; j < sa.num_reflections; j++)
		{
			int channel = selected[j], slot = -1;
			if (channel < 0)
				continue;
			for (int i = 0; i < sa.num_reflections; i++)
				if (keep[i] && slot_channels[i] == channel)
					slot = i;
			if (slot < 0)
				for (int i = 0; i < sa.num_reflections; i++)
					if (!keep[i])
					{
						slot = i;
						keep[i] = true;
						break;
					}
			if (slot >= 0)
			{
				slot_channels[slot] = channel;
				slot_sources[slot] = snapshot->sources[channel];
				slot_expiry[slot] = started + SA_IR_SECONDS;
			}
		}
		for (int i = 0; i <= sa.num_reflections; i++)
		{
			qboolean reverb = i == sa.num_reflections;
			if (reverb)
			{
				slot_sources[i].origin = snapshot->listener.origin;
				slot_sources[i].serial = 1;
			}
			IPLSimulationInputs inputs = SA_Inputs (&slot_sources[i], snapshot, probes_added ? bake.batch : NULL);
			// Direct sound keeps Quake's gameplay attenuation. Reflected/pathing
			// energy uses the SDK's physical falloff, including long reverb tails.
			inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_INVERSEDISTANCE;
			inputs.distanceAttenuationModel.minDistance = 1.0f;
			inputs.flags = 0;
			if (reverb ? snapshot->reverb : (snapshot->reflections && started < slot_expiry[i]))
				inputs.flags |= IPL_SIMULATIONFLAGS_REFLECTIONS;
			if (!reverb && keep[i] && snapshot->pathing && probes_added && bake.batch)
				inputs.flags |= IPL_SIMULATIONFLAGS_PATHING;
			iplSourceSetInputs (indirect_sources[i], IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING, &inputs);
		}
		iplSimulatorSetSharedInputs (indirect_sim, IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING, &shared);
		if (snapshot->reflections || snapshot->reverb)
			iplSimulatorRunReflections (indirect_sim);
		if (snapshot->pathing && probes_added && bake.batch)
			iplSimulatorRunPathing (indirect_sim);
		memset (indirect_results, 0, sizeof (indirect_results));
		for (int i = 0; i <= sa.num_reflections; i++)
		{
			IPLSimulationOutputs  outputs = {0};
			sa_indirect_result_t *result = &indirect_results[i];
			iplSourceGetOutputs (indirect_sources[i], IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING, &outputs);
			result->channel = slot_channels[i];
			result->serial = slot_sources[i].serial;
			result->reflections = outputs.reflections;
			result->reflections.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
			result->path = outputs.pathing;
			if (outputs.pathing.shCoeffs)
				memcpy (result->sh, outputs.pathing.shCoeffs, sizeof (result->sh));
			result->path.shCoeffs = NULL; /* Never publish an SDK-owned array. */
			result->valid = (i == sa.num_reflections) ? snapshot->reverb : (snapshot->reflections && started < slot_expiry[i]);
			result->path_valid = i < sa.num_reflections && keep[i] && snapshot->pathing && probes_added && bake.batch;
		}
		SDL_LockMutex (sa.mutex);
		memcpy (sa.indirect_results, indirect_results, sizeof (sa.indirect_results));
		sa.reflection_updates += snapshot->reflections || snapshot->reverb;
		sa.path_updates += snapshot->pathing && probes_added && bake.batch;
		SDL_UnlockMutex (sa.mutex);
		int delay = snapshot->interval - (int)((Sys_DoubleTime () - started) * 1000);
		for (int i = 0; i < delay && !SA_Quit (); i += 5)
			SDL_Delay (5);
	}
	goto done;
failed:
	SDL_LockMutex (sa.mutex);
	sa.failed = true;
	sa.quit = true;
	SDL_UnlockMutex (sa.mutex);
	SA_Message ("could not create acoustic simulation; using Standard stereo speakers");
done:
	if (bake_thread)
	{
		/* Cancellation may race the SDK entering Bake. Repeat until completion
		 * so a map change cannot accidentally wait for an entire new bake. */
		for (;;)
		{
			SDL_LockMutex (sa.mutex);
			qboolean finished = bake.finished;
			SDL_UnlockMutex (sa.mutex);
			if (finished)
				break;
			iplPathBakerCancelBake (sa.context);
			SDL_Delay (10);
		}
		SDL_WaitThread (bake_thread, NULL);
	}
	for (int i = 0; i < MAX_CHANNELS; i++)
		iplSourceRelease (&direct_sources[i]);
	for (int i = 0; i <= sa.num_reflections; i++)
		iplSourceRelease (&indirect_sources[i]);
	iplSimulatorRelease (&direct_sim);
	iplSimulatorRelease (&indirect_sim);
	iplProbeBatchRelease (&bake.batch);
	for (int i = 0; i < sa.max_brushes; i++)
		iplInstancedMeshRelease (&instances[i]);
	iplInstancedMeshRelease (&world_instance);
	iplSceneRelease (&scene);
	for (int i = 0; i < sa.num_models; i++)
		iplSceneRelease (&scenes[i]);
	Mem_Free (snapshot->brushes);
	Mem_Free (snapshot);
	Mem_Free (direct_results);
	Mem_Free (direct_sources);
	Mem_Free (brushes);
	Mem_Free (instances);
	Mem_Free (scenes);
	SDL_LockMutex (sa.mutex);
	sa.worker_done = true;
	SDL_UnlockMutex (sa.mutex);
	return 0;
}

/* Match the brush renderer's pitch correction, column-major matrix and scale.
 * C is the Quake-to-Steam-Audio rotation; the SDK matrix is C * M * inverse(C).
 * Only translations need the inches-to-metres conversion. */
static void SA_EntityMatrix (entity_t *entity, float matrix[16])
{
	vec3_t				  angles, origin;
	const entity_state_t *state = entity->update_type ? &entity->netstate : &entity->baseline;
	unsigned char		  scale = state->scale ? state->scale : ENTSCALE_DEFAULT;
	if (entity->model)
	{
		VectorCopy (entity->angles, angles);
		VectorCopy (entity->origin, origin);
	}
	else
	{
		VectorCopy (state->angles, angles);
		VectorCopy (state->origin, origin);
	}
	int model = state->modelindex;
	if (model > 0 && model < MAX_MODELS && cl.model_precache[model] && cl.model_precache[model]->type == mod_brush)
		angles[0] = -angles[0];
	IdentityMatrix (matrix);
	R_RotateForEntity (matrix, origin, angles, scale);
}

static IPLMatrix4x4 SA_Transform (entity_t *entity)
{
	float		 matrix[16];
	const int	 axis[3] = {1, 2, 0};
	const float	 sign[3] = {-1, 1, -1};
	IPLMatrix4x4 transform = {0};
	SA_EntityMatrix (entity, matrix);
	for (int row = 0; row < 3; row++)
	{
		for (int col = 0; col < 3; col++)
			transform.elements[row][col] = sign[row] * sign[col] * matrix[axis[col] * 4 + axis[row]];
		transform.elements[row][3] = sign[row] * matrix[12 + axis[row]] * SA_SCALE;
	}
	transform.elements[3][3] = 1;
	return transform;
}

void S_SteamAudio_StartSound (channel_t *channel)
{
	channel->sa_serial = ++sa_serial;
	channel->sa_modelindex = 0;
	if (channel->entnum > 0 && channel->entnum < cl.num_entities && cl.entities)
	{
		entity_t *entity = &cl.entities[channel->entnum];
		float	  matrix[16], scale_squared;
		SA_EntityMatrix (entity, matrix);
		scale_squared = matrix[0] * matrix[0] + matrix[1] * matrix[1] + matrix[2] * matrix[2];
		if (scale_squared < 1e-8f)
			return;
		channel->sa_modelindex = entity->update_type ? entity->netstate.modelindex : entity->baseline.modelindex;
		for (int i = 0; i < 3; i++)
		{
			channel->sa_offset[i] = 0;
			for (int j = 0; j < 3; j++)
				channel->sa_offset[i] += matrix[i * 4 + j] * (channel->origin[j] - matrix[12 + j]) / scale_squared;
		}
	}
}

static IPLVector3 SA_ChannelPosition (channel_t *channel)
{
	vec3_t origin;
	VectorCopy (channel->origin, origin);
	if (channel->sa_modelindex && channel->entnum > 0 && channel->entnum < cl.num_entities && cl.entities)
	{
		entity_t *entity = &cl.entities[channel->entnum];
		int		  model = entity->update_type ? entity->netstate.modelindex : entity->baseline.modelindex;
		if (model == channel->sa_modelindex)
		{
			float matrix[16];
			SA_EntityMatrix (entity, matrix);
			for (int i = 0; i < 3; i++)
				origin[i] = matrix[12 + i] + matrix[i] * channel->sa_offset[0] + matrix[4 + i] * channel->sa_offset[1] + matrix[8 + i] * channel->sa_offset[2];
		}
	}
	return SA_Position (origin);
}

static void SA_SnapshotBrush (entity_t *entity, sa_brush_t *brush)
{
	const entity_state_t *state = entity->update_type ? &entity->netstate : &entity->baseline;
	int					  modelindex = state->modelindex;
	brush->model = 0;
	if (modelindex <= 0 || modelindex >= MAX_MODELS)
		return;
	qmodel_t *model = cl.model_precache[modelindex];
	if (!model || model == cl.worldmodel || model->type != mod_brush)
		return;
	/* PVS culling clears entity->model without a remove message. Keep the last
	 * network transform for acoustics; an explicit modelindex=0 removes it. */
	for (int i = 1; i < sa.num_models; i++)
		if (sa.models[i] == model)
		{
			brush->model = i;
			brush->transform = SA_Transform (entity);
			return;
		}
}

static void SA_Snapshot (void)
{
	sa_snapshot_t *snapshot = &sa_mix_snapshot;
	snapshot->listener = SA_SourceSpace (SA_Position (listener_origin));
	if (DotProduct (listener_forward, listener_forward) > 0.5f)
	{
		snapshot->listener.right = SA_Vector (listener_right);
		snapshot->listener.up = SA_Vector (listener_up);
		snapshot->listener.ahead = SA_Vector (listener_forward);
	}
	snapshot->occlusion = sa_occlusion.value != 0;
	snapshot->transmission = sa_transmission.value != 0;
	snapshot->reflections = sa_reflections.value != 0;
	snapshot->reverb = sa_reverb.value != 0;
	snapshot->pathing = sa_pathing.value != 0;
	snapshot->rays = CLAMP (256, (int)sa_rays.value, 8192);
	snapshot->bounces = CLAMP (1, (int)sa_bounces.value, 64);
	snapshot->interval = 1000 / CLAMP (5, (int)sa_rate.value, 60);
	memset (snapshot->sources, 0, sizeof (snapshot->sources));
	for (int i = 0; i < total_channels; i++)
	{
		channel_t	*channel = &snd_channels[i];
		sa_source_t *source = &snapshot->sources[i];
		source->active = channel->sfx != NULL;
		source->serial = channel->sa_serial;
		source->local = i < NUM_AMBIENTS || channel->entnum == cl.viewentity;
		source->origin = source->local ? snapshot->listener.origin : SA_ChannelPosition (channel);
		source->volume = channel->master_vol / 255.0f;
		source->attenuation = source->local ? 0 : channel->dist_mult;
		if (!source->local && (source->volume <= 0 || SA_Attenuation (SA_Distance (source->origin, snapshot->listener.origin), source) <= 0))
			source->active = false;
	}
	if (!sa.world)
		return;
	snapshot->num_brushes = q_min (cl.max_edicts + cl.num_statics, sa.max_brushes);
	memset (snapshot->brushes, 0, sa.max_brushes * sizeof (*snapshot->brushes));
	for (int i = 1; i < cl.num_entities; i++)
		SA_SnapshotBrush (&cl.entities[i], &snapshot->brushes[i]);
	for (int i = 0; i < cl.num_statics && cl.max_edicts + i < sa.max_brushes; i++)
		SA_SnapshotBrush (cl.static_entities[i], &snapshot->brushes[cl.max_edicts + i]);
	SDL_LockMutex (sa.mutex);
	sa_brush_t *brushes = sa.pending.brushes;
	sa.pending = *snapshot;
	sa.pending.brushes = brushes;
	memcpy (brushes, snapshot->brushes, snapshot->num_brushes * sizeof (*brushes));
	SDL_UnlockMutex (sa.mutex);
}

static void SA_StartMap (void)
{
	sa.world = cl.worldmodel;
	sa.num_models = 1;
	sa.models[0] = sa.world;
	for (int i = 1; i < MAX_MODELS; i++)
	{
		qmodel_t *model = cl.model_precache[i];
		if (!model || model == sa.world || model->type != mod_brush || model->needload)
			continue;
		qboolean found = false;
		for (int j = 0; j < sa.num_models; j++)
			if (sa.models[j] == model)
				found = true;
		if (!found && sa.num_models < MAX_MODELS)
			sa.models[sa.num_models++] = model;
	}
	sa.meshes = Mem_Alloc (sa.num_models * sizeof (*sa.meshes));
	sa.map_hash = SA_Hash (UINT64_C (14695981039346656037), sa_materials, sizeof (sa_materials));
	for (int i = 0; i < sa.num_models; i++)
	{
		sa_mesh_t *mesh = &sa.meshes[i];
		SA_ExportMesh (sa.models[i], mesh);
		sa.map_hash = SA_Hash (sa.map_hash, &mesh->num_vertices, sizeof (mesh->num_vertices));
		sa.map_hash = SA_Hash (sa.map_hash, &mesh->num_triangles, sizeof (mesh->num_triangles));
		sa.map_hash = SA_Hash (sa.map_hash, mesh->vertices, mesh->num_vertices * sizeof (*mesh->vertices));
		sa.map_hash = SA_Hash (sa.map_hash, mesh->triangles, mesh->num_triangles * sizeof (*mesh->triangles));
		sa.map_hash = SA_Hash (sa.map_hash, mesh->materials, mesh->num_triangles * sizeof (*mesh->materials));
		sa.triangles += mesh->num_triangles;
	}
	SA_GenerateProbes (sa.world);
	sa.map_hash = SA_Hash (sa.map_hash, sa.probe_points, sa.num_probe_points * sizeof (*sa.probe_points));
	sa.use_cache = sa_cache.value != 0;
	if (q_snprintf (sa.cache_path, sizeof (sa.cache_path) - 32, "%s/steamaudio/%016llx-v%d", com_gamedir, (unsigned long long)sa.map_hash, SA_CACHE_VERSION) >=
		sizeof (sa.cache_path) - 32)
		sa.use_cache = false;
	sa.max_brushes = cl.max_edicts + cl.num_statics + 256;
	sa.pending.brushes = Mem_Alloc (sa.max_brushes * sizeof (*sa.pending.brushes));
	sa_mix_snapshot.brushes = Mem_Alloc (sa.max_brushes * sizeof (*sa_mix_snapshot.brushes));
	sa.quit = sa.worker_done = sa.failed = false;
	SA_Snapshot ();
	sa.thread = SDL_CreateThread (SA_Worker, "Steam Audio simulation", NULL);
	if (!sa.thread)
		sa.failed = true;
	Con_Printf (
		"Steam Audio: %d triangles, %d brush models, %d probes (%g unit spacing)\n", sa.triangles, sa.num_models - 1, sa.num_probe_points, sa.probe_spacing);
}

void S_SteamAudio_Reset (void)
{
	if (!sa.context)
		return;
	for (int i = 0; i < MAX_CHANNELS; i++)
	{
		if (sa.voices[i].binaural)
			iplBinauralEffectReset (sa.voices[i].binaural);
		if (sa.voices[i].panning)
			iplPanningEffectReset (sa.voices[i].panning);
		if (sa.voices[i].direct)
			iplDirectEffectReset (sa.voices[i].direct);
		sa.voices[i].serial = 0;
		sa.voices[i].tail = 0;
		S_Crossover_Reset (&sa.voices[i].crossover);
	}
	for (int i = 0; i <= sa.num_reflections; i++)
	{
		if (sa.indirect[i].reflection)
			iplReflectionEffectReset (sa.indirect[i].reflection);
		if (sa.indirect[i].path)
			iplPathEffectReset (sa.indirect[i].path);
		sa.indirect[i].serial = 0;
		sa.indirect[i].tail = 0;
	}
	if (sa.decode)
		iplAmbisonicsDecodeEffectReset (sa.decode);
	if (sa.bass_decode)
		iplAmbisonicsDecodeEffectReset (sa.bass_decode);
	memset (sa.indirect_crossover, 0, sizeof (sa.indirect_crossover));
	sa.crossover_frequency = 0;
}

void S_SteamAudio_ClearMap (void)
{
	if (sa.thread)
	{
		SDL_LockMutex (sa.mutex);
		sa.quit = true;
		SDL_UnlockMutex (sa.mutex);
		SDL_WaitThread (sa.thread, NULL);
		sa.thread = NULL;
	}
	S_SteamAudio_Reset ();
	for (int i = 0; i < sa.num_models; i++)
	{
		Mem_Free (sa.meshes[i].vertices);
		Mem_Free (sa.meshes[i].triangles);
		Mem_Free (sa.meshes[i].materials);
	}
	Mem_Free (sa.meshes);
	Mem_Free (sa.probe_points);
	Mem_Free (sa.pending.brushes);
	Mem_Free (sa_mix_snapshot.brushes);
	sa.meshes = NULL;
	sa.probe_points = NULL;
	sa.pending.brushes = NULL;
	sa_mix_snapshot.brushes = NULL;
	sa.world = NULL;
	sa.num_models = sa.num_probe_points = sa.triangles = sa.probes = sa.instances = 0;
	sa.updates = sa.reflection_updates = sa.path_updates = sa.progress = 0;
	sa.ready = sa.cache_hit = sa.failed = sa.force_rebuild = false;
	sa.message[0] = 0;
	memset (sa.models, 0, sizeof (sa.models));
	memset (sa.results, 0, sizeof (sa.results));
	memset (sa.indirect_results, 0, sizeof (sa.indirect_results));
}

void S_SteamAudio_Shutdown (void)
{
	S_SteamAudio_ClearMap ();
	if (sa.library && sa.context)
	{
		for (int i = 0; i < MAX_CHANNELS; i++)
		{
			iplBinauralEffectRelease (&sa.voices[i].binaural);
			iplPanningEffectRelease (&sa.voices[i].panning);
			iplDirectEffectRelease (&sa.voices[i].direct);
		}
		for (int i = 0; i <= sa.num_reflections; i++)
		{
			iplReflectionEffectRelease (&sa.indirect[i].reflection);
			iplPathEffectRelease (&sa.indirect[i].path);
		}
		iplAmbisonicsDecodeEffectRelease (&sa.decode);
		iplAmbisonicsDecodeEffectRelease (&sa.bass_decode);
		iplHRTFRelease (&sa.hrtf);
		iplContextRelease (&sa.context);
	}
	if (sa.mutex)
		SDL_DestroyMutex (sa.mutex);
	if (sa.library)
		SDL_UnloadObject (sa.library);
	memset (&sa, 0, sizeof (sa));
}

static qboolean SA_Init (void)
{
	char path[MAX_OSPATH];
#ifdef _WIN32
	const char *filename = "phonon.dll";
#elif defined(__APPLE__)
	const char *filename = "libphonon.dylib";
#else
	const char *filename = "libphonon.so";
#endif
	const char *base = SDL_GetBasePath ();
	q_snprintf (path, sizeof (path), "%s%s", base ? base : "", filename);
	sa.library = SDL_LoadObject (path);
#ifndef USE_SDL3
	SDL_free ((void *)base);
#endif
	if (!sa.library)
		sa.library = SDL_LoadObject (filename);
	if (!sa.library)
	{
		Con_Warning ("Steam Audio runtime unavailable (%s): %s. Using Standard stereo speakers.\n", filename, SDL_GetError ());
		return false;
	}
	if (!SA_LoadFunctions (sa.library))
	{
		SDL_UnloadObject (sa.library);
		sa.library = NULL;
		return false;
	}
	IPLContextSettings context_settings = {0};
	context_settings.version = STEAMAUDIO_VERSION;
	context_settings.logCallback = SA_Log;
	context_settings.simdLevel = IPL_SIMDLEVEL_AVX2;
	if (iplContextCreate (&context_settings, &sa.context) != IPL_STATUS_SUCCESS)
		goto failed;
	sa.audio.samplingRate = shm->speed;
	sa.audio.frameSize = STEAMAUDIO_FRAME_SIZE;
	sa.renderer = S_SteamAudio_Renderer ();
	sa.channels = S_SteamAudio_OutputChannels ();
	const IPLSpeakerLayoutType layouts[SND_RENDERER_COUNT] = {IPL_SPEAKERLAYOUTTYPE_STEREO,		  IPL_SPEAKERLAYOUTTYPE_STEREO,
															  IPL_SPEAKERLAYOUTTYPE_STEREO,		  IPL_SPEAKERLAYOUTTYPE_QUADRAPHONIC,
															  IPL_SPEAKERLAYOUTTYPE_SURROUND_5_1, IPL_SPEAKERLAYOUTTYPE_SURROUND_7_1};
	sa.speaker_layout.type = layouts[sa.renderer];
	if (sa.renderer == SND_RENDERER_HRTF)
	{
		IPLHRTFSettings hrtf_settings = {0};
		hrtf_settings.type = IPL_HRTFTYPE_DEFAULT;
		hrtf_settings.volume = 1;
		if (iplHRTFCreate (sa.context, &sa.audio, &hrtf_settings, &sa.hrtf) != IPL_STATUS_SUCCESS)
			goto failed;
	}
	IPLAmbisonicsDecodeEffectSettings decode_settings = {0};
	decode_settings.speakerLayout = sa.speaker_layout;
	decode_settings.hrtf = sa.hrtf;
	decode_settings.maxOrder = SA_ORDER;
	if (iplAmbisonicsDecodeEffectCreate (sa.context, &sa.audio, &decode_settings, &sa.decode) != IPL_STATUS_SUCCESS)
		goto failed;
	if (sa.channels == 6 || sa.channels == 8)
	{
		decode_settings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_MONO;
		if (iplAmbisonicsDecodeEffectCreate (sa.context, &sa.audio, &decode_settings, &sa.bass_decode) != IPL_STATUS_SUCCESS)
			goto failed;
	}
	sa.num_reflections = CLAMP (1, (int)sa_sources.value, SA_MAX_REFLECTIONS);
	for (int i = 0; i <= sa.num_reflections; i++)
	{
		IPLReflectionEffectSettings reflection_settings = {0};
		reflection_settings.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
		reflection_settings.irSize = (int)(SA_IR_SECONDS * shm->speed);
		reflection_settings.numChannels = SA_AMBI_CHANNELS;
		if (iplReflectionEffectCreate (sa.context, &sa.audio, &reflection_settings, &sa.indirect[i].reflection) != IPL_STATUS_SUCCESS)
			goto failed;
		IPLPathEffectSettings path_settings = {0};
		path_settings.maxOrder = SA_ORDER;
		if (iplPathEffectCreate (sa.context, &sa.audio, &path_settings, &sa.indirect[i].path) != IPL_STATUS_SUCCESS)
			goto failed;
	}
	sa.mutex = SDL_CreateMutex ();
	if (!sa.mutex)
		goto failed;
	Con_Printf (
		"Audio renderer: %s (native SDK %d.%d.%d, %d channels, %d Hz%s)\n", S_SteamAudio_RendererName (sa.renderer), STEAMAUDIO_VERSION_MAJOR,
		STEAMAUDIO_VERSION_MINOR, STEAMAUDIO_VERSION_PATCH, sa.channels, shm->speed, sa.renderer == SND_RENDERER_HRTF ? ", built-in HRTF" : "");
	return true;
failed:
	Con_Warning ("Steam Audio initialization failed. Using Standard stereo speakers.\n");
	S_SteamAudio_Shutdown ();
	return false;
}

qboolean S_SteamAudio_Active (void)
{
	return sa.context != NULL;
}

void S_SteamAudio_Update (void)
{
	if (!shm)
		return;
	snd_renderer_t renderer = S_SteamAudio_Renderer ();
	if (snd_renderer.value != renderer)
		Cvar_SetValueQuick (&snd_renderer, renderer);
	if (renderer == SND_RENDERER_STANDARD)
	{
		if (sa.context)
		{
			S_SteamAudio_Shutdown ();
			S_ClearBuffer ();
			Con_Printf ("Audio renderer: %s\n", S_SteamAudio_RendererName (renderer));
		}
		if (snd_renderer.value != 0)
			Cvar_SetValueQuick (&snd_renderer, 0);
		return;
	}
	if (sa.context &&
		(sa.renderer != renderer || sa.audio.samplingRate != shm->speed || sa.num_reflections != CLAMP (1, (int)sa_sources.value, SA_MAX_REFLECTIONS)))
		S_SteamAudio_Shutdown ();
	if (!sa.context)
	{
		if (!SA_Init ())
		{
			Cvar_SetValueQuick (&snd_renderer, 0);
			return;
		}
		S_ClearBuffer ();
	}
	qboolean changed = sa.world != cl.worldmodel;
	if (sa.world)
	{
		changed |= cl.max_edicts + cl.num_statics > sa.max_brushes;
		for (int i = 1; i < MAX_MODELS && !changed; i++)
		{
			qmodel_t *model = cl.model_precache[i];
			if (!model || model->type != mod_brush || model->needload)
				continue;
			int j;
			for (j = 0; j < sa.num_models && sa.models[j] != model; j++)
				;
			changed = j == sa.num_models;
		}
	}
	if (changed)
	{
		S_SteamAudio_ClearMap ();
		if (cl.worldmodel && cls.state == ca_connected)
			SA_StartMap ();
	}
	SA_Snapshot ();
	SDL_LockMutex (sa.mutex);
	qboolean failed = sa.failed;
	if (sa.message[0])
	{
		Con_Printf ("Steam Audio: %s\n", sa.message);
		sa.message[0] = 0;
	}
	SDL_UnlockMutex (sa.mutex);
	if (failed)
	{
		S_SteamAudio_Shutdown ();
		Cvar_SetValueQuick (&snd_renderer, 0);
	}
}

static void SA_Info (void)
{
	if (!sa.context)
	{
		Con_Printf ("Audio renderer: %s\n", S_SteamAudio_RendererName (SND_RENDERER_STANDARD));
		return;
	}
	SDL_LockMutex (sa.mutex);
	Con_Printf (
		"%s: %d Hz, %d samples, %d channels, %d indirect voices + listener reverb\n", S_SteamAudio_RendererName (sa.renderer), sa.audio.samplingRate,
		STEAMAUDIO_FRAME_SIZE, sa.channels, sa.num_reflections);
	if (sa.bass_decode)
		Con_Printf ("Bass crossover: %d Hz, 24 dB/octave Linkwitz-Riley\n", S_SteamAudio_Crossover ());
	Con_Printf (
		"Acoustics: %d triangles, %d moving brushes, %d probes; %s; cache %s (%d%%)\n", sa.triangles, sa.instances, sa.probes,
		sa.ready ? "simulation ready" : "preparing scene", sa.cache_hit ? "loaded" : "generated", sa.progress);
	Con_Printf ("Simulation updates: direct %d, reflections %d, pathing %d\n", sa.updates, sa.reflection_updates, sa.path_updates);
	Con_Printf ("Cache: %s\n", sa.cache_path);
	SDL_UnlockMutex (sa.mutex);
}

static void SA_Rebuild (void)
{
	if (!sa.context)
		return;
	S_SteamAudio_ClearMap ();
	sa.force_rebuild = true;
	if (cl.worldmodel)
		SA_StartMap ();
}

void S_SteamAudio_Register (void)
{
	cvar_t *variables[] = {&snd_renderer, &sa_occlusion,	   &sa_transmission, &sa_reflections, &sa_reverb,
						   &sa_pathing,	  &sa_reflection_gain, &sa_reverb_gain,	 &sa_path_gain,	  &sa_sources,
						   &sa_rays,	  &sa_bounces,		   &sa_rate,		 &sa_spacing,	  &sa_cache};
	for (size_t i = 0; i < countof (variables); i++)
		Cvar_RegisterVariable (variables[i]);
	Cvar_RegisterVariable (&snd_steam_crossover);
	Cvar_SetCallback (&snd_steam_crossover, SA_CrossoverChanged);
	SA_CrossoverChanged (&snd_steam_crossover);
	Cmd_AddCommand ("snd_steam_info", SA_Info);
	Cmd_AddCommand ("snd_steam_rebuild", SA_Rebuild);
}

static qboolean SA_CreateVoice (sa_voice_t *voice)
{
	IPLBinauralEffectSettings binaural_settings = {0};
	IPLDirectEffectSettings	  direct_settings = {1};
	binaural_settings.hrtf = sa.hrtf;
	if (sa.renderer == SND_RENDERER_HRTF)
	{
		if (!voice->binaural && iplBinauralEffectCreate (sa.context, &sa.audio, &binaural_settings, &voice->binaural) != IPL_STATUS_SUCCESS)
			return false;
	}
	else if (!voice->panning)
	{
		IPLPanningEffectSettings settings = {sa.speaker_layout};
		if (iplPanningEffectCreate (sa.context, &sa.audio, &settings, &voice->panning) != IPL_STATUS_SUCCESS)
			return false;
	}
	if (!voice->direct && iplDirectEffectCreate (sa.context, &sa.audio, &direct_settings, &voice->direct) != IPL_STATUS_SUCCESS)
		return false;
	return true;
}

/* Consume exactly one SDK block, including loops and partial final samples.
 * Inaudible voices advance too, so an occluded loop resumes at the right time. */
static qboolean SA_ReadVoice (channel_t *channel, float *samples, qboolean pause_loops)
{
	sfxcache_t *cache = channel->sfx ? S_LoadSound (channel->sfx) : NULL;
	int			offset = 0;
	memset (samples, 0, STEAMAUDIO_FRAME_SIZE * sizeof (*samples));
	if (!cache || cache->length <= 0 || (pause_loops && cache->loopstart >= 0))
		return false;
	float gain = CLAMP (0.0f, channel->master_vol / 255.0f, 4.0f) * CLAMP (0.0f, sfxvolume.value, 1.0f);
	while (offset < STEAMAUDIO_FRAME_SIZE)
	{
		int count = q_min (STEAMAUDIO_FRAME_SIZE - offset, q_min (cache->length - channel->pos, channel->end - (paintedtime + offset)));
		if (count > 0)
		{
			for (int i = 0; i < count; i++)
				samples[offset + i] =
					gain * (cache->width == 1 ? ((signed char *)cache->data)[channel->pos + i] / 128.0f : ((short *)cache->data)[channel->pos + i] / 32768.0f);
			channel->pos += count;
			offset += count;
		}
		if (paintedtime + offset >= channel->end || channel->pos >= cache->length || count <= 0)
		{
			if (cache->loopstart >= 0 && cache->loopstart < cache->length)
			{
				channel->pos = cache->loopstart;
				channel->end = paintedtime + offset + cache->length - channel->pos;
			}
			else
			{
				channel->sfx = NULL;
				break;
			}
		}
	}
	return offset > 0;
}

static void SA_AddOutput (float gain)
{
	for (int side = 0; side < sa.channels; side++)
		for (int j = 0; j < STEAMAUDIO_FRAME_SIZE; j++)
			sa_mix[side][j] += sa_output[side][j] * gain;
}

static void SA_AddAmbisonics (float gain)
{
	for (int side = 0; side < SA_AMBI_CHANNELS; side++)
		for (int j = 0; j < STEAMAUDIO_FRAME_SIZE; j++)
			sa_ambi[side][j] += sa_wet[side][j] * gain;
}

static void SA_SplitDirect (sa_voice_t *voice, float *samples)
{
	if (!sa.bass_decode)
		return;
	for (int i = 0; i < STEAMAUDIO_FRAME_SIZE; i++)
	{
		const snd_crossover_sample_t sample = S_Crossover_Process (&sa.crossover, &voice->crossover, samples[i]);
		sa_mix[3][i] += (float)sample.low;
		samples[i] = (float)sample.high;
	}
}

void S_SteamAudio_Mix (int buffer[STEAMAUDIO_FRAME_SIZE][STEAMAUDIO_MAX_CHANNELS], qboolean pause_loops)
{
	uint64_t block_serial[MAX_CHANNELS] = {0};
	float	 zero[STEAMAUDIO_FRAME_SIZE] = {0};
	float	*output_data[STEAMAUDIO_MAX_CHANNELS];
	float	*ambi_data[] = {sa_ambi[0], sa_ambi[1], sa_ambi[2], sa_ambi[3]};
	float	*wet_data[] = {sa_wet[0], sa_wet[1], sa_wet[2], sa_wet[3]};
	for (int i = 0; i < sa.channels; i++)
		output_data[i] = sa_output[i];
	IPLAudioBuffer output = SA_Buffer (sa.channels, output_data);
	IPLAudioBuffer ambi = SA_Buffer (SA_AMBI_CHANNELS, ambi_data);
	IPLAudioBuffer wet = SA_Buffer (SA_AMBI_CHANNELS, wet_data);
	SDL_LockMutex (sa.mutex);
	memcpy (sa_mix_direct, sa.results, sizeof (sa_mix_direct));
	memcpy (sa_mix_indirect, sa.indirect_results, sizeof (sa_mix_indirect));
	SDL_UnlockMutex (sa.mutex);
	memset (sa_mix, 0, sizeof (sa_mix));
	memset (sa_ambi, 0, sizeof (sa_ambi));
	memset (sa_reverb_input, 0, sizeof (sa_reverb_input));
	if (sa.bass_decode && sa.crossover_frequency != S_SteamAudio_Crossover ())
	{
		sa.crossover_frequency = S_SteamAudio_Crossover ();
		S_Crossover_Init (&sa.crossover, sa.audio.samplingRate, sa.crossover_frequency);
		for (int i = 0; i < MAX_CHANNELS; i++)
			S_Crossover_Reset (&sa.voices[i].crossover);
		memset (sa.indirect_crossover, 0, sizeof (sa.indirect_crossover));
	}
	for (int i = 0; i < total_channels; i++)
	{
		channel_t  *channel = &snd_channels[i];
		sa_voice_t *voice = &sa.voices[i];
		qboolean	local = i < NUM_AMBIENTS || channel->entnum == cl.viewentity;
		if (voice->serial != channel->sa_serial)
		{
			if (voice->binaural)
				iplBinauralEffectReset (voice->binaural);
			if (voice->panning)
				iplPanningEffectReset (voice->panning);
			if (voice->direct)
				iplDirectEffectReset (voice->direct);
			voice->serial = channel->sa_serial;
			voice->tail = 0;
			S_Crossover_Reset (&voice->crossover);
		}
		qboolean has_samples = SA_ReadVoice (channel, sa_mono[i], pause_loops);
		if (!has_samples)
		{
			if (sa.bass_decode && S_Crossover_Active (&voice->crossover))
			{
				memset (sa_direct_buffer, 0, sizeof (sa_direct_buffer));
				SA_SplitDirect (voice, sa_direct_buffer);
				if (!local && voice->panning)
				{
					float				  *data = sa_direct_buffer;
					IPLAudioBuffer		   direct = SA_Buffer (1, &data);
					IPLPanningEffectParams panning = {voice->direction};
					iplPanningEffectApply (voice->panning, &panning, &direct, &output);
					SA_AddOutput (1);
				}
				else
					for (int j = 0; j < STEAMAUDIO_FRAME_SIZE; j++)
					{
						sa_mix[0][j] += sa_direct_buffer[j];
						sa_mix[1][j] += sa_direct_buffer[j];
					}
			}
			if (voice->binaural && voice->tail > 0)
			{
				iplBinauralEffectGetTail (voice->binaural, &output);
				voice->tail = iplBinauralEffectGetTailSize (voice->binaural);
				SA_AddOutput (1);
			}
			continue;
		}
		block_serial[i] = channel->sa_serial;
		IPLVector3	position = local ? sa_mix_snapshot.listener.origin : SA_ChannelPosition (channel);
		sa_source_t source = {0};
		source.attenuation = local ? 0 : channel->dist_mult;
		float	 attenuation = SA_Attenuation (SA_Distance (position, sa_mix_snapshot.listener.origin), &source);
		qboolean individual = false;
		for (int slot = 0; slot < sa.num_reflections; slot++)
			if (sa_mix_indirect[slot].valid && sa_mix_indirect[slot].channel == i && sa_mix_indirect[slot].serial == channel->sa_serial)
				individual = true;
		if (i >= NUM_AMBIENTS && !individual)
			for (int j = 0; j < STEAMAUDIO_FRAME_SIZE; j++)
				sa_reverb_input[j] += sa_mono[i][j] * attenuation;
		if (local)
		{
			memcpy (sa_direct_buffer, sa_mono[i], sizeof (sa_direct_buffer));
			SA_SplitDirect (voice, sa_direct_buffer);
			for (int j = 0; j < STEAMAUDIO_FRAME_SIZE; j++)
			{
				sa_mix[0][j] += sa_direct_buffer[j];
				sa_mix[1][j] += sa_direct_buffer[j];
			}
			continue;
		}
		if (attenuation <= 0 || channel->master_vol <= 0)
		{
			S_Crossover_Reset (&voice->crossover);
			/* Keep sample/loop time above, but spend no DSP on inaudible voices. */
			if (voice->binaural && voice->tail > 0)
			{
				iplBinauralEffectGetTail (voice->binaural, &output);
				voice->tail = iplBinauralEffectGetTailSize (voice->binaural);
				SA_AddOutput (1);
			}
			continue;
		}
		if (!SA_CreateVoice (voice))
		{
			/* Resource failure must not mute an event. Fall back for this voice. */
			for (int j = 0; j < STEAMAUDIO_FRAME_SIZE; j++)
				sa_direct_buffer[j] = sa_mono[i][j] * attenuation;
			SA_SplitDirect (voice, sa_direct_buffer);
			for (int j = 0; j < STEAMAUDIO_FRAME_SIZE; j++)
			{
				sa_mix[0][j] += sa_direct_buffer[j];
				sa_mix[1][j] += sa_direct_buffer[j];
			}
			continue;
		}
		IPLDirectEffectParams direct_params = {0};
		qboolean			  simulated = sa_mix_direct[i].serial == channel->sa_serial;
		if (simulated)
			direct_params = sa_mix_direct[i].direct;
		else
		{
			direct_params.occlusion = 1;
			for (int band = 0; band < IPL_NUM_BANDS; band++)
				direct_params.airAbsorption[band] = direct_params.transmission[band] = 1;
		}
		direct_params.distanceAttenuation = attenuation;
		direct_params.flags = IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION | IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION;
		if (sa_occlusion.value)
			direct_params.flags |= IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION;
		if (sa_occlusion.value && sa_transmission.value)
			direct_params.flags |= IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION;
		direct_params.transmissionType = IPL_TRANSMISSIONTYPE_FREQDEPENDENT;
		float		  *mono_data = sa_mono[i], *direct_data = sa_direct_buffer;
		IPLAudioBuffer mono = SA_Buffer (1, &mono_data), direct = SA_Buffer (1, &direct_data);
		iplDirectEffectApply (voice->direct, &direct_params, &mono, &direct);
		/* Split each acoustically processed mono source before directional panning. */
		SA_SplitDirect (voice, sa_direct_buffer);
		IPLVector3 direction = {
			position.x - sa_mix_snapshot.listener.origin.x, position.y - sa_mix_snapshot.listener.origin.y, position.z - sa_mix_snapshot.listener.origin.z};
		float					distance = sqrtf (direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
		IPLBinauralEffectParams binaural = {0};
		IPLCoordinateSpace3	   *listener = &sa_mix_snapshot.listener;
		if (distance > 1e-6f)
		{
			binaural.direction.x = (direction.x * listener->right.x + direction.y * listener->right.y + direction.z * listener->right.z) / distance;
			binaural.direction.y = (direction.x * listener->up.x + direction.y * listener->up.y + direction.z * listener->up.z) / distance;
			binaural.direction.z = -(direction.x * listener->ahead.x + direction.y * listener->ahead.y + direction.z * listener->ahead.z) / distance;
		}
		else
			binaural.direction.z = -1;
		voice->direction = binaural.direction;
		binaural.hrtf = sa.hrtf;
		binaural.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
		binaural.spatialBlend = 1;
		if (sa.renderer == SND_RENDERER_HRTF)
		{
			iplBinauralEffectApply (voice->binaural, &binaural, &direct, &output);
			voice->tail = iplBinauralEffectGetTailSize (voice->binaural);
		}
		else
		{
			IPLPanningEffectParams panning = {binaural.direction};
			iplPanningEffectApply (voice->panning, &panning, &direct, &output);
		}
		SA_AddOutput (1);
	}
	for (int i = 0; i <= sa.num_reflections; i++)
	{
		sa_indirect_result_t *result = &sa_mix_indirect[i];
		sa_indirect_voice_t	 *voice = &sa.indirect[i];
		qboolean			  reverb = i == sa.num_reflections;
		float				 *input = zero;
		if (voice->serial != result->serial)
		{
			iplReflectionEffectReset (voice->reflection);
			iplPathEffectReset (voice->path);
			voice->serial = result->serial;
			voice->tail = 0;
		}
		if (reverb)
			input = sa_reverb_input;
		else if (result->channel >= 0 && result->channel < MAX_CHANNELS && block_serial[result->channel] && block_serial[result->channel] == result->serial)
			input = sa_mono[result->channel];
		IPLAudioBuffer mono = SA_Buffer (1, &input);
		if (result->valid && result->reflections.ir && (reverb ? sa_reverb.value : sa_reflections.value))
		{
			float gain = CLAMP (0.0f, reverb ? sa_reverb_gain.value : sa_reflection_gain.value, 2.0f);
			iplReflectionEffectApply (voice->reflection, &result->reflections, &mono, &wet, NULL);
			SA_AddAmbisonics (gain);
		}
		else
			iplReflectionEffectReset (voice->reflection);
		if (result->path_valid && sa_pathing.value && input != zero)
		{
			result->path.shCoeffs = result->sh;
			result->path.order = SA_ORDER;
			result->path.normalizeEQ = IPL_FALSE;
			iplPathEffectApply (voice->path, &result->path, &mono, &wet);
			/* A clear direct path is already rendered above. Blend diffraction in
			 * as it becomes blocked, rather than duplicating the clear signal. */
			float occlusion = sa_mix_direct[result->channel].serial == result->serial ? sa_mix_direct[result->channel].direct.occlusion : 1;
			SA_AddAmbisonics (CLAMP (0.0f, sa_path_gain.value, 2.0f) * (1 - CLAMP (0.0f, occlusion, 1.0f)));
		}
		else
			iplPathEffectReset (voice->path);
	}
	IPLAmbisonicsDecodeEffectParams decode = {0};
	decode.order = SA_ORDER;
	decode.hrtf = sa.hrtf;
	decode.orientation = sa_mix_snapshot.listener;
	decode.binaural = sa.renderer == SND_RENDERER_HRTF ? IPL_TRUE : IPL_FALSE;
	if (sa.bass_decode)
	{
		/* Reflections/pathing remain Ambisonic until after the crossover. Decode
		 * their low band to mono with the SDK's own channel normalization. */
		for (int channel = 0; channel < SA_AMBI_CHANNELS; channel++)
			for (int i = 0; i < STEAMAUDIO_FRAME_SIZE; i++)
			{
				const snd_crossover_sample_t sample = S_Crossover_Process (&sa.crossover, &sa.indirect_crossover[channel], sa_ambi[channel][i]);
				sa_wet[channel][i] = (float)sample.low;
				sa_ambi[channel][i] = (float)sample.high;
			}
		float		  *data = sa_direct_buffer;
		IPLAudioBuffer bass = SA_Buffer (1, &data);
		iplAmbisonicsDecodeEffectApply (sa.bass_decode, &decode, &wet, &bass);
		for (int i = 0; i < STEAMAUDIO_FRAME_SIZE; i++)
			sa_mix[3][i] += sa_direct_buffer[i];
	}
	iplAmbisonicsDecodeEffectApply (sa.decode, &decode, &ambi, &output);
	SA_AddOutput (1);
	for (int i = 0; i < STEAMAUDIO_FRAME_SIZE; i++)
		for (int channel = 0; channel < sa.channels; channel++)
			buffer[i][channel] = (int)(CLAMP (-1.0f, sa_mix[channel][i], 1.0f) * 32767 * 256);
}
