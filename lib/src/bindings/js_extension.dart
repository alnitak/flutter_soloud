// ignore_for_file: public_member_api_docs

import 'dart:js_interop';
import 'package:web/web.dart' as web;

@JS('globalThis')
external JSObject get globalThis;

@JS('Module_soloud._malloc')
external int wasmMalloc(int bytesCount);

@JS('Module_soloud._free')
external void wasmFree(int ptrAddress);

@JS('Module_soloud.getValue')
external int wasmGetI32Value(int ptrAddress, String type);

@JS('Module_soloud.getValue')
external double wasmGetF32Value(int ptrAddress, String type);

@JS('Module_soloud.HEAPU8.buffer')
external JSArrayBuffer get wasmHeapU8Buffer;

@JS('Module_soloud.HEAPF32')
external JSFloat32Array get wasmHeapF32Buffer;

@JS('Module_soloud.UTF8ToString')
external String wasmUtf8ToString(int ptrAddress);

@JS('Module_soloud.setValue')
external void wasmSetValue(int ptrAddress, int value, String type);

@JS('Module_soloud.cwrap')
external JSFunction wasmCwrap(
  JSString fName,
  JSString returnType,
  JSArray<JSString> argTypes,
);

@JS('Module_soloud.ccall')
external JSFunction wasmCccall(
  JSString fName,
  JSString returnType,
  JSArray<JSString> argTypes,
  JSArray<JSAny> args,
);

@JS('Module_soloud._createWorkerInWasm')
external int wasmCreateWorkerInWasm();

@JS('Module_soloud._sendToWorker')
external void wasmSendToWorker(int message, int value);

@JS('Module_soloud.wasmWorker')
external web.Worker wasmWorker;

@JS('Module_soloud._setBufferStream')
external int wasmSetBufferStream(
  int hashPtr,
  int bufferingType,
  int maxBufferSize,
  double bufferingTimeNeeds,
  int sampleRate,
  int channels,
  int format,
  int onBufferingPtr,
);

@JS('Module_soloud._resetBufferStream')
external int wasmResetBufferStream(int hash);

@JS('Module_soloud._addAudioDataStream')
external int wasmAddAudioDataStream(int hash, int audioChunkPtr, int dataLen);

@JS('Module_soloud._setDataIsEnded')
external int wasmSetDataIsEnded(int hash);

@JS('Module_soloud._getBufferSize')
external int wasmGetBufferSize(int hash, int sizeInBytesPtr);

@JS('Module_soloud._areOpusOggLibsAvailable')
external int wasmAreOpusOggLibsAvailable();

@JS('Module_soloud._initEngine')
external int wasmInitEngine(
  int deviceId,
  int sampleRate,
  int bufferSize,
  int channels,
);

@JS('Module_soloud._changeDevice')
external int wasmChangeDevice(int deviceId);

@JS('Module_soloud._listPlaybackDevices')
external void wasmListPlaybackDevices(
  int namesPtr,
  int deviceIdPtr,
  int isDefaultPtr,
  int nDevicePtr,
);

@JS('Module_soloud._freeListPlaybackDevices')
external void wasmFreeListPlaybackDevices(
  int namesPtr,
  int deviceIdPtr,
  int isDefaultPtr,
  int nDevicePtr,
);

@JS('Module_soloud._dispose')
external void wasmDeinit();

@JS('Module_soloud._isInited')
external int wasmIsInited();

@JS('Module_soloud._loadFile')
external int wasmLoadFile(
  int completeFileNamePtr,
  int loadIntoMem,
  int hashPtr,
);

@JS('Module_soloud._loadMem')
external int wasmLoadMem(
  int uniqueNamePtr,
  int memPtr,
  int length,
  int loadIntoMem,
  int hashPtr,
);

@JS('Module_soloud._loadWaveform')
external int wasmLoadWaveform(
  int waveform,
  // ignore: avoid_positional_boolean_parameters
  bool superWave,
  double scale,
  double detune,
  int hashPtr,
);

@JS('Module_soloud._setWaveformScale')
external void wasmSetWaveformScale(int soundHash, double newScale);

@JS('Module_soloud._setWaveformDetune')
external void wasmSetWaveformDetune(int soundHash, double newDetune);

@JS('Module_soloud._setWaveformFreq')
external void wasmSetWaveformFreq(int soundHash, double newFreq);

@JS('Module_soloud._setSuperWave')
external void wasmSetSuperWave(int soundHash, int superwave);

@JS('Module_soloud._setWaveform')
external void wasmSetWaveform(int soundHash, int newWaveform);

@JS('Module_soloud._speechText')
external int wasmSpeechText(int textToSpeechPtr, int handlePtr);

@JS('Module_soloud._pauseSwitch')
external void wasmPauseSwitch(int handle);

@JS('Module_soloud._setPause')
external void wasmSetPause(int handle, int pause);

@JS('Module_soloud._getPause')
external int wasmGetPause(int handle);

