/* Native SDK entry points, from Valve Steam Audio 4.8.1 phonon.h. */
/* clang-format off */
#define SA_FUNCTIONS(X) \
    X(IPLAudioEffectState, iplAmbisonicsDecodeEffectApply, (IPLAmbisonicsDecodeEffect effect, IPLAmbisonicsDecodeEffectParams* params, IPLAudioBuffer* in, IPLAudioBuffer* out)) \
    X(IPLerror, iplAmbisonicsDecodeEffectCreate, (IPLContext context, IPLAudioSettings* audioSettings, IPLAmbisonicsDecodeEffectSettings* effectSettings, IPLAmbisonicsDecodeEffect* effect)) \
    X(void, iplAmbisonicsDecodeEffectRelease, (IPLAmbisonicsDecodeEffect* effect)) \
    X(void, iplAmbisonicsDecodeEffectReset, (IPLAmbisonicsDecodeEffect effect)) \
    X(IPLAudioEffectState, iplBinauralEffectApply, (IPLBinauralEffect effect, IPLBinauralEffectParams* params, IPLAudioBuffer* in, IPLAudioBuffer* out)) \
    X(IPLerror, iplBinauralEffectCreate, (IPLContext context, IPLAudioSettings* audioSettings, IPLBinauralEffectSettings* effectSettings, IPLBinauralEffect* effect)) \
    X(IPLAudioEffectState, iplBinauralEffectGetTail, (IPLBinauralEffect effect, IPLAudioBuffer* out)) \
    X(IPLint32, iplBinauralEffectGetTailSize, (IPLBinauralEffect effect)) \
    X(void, iplBinauralEffectRelease, (IPLBinauralEffect* effect)) \
    X(void, iplBinauralEffectReset, (IPLBinauralEffect effect)) \
    X(IPLerror, iplContextCreate, (IPLContextSettings* settings, IPLContext* context)) \
    X(void, iplContextRelease, (IPLContext* context)) \
    X(IPLAudioEffectState, iplDirectEffectApply, (IPLDirectEffect effect, IPLDirectEffectParams* params, IPLAudioBuffer* in, IPLAudioBuffer* out)) \
    X(IPLerror, iplDirectEffectCreate, (IPLContext context, IPLAudioSettings* audioSettings, IPLDirectEffectSettings* effectSettings, IPLDirectEffect* effect)) \
    X(void, iplDirectEffectRelease, (IPLDirectEffect* effect)) \
    X(void, iplDirectEffectReset, (IPLDirectEffect effect)) \
    X(IPLerror, iplHRTFCreate, (IPLContext context, IPLAudioSettings* audioSettings, IPLHRTFSettings* hrtfSettings, IPLHRTF* hrtf)) \
    X(void, iplHRTFRelease, (IPLHRTF* hrtf)) \
    X(IPLAudioEffectState, iplPanningEffectApply, (IPLPanningEffect effect, IPLPanningEffectParams* params, IPLAudioBuffer* in, IPLAudioBuffer* out)) \
    X(IPLerror, iplPanningEffectCreate, (IPLContext context, IPLAudioSettings* audioSettings, IPLPanningEffectSettings* effectSettings, IPLPanningEffect* effect)) \
    X(void, iplPanningEffectRelease, (IPLPanningEffect* effect)) \
    X(void, iplPanningEffectReset, (IPLPanningEffect effect)) \
    X(void, iplInstancedMeshAdd, (IPLInstancedMesh instancedMesh, IPLScene scene)) \
    X(IPLerror, iplInstancedMeshCreate, (IPLScene scene, IPLInstancedMeshSettings* settings, IPLInstancedMesh* instancedMesh)) \
    X(void, iplInstancedMeshRelease, (IPLInstancedMesh* instancedMesh)) \
    X(void, iplInstancedMeshRemove, (IPLInstancedMesh instancedMesh, IPLScene scene)) \
    X(void, iplInstancedMeshUpdateTransform, (IPLInstancedMesh instancedMesh, IPLScene scene, IPLMatrix4x4 transform)) \
    X(void, iplPathBakerBake, (IPLContext context, IPLPathBakeParams* params, IPLProgressCallback progressCallback, void* userData)) \
    X(void, iplPathBakerCancelBake, (IPLContext context)) \
    X(IPLAudioEffectState, iplPathEffectApply, (IPLPathEffect effect, IPLPathEffectParams* params, IPLAudioBuffer* in, IPLAudioBuffer* out)) \
    X(IPLerror, iplPathEffectCreate, (IPLContext context, IPLAudioSettings* audioSettings, IPLPathEffectSettings* effectSettings, IPLPathEffect* effect)) \
    X(void, iplPathEffectRelease, (IPLPathEffect* effect)) \
    X(void, iplPathEffectReset, (IPLPathEffect effect)) \
    X(void, iplProbeBatchAddProbe, (IPLProbeBatch probeBatch, IPLSphere probe)) \
    X(void, iplProbeBatchCommit, (IPLProbeBatch probeBatch)) \
    X(IPLerror, iplProbeBatchCreate, (IPLContext context, IPLProbeBatch* probeBatch)) \
    X(IPLint32, iplProbeBatchGetNumProbes, (IPLProbeBatch probeBatch)) \
    X(IPLerror, iplProbeBatchLoad, (IPLContext context, IPLSerializedObject serializedObject, IPLProbeBatch* probeBatch)) \
    X(void, iplProbeBatchRelease, (IPLProbeBatch* probeBatch)) \
    X(void, iplProbeBatchSave, (IPLProbeBatch probeBatch, IPLSerializedObject serializedObject)) \
    X(IPLAudioEffectState, iplReflectionEffectApply, (IPLReflectionEffect effect, IPLReflectionEffectParams* params, IPLAudioBuffer* in, IPLAudioBuffer* out, IPLReflectionMixer mixer)) \
    X(IPLerror, iplReflectionEffectCreate, (IPLContext context, IPLAudioSettings* audioSettings, IPLReflectionEffectSettings* effectSettings, IPLReflectionEffect* effect)) \
    X(void, iplReflectionEffectRelease, (IPLReflectionEffect* effect)) \
    X(void, iplReflectionEffectReset, (IPLReflectionEffect effect)) \
    X(void, iplSceneCommit, (IPLScene scene)) \
    X(IPLerror, iplSceneCreate, (IPLContext context, IPLSceneSettings* settings, IPLScene* scene)) \
    X(IPLerror, iplSceneLoad, (IPLContext context, IPLSceneSettings* settings, IPLSerializedObject serializedObject, IPLProgressCallback progressCallback, void* progressCallbackUserData, IPLScene* scene)) \
    X(void, iplSceneRelease, (IPLScene* scene)) \
    X(void, iplSceneSave, (IPLScene scene, IPLSerializedObject serializedObject)) \
    X(IPLerror, iplSerializedObjectCreate, (IPLContext context, IPLSerializedObjectSettings* settings, IPLSerializedObject* serializedObject)) \
    X(IPLbyte*, iplSerializedObjectGetData, (IPLSerializedObject serializedObject)) \
    X(IPLsize, iplSerializedObjectGetSize, (IPLSerializedObject serializedObject)) \
    X(void, iplSerializedObjectRelease, (IPLSerializedObject* serializedObject)) \
    X(void, iplSimulatorAddProbeBatch, (IPLSimulator simulator, IPLProbeBatch probeBatch)) \
    X(void, iplSimulatorCommit, (IPLSimulator simulator)) \
    X(IPLerror, iplSimulatorCreate, (IPLContext context, IPLSimulationSettings* settings, IPLSimulator* simulator)) \
    X(void, iplSimulatorRelease, (IPLSimulator* simulator)) \
    X(void, iplSimulatorRunDirect, (IPLSimulator simulator)) \
    X(void, iplSimulatorRunPathing, (IPLSimulator simulator)) \
    X(void, iplSimulatorRunReflections, (IPLSimulator simulator)) \
    X(void, iplSimulatorSetScene, (IPLSimulator simulator, IPLScene scene)) \
    X(void, iplSimulatorSetSharedInputs, (IPLSimulator simulator, IPLSimulationFlags flags, IPLSimulationSharedInputs* sharedInputs)) \
    X(void, iplSourceAdd, (IPLSource source, IPLSimulator simulator)) \
    X(IPLerror, iplSourceCreate, (IPLSimulator simulator, IPLSourceSettings* settings, IPLSource* source)) \
    X(void, iplSourceGetOutputs, (IPLSource source, IPLSimulationFlags flags, IPLSimulationOutputs* outputs)) \
    X(void, iplSourceRelease, (IPLSource* source)) \
    X(void, iplSourceSetInputs, (IPLSource source, IPLSimulationFlags flags, IPLSimulationInputs* inputs)) \
    X(void, iplStaticMeshAdd, (IPLStaticMesh staticMesh, IPLScene scene)) \
    X(IPLerror, iplStaticMeshCreate, (IPLScene scene, IPLStaticMeshSettings* settings, IPLStaticMesh* staticMesh)) \
    X(void, iplStaticMeshRelease, (IPLStaticMesh* staticMesh))
