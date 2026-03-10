// swift-tools-version: 5.9
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "flutter_soloud",
    platforms: [
        .iOS("13.0")
    ],
    products: [
        .library(name: "flutter-soloud", targets: ["flutter_soloud"])
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
            resources: [
                // TODO: If your plugin requires a privacy manifest
                // (e.g. if it uses any required reason APIs), update the PrivacyInfo.xcprivacy file
                // to describe your plugin's privacy impact, and then uncomment this line.
                // For more information, see:
                // https://developer.apple.com/documentation/bundleresources/privacy_manifest_files
                // .process("PrivacyInfo.xcprivacy"),
            ],
            cSettings: [
                .headerSearchPath("../../../include"),
                .headerSearchPath("../../../include/opus"),
                .headerSearchPath("../../../include/ogg"),
                .headerSearchPath("../../../include/vorbis"),
                .headerSearchPath("../../../../src"),
                .headerSearchPath("../../../../src/soloud/include"),
            ],
            cxxSettings: [
                .headerSearchPath("../../../include"),
                .headerSearchPath("../../../include/opus"),
                .headerSearchPath("../../../include/ogg"),
                .headerSearchPath("../../../include/vorbis"),
                .headerSearchPath("../../../../src"),
                .headerSearchPath("../../../../src/soloud/include"),
            ],
            linkerSettings: [
                .linkedFramework("AudioToolbox"),
                .linkedFramework("AVFAudio"),
            ]
        )
    ]
)
