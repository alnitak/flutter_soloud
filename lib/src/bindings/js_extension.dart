// ignore_for_file: public_member_api_docs

import 'dart:js_interop';
import 'package:web/web.dart' as web;

@JS('Module._malloc')
external int wasmMalloc(int bytesCount);

@JS('Module._free')
external void wasmFree(int ptrAddress);

@JS('Module.getValue')
external int wasmGetI32Value(int ptrAddress, String type);

@JS('Module.getValue')
external double wasmGetF32Value(int ptrAddress, String type);

@JS('Module.UTF8ToString')
external String wasmUtf8ToString(int ptrAddress);

@JS('Module.setValue')
external void wasmSetValue(int ptrAddress, int value, String type);

@JS('Module.cwrap')
external JSFunction wasmCwrap(
  JSString fName,
  JSString returnType,
  JSArray<JSString> argTypes,
);

@JS('Module.ccall')
external JSFunction wasmCccall(
  JSString fName,
  JSString returnType,
  JSArray<JSString> argTypes,
  JSArray<JSAny> args,
);

@JS('Module._createWorkerInWasm')
external void wasmCreateWorkerInWasm();

@JS('Module._sendToWorker')
external void wasmSendToWorker(int message, int value);

@JS('Module.wasmWorker')
external web.Worker wasmWorker;

@JS('Module._initEngine')
external int wasmInitEngine(int sampleRate, int bufferSize, int channels);

@JS('Module._dispose')
external void wasmDeinit();

@JS('Module._isInited')
external int wasmIsInited();

@JS('Module._loadFile')
external int wasmLoadFile(
  int completeFileNamePtr,
  int loadIntoMem,
  int hashPtr,
);

@JS('Module._loadMem')
external int wasmLoadMem(
  int uniqueNamePtr,
  int memPtr,
  int length,
  int loadIntoMem,
  int hashPtr,
);

@JS('Module._loadWaveform')
external int wasmLoadWaveform(
  int waveform,
  // ignore: avoid_positional_boolean_parameters
  bool superWave,
  double scale,
  double detune,
  int hashPtr,
);

@JS('Module._setWaveformScale')
external void wasmSetWaveformScale(int soundHash, double newScale);

@JS('Module._setWaveformDetune')
external void wasmSetWaveformDetune(int soundHash, double newDetune);

@JS('Module._setWaveformFreq')
external void wasmSetWaveformFreq(int soundHash, double newFreq);

@JS('Module._setSuperWave')
external void wasmSetSuperWave(int soundHash, int superwave);

@JS('Module._setWaveform')
external void wasmSetWaveform(int soundHash, int newWaveform);

@JS('Module._speechText')
external int wasmSpeechText(int textToSpeechPtr, int handlePtr);

@JS('Module._pauseSwitch')
external void wasmPauseSwitch(int handle);

@JS('Module._setPause')
external void wasmSetPause(int handle, int pause);

@JS('Module._getPause')
external int wasmGetPause(int handle);

@JS('Module._setRelativePlaySpeed')
external void wasmSetRelativePlaySpeed(int handle, double speed);

@JS('Module._getRelativePlaySpeed')
external double wasmGetRelativePlaySpeed(int handle);

@JS('Module._play')
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

@JS('Module._stop')
external void wasmStop(int handle);

@JS('Module._disposeSound')
external void wasmDisposeSound(int soundHash);

@JS('Module._disposeAllSound')
external void wasmDisposeAllSound();

@JS('Module._getLooping')
external int wasmGetLooping(int handle);

@JS('Module._setLooping')
external void wasmSetLooping(int handle, int enable);

@JS('Module._getLoopPoint')
external double wasmGetLoopPoint(int handle);

@JS('Module._setLoopPoint')
external void wasmSetLoopPoint(int handle, double time);

@JS('Module._setVisualizationEnabled')
external void wasmSetVisualizationEnabled(int enabled);

@JS('Module._getVisualizationEnabled')
external int wasmGetVisualizationEnabled();

@JS('Module._getWave')
external void wasmGetWave(int samplesPtr);

@JS('Module._getFft')
external void wasmGetFft(int samplesPtr);

@JS('Module._setFftSmoothing')
external void wasmSetFftSmoothing(double smooth);

@JS('Module._getAudioTexture')
external void wasmGetAudioTexture(int samplesPtr);

@JS('Module._getAudioTexture2D')
external int wasmGetAudioTexture2D(int samplesPtr);

@JS('Module._getTextureValue')
external double wasmGetTextureValue(int row, int column);

@JS('Module._getLength')
external double wasmGetLength(int soundHash);

@JS('Module._seek')
external int wasmSeek(int handle, double time);

@JS('Module._getPosition')
external double wasmGetPosition(int handle);

@JS('Module._getGlobalVolume')
external double wasmGetGlobalVolume();

@JS('Module._setGlobalVolume')
external int wasmSetGlobalVolume(double volume);

@JS('Module._getVolume')
external double wasmGetVolume(int handle);

@JS('Module._setVolume')
external int wasmSetVolume(int handle, double volume);

@JS('Module._getPan')
external double wasmGetPan(int handle);

@JS('Module._setPan')
external void wasmSetPan(int handle, double pan);

@JS('Module._setPanAbsolute')
external void wasmSetPanAbsolute(int handle, double panLeft, double panRight);