@JS('Module_soloud._setRelativePlaySpeed')
external void wasmSetRelativePlaySpeed(int handle, double speed);

@JS('Module_soloud._getRelativePlaySpeed')
external double wasmGetRelativePlaySpeed(int handle);

@JS('Module_soloud._play')
external int wasmPlay(
  int soundHash,
  double volume,
  double pan,
  // ignore: avoid_positional_boolean_parameters
  bool paused,
  bool looping,
  double loopingStartAt,
  int handlePtr,
);

@JS('Module_soloud._stop')
external void wasmStop(int handle);

@JS('Module_soloud._disposeSound')
external void wasmDisposeSound(int soundHash);

@JS('Module_soloud._disposeAllSound')
external void wasmDisposeAllSound();

@JS('Module_soloud._getLooping')
external int wasmGetLooping(int handle);

@JS('Module_soloud._setLooping')
external void wasmSetLooping(int handle, int enable);

@JS('Module_soloud._getLoopPoint')
external double wasmGetLoopPoint(int handle);

@JS('Module_soloud._setLoopPoint')
external void wasmSetLoopPoint(int handle, double time);

@JS('Module_soloud._setVisualizationEnabled')
external void wasmSetVisualizationEnabled(int enabled);

@JS('Module_soloud._getVisualizationEnabled')
external int wasmGetVisualizationEnabled();

@JS('Module_soloud._getWave')
external void wasmGetWave(int samplesPtr, int isTheSameAsBeforePtr);

@JS('Module_soloud._getFft')
external void wasmGetFft(int samplesPtr, int isTheSameAsBeforePtr);

@JS('Module_soloud._setFftSmoothing')
external void wasmSetFftSmoothing(double smooth);

@JS('Module_soloud._getAudioTexture')
external void wasmGetAudioTexture(int samplesPtr, int isTheSameAsBeforePtr);

@JS('Module_soloud._getAudioTexture2D')
external void wasmGetAudioTexture2D(int samplesPtr, int isTheSameAsBeforePtr);

@JS('Module_soloud._getTextureValue')
external double wasmGetTextureValue(int row, int column);

@JS('Module_soloud._getLength')
external double wasmGetLength(int soundHash);

@JS('Module_soloud._seek')
external int wasmSeek(int handle, double time);

@JS('Module_soloud._getPosition')
external double wasmGetPosition(int handle);

@JS('Module_soloud._getGlobalVolume')
external double wasmGetGlobalVolume();

@JS('Module_soloud._setGlobalVolume')
external int wasmSetGlobalVolume(double volume);

@JS('Module_soloud._getVolume')
external double wasmGetVolume(int handle);

@JS('Module_soloud._setVolume')
external int wasmSetVolume(int handle, double volume);

@JS('Module_soloud._getPan')
external double wasmGetPan(int handle);

@JS('Module_soloud._setPan')
external void wasmSetPan(int handle, double pan);

@JS('Module_soloud._setPanAbsolute')
external void wasmSetPanAbsolute(int handle, double panLeft, double panRight);

@JS('Module_soloud._getIsValidVoiceHandle')
external int wasmGetIsValidVoiceHandle(int handle);

@JS('Module_soloud._getActiveVoiceCount')
external int wasmGetActiveVoiceCount();

@JS('Module_soloud._countAudioSource')
external int wasmCountAudioSource(int soundHash);

@JS('Module_soloud._getVoiceCount')
external int wasmGetVoiceCount();

@JS('Module_soloud._getProtectVoice')
external int wasmGetProtectVoice(int handle);

@JS('Module._setInaudibleBehavior')
external void wasmSetInaudibleBehavior(int handle, int mustTick, int kill);

@JS('Module_soloud._setProtectVoice')
external void wasmSetProtectVoice(int handle, int protect);

@JS('Module_soloud._getMaxActiveVoiceCount')
external int wasmGetMaxActiveVoiceCount();

@JS('Module_soloud._setMaxActiveVoiceCount')
external void wasmSetMaxActiveVoiceCount(int maxVoiceCount);

/////////////////////////////////////////
/// voice groups
/////////////////////////////////////////

@JS('Module_soloud._createVoiceGroup')
external int wasmCreateVoiceGroup();

@JS('Module_soloud._destroyVoiceGroup')
external void wasmDestroyVoiceGroup(int handle);

@JS('Module_soloud._addVoiceToGroup')
external void wasmAddVoiceToGroup(int voiceGroupHandle, int voiceHandle);

@JS('Module_soloud._isVoiceGroup')
external int wasmIsVoiceGroup(int handle);

@JS('Module_soloud._isVoiceGroupEmpty')
external int wasmIsVoiceGroupEmpty(int handle);

// ///////////////////////////////////////
//  faders
// ///////////////////////////////////////

@JS('Module_soloud._fadeGlobalVolume')
external int wasmFadeGlobalVolume(double to, double duration);

@JS('Module_soloud._fadeVolume')
external int wasmFadeVolume(int handle, double to, double duration);

@JS('Module_soloud._fadePan')
external int wasmFadePan(int handle, double to, double duration);

