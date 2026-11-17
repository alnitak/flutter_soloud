//
//  Generated file. Do not edit.
//

import FlutterMacOS
import Foundation

import audio_session
import audio_service
import file_picker
import flutter_soloud
import sqflite_darwin

func RegisterGeneratedPlugins(registry: FlutterPluginRegistry) {
  AudioSessionPlugin.register(with: registry.registrar(forPlugin: "AudioSessionPlugin"))
  AudioServicePlugin.register(with: registry.registrar(forPlugin: "AudioServicePlugin"))
  FilePickerPlugin.register(with: registry.registrar(forPlugin: "FilePickerPlugin"))
  FlutterSoloudPlugin.register(with: registry.registrar(forPlugin: "FlutterSoloudPlugin"))
  SqflitePlugin.register(with: registry.registrar(forPlugin: "SqflitePlugin"))
}