@JS('Module._getIsValidVoiceHandle')
external int wasmGetIsValidVoiceHandle(int handle);

@JS('Module._getActiveVoiceCount')
external int wasmGetActiveVoiceCount();

@JS('Module._countAudioSource')
external int wasmCountAudioSource(int soundHash);

@JS('Module._getVoiceCount')
external int wasmGetVoiceCount();

@JS('Module._getProtectVoice')
external int wasmGetProtectVoice(int handle);

@JS('Module._setProtectVoice')
external void wasmSetProtectVoice(int handle, int protect);

@JS('Module._getMaxActiveVoiceCount')
external int wasmGetMaxActiveVoiceCount();

@JS('Module._setMaxActiveVoiceCount')
external void wasmSetMaxActiveVoiceCount(int maxVoiceCount);

/////////////////////////////////////////
/// voice groups
/////////////////////////////////////////

@JS('Module._createVoiceGroup')
external int wasmCreateVoiceGroup();

@JS('Module._destroyVoiceGroup')
external void wasmDestroyVoiceGroup(int handle);

@JS('Module._addVoiceToGroup')
external void wasmAddVoiceToGroup(int voiceGroupHandle, int voiceHandle);

@JS('Module._isVoiceGroup')
external int wasmIsVoiceGroup(int handle);

@JS('Module._isVoiceGroupEmpty')
external int wasmIsVoiceGroupEmpty(int handle);

// ///////////////////////////////////////
//  faders
// ///////////////////////////////////////

@JS('Module._fadeGlobalVolume')
external int wasmFadeGlobalVolume(double to, double duration);

@JS('Module._fadeVolume')
external int wasmFadeVolume(int handle, double to, double duration);

@JS('Module._fadePan')
external int wasmFadePan(int handle, double to, double duration);

@JS('Module._fadeRelativePlaySpeed')
external int wasmFadeRelativePlaySpeed(int handle, double to, double duration);

@JS('Module._schedulePause')
external int wasmSchedulePause(int handle, double duration);

@JS('Module._scheduleStop')
external int wasmScheduleStop(int handle, double duration);

@JS('Module._oscillateVolume')
external int wasmOscillateVolume(
  int handle,
  double from,
  double to,
  double time,
);

@JS('Module._oscillatePan')
external int wasmOscillatePan(int handle, double from, double to, double time);

@JS('Module._oscillateRelativePlaySpeed')
external int wasmOscillateRelativePlaySpeed(
  int handle,
  double from,
  double to,
  double time,
);

@JS('Module._oscillateGlobalVolume')
external int wasmOscillateGlobalVolume(double from, double to, double time);

@JS('Module._fadeFilterParameter')
external int wasmFadeFilterParameter(
  int handle,
  int filterType,
  int attributeId,
  double to,
  double time,
);

@JS('Module._oscillateFilterParameter')
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

@JS('Module._isFilterActive')
external int wasmIsFilterActive(int soundHash, int filterType, int idPtr);

@JS('Module._getFilterParamNames')
external int wasmGetFilterParamNames(
  int filterType,
  int paramsCountPtr,
  int namesPtr,
);

@JS('Module._addFilter')
external int wasmAddFilter(int soundHash, int filterType);

@JS('Module._removeFilter')
external int wasmRemoveFilter(int soundHash, int filterType);

@JS('Module._setFilterParams')
external int wasmSetFilterParams(
  int handle,
  int filterType,
  int attributeId,
  double value,
);

@JS('Module._getFilterParams')
external int wasmGetFilterParams(
  int handle,
  int filterType,
  int attributeId,
  int paramValuePtr,
);

@JS('Module._play3d')
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

@JS('Module._set3dSoundSpeed')
external void wasmSet3dSoundSpeed(double speed);

@JS('Module._get3dSoundSpeed')
external double wasmGet3dSoundSpeed();

@JS('Module._set3dListenerParameters')
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

@JS('Module._set3dListenerPosition')
external void wasmSet3dListenerPosition(double posX, double posY, double posZ);

@JS('Module._set3dListenerAt')
external void wasmSet3dListenerAt(double atX, double atY, double atZ);

@JS('Module._set3dListenerUp')
external void wasmSet3dListenerUp(double upX, double upY, double upZ);

@JS('Module._set3dListenerVelocity')
external void wasmSet3dListenerVelocity(
  double velocityX,
  double velocityY,
  double velocityZ,
);

@JS('Module._set3dSourceParameters')
external void wasmSet3dSourceParameters(
  int handle,
  double posX,
  double posY,
  double posZ,
  double velocityX,
  double velocityY,
  double velocityZ,
);

@JS('Module._set3dSourcePosition')
external void wasmSet3dSourcePosition(
  int handle,
  double posX,
  double posY,
  double posZ,
);

@JS('Module._set3dSourceVelocity')
external void wasmSet3dSourceVelocity(
  int handle,
  double velocityX,
  double velocityY,
  double velocityZ,
);

@JS('Module._set3dSourceMinMaxDistance')
external void wasmSet3dSourceMinMaxDistance(
  int handle,
  double minDistance,
  double maxDistance,
);

@JS('Module._set3dSourceAttenuation')
external void wasmSet3dSourceAttenuation(
  int handle,
  int attenuationModel,
  double attenuationRolloffFactor,
);

@JS('Module._set3dSourceDopplerFactor')
external void wasmSet3dSourceDopplerFactor(int handle, double dopplerFactor);