@JS('Module_soloud._fadeRelativePlaySpeed')
external int wasmFadeRelativePlaySpeed(int handle, double to, double duration);

@JS('Module_soloud._schedulePause')
external int wasmSchedulePause(int handle, double duration);

@JS('Module_soloud._scheduleStop')
external int wasmScheduleStop(int handle, double duration);

@JS('Module_soloud._oscillateVolume')
external int wasmOscillateVolume(
  int handle,
  double from,
  double to,
  double time,
);

@JS('Module_soloud._oscillatePan')
external int wasmOscillatePan(int handle, double from, double to, double time);

@JS('Module_soloud._oscillateRelativePlaySpeed')
external int wasmOscillateRelativePlaySpeed(
  int handle,
  double from,
  double to,
  double time,
);

@JS('Module_soloud._oscillateGlobalVolume')
external int wasmOscillateGlobalVolume(double from, double to, double time);

@JS('Module_soloud._fadeFilterParameter')
external int wasmFadeFilterParameter(
  int handle,
  int filterType,
  int attributeId,
  double to,
  double time,
);

@JS('Module_soloud._oscillateFilterParameter')
external int wasmOscillateFilterParameter(
  int handle,
  int filterType,
  int attributeId,
  double from,
  double to,
  double time,
);

// ///////////////////////////////////////
//  Filters
// ///////////////////////////////////////

@JS('Module_soloud._isFilterActive')
external int wasmIsFilterActive(int soundHash, int filterType, int idPtr);

@JS('Module_soloud._getFilterParamNames')
external int wasmGetFilterParamNames(
  int filterType,
  int paramsCountPtr,
  int namesPtr,
);

@JS('Module_soloud._addFilter')
external int wasmAddFilter(int soundHash, int filterType);

@JS('Module_soloud._removeFilter')
external int wasmRemoveFilter(int soundHash, int filterType);

@JS('Module_soloud._setFilterParams')
external int wasmSetFilterParams(
  int handle,
  int filterType,
  int attributeId,
  double value,
);

@JS('Module_soloud._getFilterParams')
external int wasmGetFilterParams(
  int handle,
  int filterType,
  int attributeId,
  int paramValuePtr,
);

@JS('Module_soloud._play3d')
external int wasmPlay3d(
  int soundHash,
  double posX,
  double posY,
  double posZ,
  double velX,
  double velY,
  double velZ,
  double volume,
  int paused,
  int looping,
  double loopingStartAt,
  int handlePtr,
);

@JS('Module_soloud._set3dSoundSpeed')
external void wasmSet3dSoundSpeed(double speed);

@JS('Module_soloud._get3dSoundSpeed')
external double wasmGet3dSoundSpeed();

@JS('Module_soloud._set3dListenerParameters')
external void wasmSet3dListenerParameters(
  double posX,
  double posY,
  double posZ,
  double atX,
  double atY,
  double atZ,
  double upX,
  double upY,
  double upZ,
  double velocityX,
  double velocityY,
  double velocityZ,
);

@JS('Module_soloud._set3dListenerPosition')
external void wasmSet3dListenerPosition(double posX, double posY, double posZ);

@JS('Module_soloud._set3dListenerAt')
external void wasmSet3dListenerAt(double atX, double atY, double atZ);

@JS('Module_soloud._set3dListenerUp')
external void wasmSet3dListenerUp(double upX, double upY, double upZ);

@JS('Module_soloud._set3dListenerVelocity')
external void wasmSet3dListenerVelocity(
  double velocityX,
  double velocityY,
  double velocityZ,
);

@JS('Module_soloud._set3dSourceParameters')
external void wasmSet3dSourceParameters(
  int handle,
  double posX,
  double posY,
  double posZ,
  double velocityX,
  double velocityY,
  double velocityZ,
);

@JS('Module_soloud._set3dSourcePosition')
external void wasmSet3dSourcePosition(
  int handle,
  double posX,
  double posY,
  double posZ,
);

@JS('Module_soloud._set3dSourceVelocity')
external void wasmSet3dSourceVelocity(
  int handle,
  double velocityX,
  double velocityY,
  double velocityZ,
);

@JS('Module_soloud._set3dSourceMinMaxDistance')
external void wasmSet3dSourceMinMaxDistance(
  int handle,
  double minDistance,
  double maxDistance,
);

@JS('Module_soloud._set3dSourceAttenuation')
external void wasmSet3dSourceAttenuation(
  int handle,
  int attenuationModel,
  double attenuationRolloffFactor,
);

@JS('Module_soloud._set3dSourceDopplerFactor')
external void wasmSet3dSourceDopplerFactor(int handle, double dopplerFactor);

@JS('Module_soloud._readSamplesFromMem')
external int wasmReadSamplesFromMem(
  int bufferPtr,
  int bufferLength,
  double startTime,
  double endTime,
  int numSamplesNeeded,
  // ignore: avoid_positional_boolean_parameters
  bool average,
  int pSamplesPtr,
);
