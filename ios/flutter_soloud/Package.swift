// swift-tools-version: 5.9
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription
import class Foundation.ProcessInfo

// Check if Xiph libraries should be disabled via environment variable
// Usage: NO_XIPH_LIBS=1 swift build
var disableXiphLibs: Bool {
    ProcessInfo.processInfo.environment["NO_XIPH_LIBS"] == "1"
}

// Base dependencies that are always included
var baseDependencies: [Target.Dependency] = [
    .product(name: "FlutterFramework", package: "FlutterFramework")
]

// Add Xiph library dependencies only when not disabled
let targetDependencies: [Target.Dependency]
if disableXiphLibs {
    targetDependencies = baseDependencies
} else {
    targetDependencies = baseDependencies + [
        "opus", "ogg", "vorbis", "vorbisfile", "flac"
    ]
}

// Base compiler settings (always included)
var baseCSettings: [CSetting] = [
    .headerSearchPath("../../include"),
    .headerSearchPath("src"),
    .headerSearchPath("src/soloud/include"),
    .unsafeFlags(["-O3"]),
]

var baseCXXSettings: [CXXSetting] = [
    .headerSearchPath("../../include"),
    .headerSearchPath("src"),
    .headerSearchPath("src/soloud/include"),
    .unsafeFlags(["-O3"]),
]

// Add Xiph include paths only when not disabled
let cSettings: [CSetting]
let cxxSettings: [CXXSetting]
if disableXiphLibs {
    cSettings = baseCSettings + [
        .define("NO_XIPH_LIBS")
    ]
    cxxSettings = baseCXXSettings + [
        .define("NO_XIPH_LIBS")
    ]
} else {
    cSettings = baseCSettings + [
        .headerSearchPath("../../include/opus"),
        .headerSearchPath("../../include/ogg"),
        .headerSearchPath("../../include/vorbis"),
    ]
    cxxSettings = baseCXXSettings + [
        .headerSearchPath("../../include/opus"),
        .headerSearchPath("../../include/ogg"),
        .headerSearchPath("../../include/vorbis"),
    ]
}

// Build the targets array
var targets: [Target] = [
    .target(
        name: "flutter_soloud",
        dependencies: targetDependencies,
        exclude: [
            "src"
        ],
        resources: [
            // TODO: If your plugin requires a privacy manifest
            // (e.g., if it uses any required reason APIs), update the PrivacyInfo.xcprivacy file
            // to describe your plugin's privacy impact, and then uncomment this line.
            // For more information, see:
            // https://developer.apple.com/documentation/bundleresources/privacy_manifest_files
            // .process("PrivacyInfo.xcprivacy"),
        ],
        cSettings: cSettings,
        cxxSettings: cxxSettings,
        linkerSettings: [
            .linkedFramework("AudioToolbox"),
            .linkedFramework("AVFAudio"),
            .unsafeFlags(["-Wl,-undefined,dynamic_lookup"]),
        ]
    )
]

// Add binary targets only when not disabled
if !disableXiphLibs {
    targets.append(contentsOf: [
        .binaryTarget(name: "opus", path: "Frameworks/opus.xcframework"),
        .binaryTarget(name: "ogg", path: "Frameworks/ogg.xcframework"),
        .binaryTarget(name: "vorbis", path: "Frameworks/vorbis.xcframework"),
        .binaryTarget(name: "vorbisfile", path: "Frameworks/vorbisfile.xcframework"),
        .binaryTarget(name: "flac", path: "Frameworks/flac.xcframework")
    ])
}

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
    targets: targets,
    cxxLanguageStandard: .cxx17
)
