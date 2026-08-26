// swift-tools-version: 5.9
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

// The native engine is built by the Dart build hook (hook/build.dart) and
// bundled as a native code asset. This package only provides the
// FlutterSoloudPlugin class, which observes the FlutterEngine lifecycle.
let package = Package(
    name: "flutter_soloud",
    platforms: [
        .iOS("13.0")
    ],
    products: [
        .library(name: "flutter-soloud", type: .dynamic, targets: ["flutter_soloud"])
    ],
    dependencies: [
        .package(name: "FlutterFramework", path: "../FlutterFramework")
    ],
    targets: [
        .target(
            name: "flutter_soloud",
            dependencies: [
                .product(name: "FlutterFramework", package: "FlutterFramework")
            ],
            exclude: [
                // Symlink to the plugin's C++ sources, needed only for the
                // "engine_lifecycle.h" header search path below.
                "src",
                // Force-included by the build hook on iOS, not compiled here.
                "miniaudio_objc_prefix.h"
            ],
            cSettings: [
                .headerSearchPath("src")
            ],
            cxxSettings: [
                .headerSearchPath("src")
            ],
            linkerSettings: [
                // FlutterSoloudPlugin.mm calls the engine-lifecycle exports,
                // which live in the native code asset built by the Dart build
                // hook; they are resolved at load time.
                .unsafeFlags(["-Xlinker", "-undefined", "-Xlinker", "dynamic_lookup"])
            ]
        )
    ],
    cxxLanguageStandard: .cxx17
)