#define SA_DECLARE(ret, name, args) static ret (IPLCALL *sa_##name) args;
SA_FUNCTIONS(SA_DECLARE)
#undef SA_DECLARE
static qboolean SA_LoadFunctions (void *library)
{
#define SA_BIND(ret, name, args) \
    *(void **)&sa_##name = (void *)SDL_LoadFunction (library, #name); \
    if (!sa_##name) { Con_Warning ("Steam Audio: missing native entry point %s\n", #name); return false; }
    SA_FUNCTIONS(SA_BIND)
#undef SA_BIND
    return true;
}
#undef SA_FUNCTIONS
#define iplAmbisonicsDecodeEffectApply sa_iplAmbisonicsDecodeEffectApply
#define iplAmbisonicsDecodeEffectCreate sa_iplAmbisonicsDecodeEffectCreate
#define iplAmbisonicsDecodeEffectRelease sa_iplAmbisonicsDecodeEffectRelease
#define iplAmbisonicsDecodeEffectReset sa_iplAmbisonicsDecodeEffectReset
#define iplBinauralEffectApply sa_iplBinauralEffectApply
#define iplBinauralEffectCreate sa_iplBinauralEffectCreate
#define iplBinauralEffectGetTail sa_iplBinauralEffectGetTail
#define iplBinauralEffectGetTailSize sa_iplBinauralEffectGetTailSize
#define iplBinauralEffectRelease sa_iplBinauralEffectRelease
#define iplBinauralEffectReset sa_iplBinauralEffectReset
#define iplContextCreate sa_iplContextCreate
#define iplContextRelease sa_iplContextRelease
#define iplDirectEffectApply sa_iplDirectEffectApply
#define iplDirectEffectCreate sa_iplDirectEffectCreate
#define iplDirectEffectRelease sa_iplDirectEffectRelease
#define iplDirectEffectReset sa_iplDirectEffectReset
#define iplHRTFCreate sa_iplHRTFCreate
#define iplHRTFRelease sa_iplHRTFRelease
#define iplPanningEffectApply sa_iplPanningEffectApply
#define iplPanningEffectCreate sa_iplPanningEffectCreate
#define iplPanningEffectRelease sa_iplPanningEffectRelease
#define iplPanningEffectReset sa_iplPanningEffectReset
#define iplInstancedMeshAdd sa_iplInstancedMeshAdd
#define iplInstancedMeshCreate sa_iplInstancedMeshCreate
#define iplInstancedMeshRelease sa_iplInstancedMeshRelease
#define iplInstancedMeshRemove sa_iplInstancedMeshRemove
#define iplInstancedMeshUpdateTransform sa_iplInstancedMeshUpdateTransform
#define iplPathBakerBake sa_iplPathBakerBake
#define iplPathBakerCancelBake sa_iplPathBakerCancelBake
#define iplPathEffectApply sa_iplPathEffectApply
#define iplPathEffectCreate sa_iplPathEffectCreate
#define iplPathEffectRelease sa_iplPathEffectRelease
#define iplPathEffectReset sa_iplPathEffectReset
#define iplProbeBatchAddProbe sa_iplProbeBatchAddProbe
#define iplProbeBatchCommit sa_iplProbeBatchCommit
#define iplProbeBatchCreate sa_iplProbeBatchCreate
#define iplProbeBatchGetNumProbes sa_iplProbeBatchGetNumProbes
#define iplProbeBatchLoad sa_iplProbeBatchLoad
#define iplProbeBatchRelease sa_iplProbeBatchRelease
#define iplProbeBatchSave sa_iplProbeBatchSave
#define iplReflectionEffectApply sa_iplReflectionEffectApply
#define iplReflectionEffectCreate sa_iplReflectionEffectCreate
#define iplReflectionEffectRelease sa_iplReflectionEffectRelease
#define iplReflectionEffectReset sa_iplReflectionEffectReset
#define iplSceneCommit sa_iplSceneCommit
#define iplSceneCreate sa_iplSceneCreate
#define iplSceneLoad sa_iplSceneLoad
#define iplSceneRelease sa_iplSceneRelease
#define iplSceneSave sa_iplSceneSave
#define iplSerializedObjectCreate sa_iplSerializedObjectCreate
#define iplSerializedObjectGetData sa_iplSerializedObjectGetData
#define iplSerializedObjectGetSize sa_iplSerializedObjectGetSize
#define iplSerializedObjectRelease sa_iplSerializedObjectRelease
#define iplSimulatorAddProbeBatch sa_iplSimulatorAddProbeBatch
#define iplSimulatorCommit sa_iplSimulatorCommit
#define iplSimulatorCreate sa_iplSimulatorCreate
#define iplSimulatorRelease sa_iplSimulatorRelease
#define iplSimulatorRunDirect sa_iplSimulatorRunDirect
#define iplSimulatorRunPathing sa_iplSimulatorRunPathing
#define iplSimulatorRunReflections sa_iplSimulatorRunReflections
#define iplSimulatorSetScene sa_iplSimulatorSetScene
#define iplSimulatorSetSharedInputs sa_iplSimulatorSetSharedInputs
#define iplSourceAdd sa_iplSourceAdd
#define iplSourceCreate sa_iplSourceCreate
#define iplSourceGetOutputs sa_iplSourceGetOutputs
#define iplSourceRelease sa_iplSourceRelease
#define iplSourceSetInputs sa_iplSourceSetInputs
#define iplStaticMeshAdd sa_iplStaticMeshAdd
#define iplStaticMeshCreate sa_iplStaticMeshCreate
#define iplStaticMeshRelease sa_iplStaticMeshRelease
/* clang-format on */
