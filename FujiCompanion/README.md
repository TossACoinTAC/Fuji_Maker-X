# Fuji Companion P0

Fuji Companion is the iPhone-side P0 for the Fuji smart pet. It targets iOS
18.0 and is built with the iOS 26.5 SDK, so the same binary can be installed on
an iOS 26.5 iPhone.

## Run

1. Install CocoaPods 1.17.0 and run `pod install` in this directory.
2. Put the Bundle-ID-bound AMap iOS key in `Config/Secrets.xcconfig`, following
   `Config/Secrets.example.xcconfig`. The local secrets file is ignored by Git.
3. Open `FujiCompanion.xcworkspace`, select the shared `FujiCompanion` scheme,
   and run on an iPhone or an installed iOS simulator.

The app can build without a secrets file. Nearby search then reports that AMap
is not configured instead of embedding a key in source control.

AMap 9.8.0 does not contain an Apple Silicon simulator slice. Debug and test
builds therefore use the fixture search service, while Release device builds
link the real AMap SDK. Real nearby search must be verified on an iPhone using
the Release configuration.

## Local developer storage

On the primary development Mac, mount `Mac_DiskExtension` before opening Xcode.
Project DerivedData lives at
`/Volumes/Mac_DiskExtension/XcodeCache/FujiCompanion`, and the user-level iOS
DeviceSupport directory lives at
`/Volumes/Mac_DiskExtension/Developer/Toolchains/Xcode/iOS DeviceSupport` with
a compatibility symlink from Xcode's standard Library path. Xcode.app's signed
toolchains and platforms, along with the global CoreSimulator runtime, remain
in their standard system locations.

## P0 boundary

The complete food flow runs through `MockDeviceTransport`: request, nearby
search, up to three recommendations, private TTS, explicit confirmation, and
walking navigation. Production BLE GATT and provisioning remain behind
`DeviceTransport`; the business layer has no board name, GATT UUID, or BluFi
dependency.

Location is requested only when a search starts and raw coordinates are not
persisted. AMap SDK setup happens only after the user accepts its privacy notice.
Navigation first resolves an unambiguous Apple Maps item, then falls back to the
AMap app URI and finally its HTTPS URI. TTS defaults to a confirmed private audio
route and stops if that route disappears.

## Verification

The app and both test targets compile for an iOS 18.0 simulator destination:

```sh
xcodebuild -workspace FujiCompanion.xcworkspace \
  -scheme FujiCompanion \
  -sdk iphonesimulator \
  -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath /Volumes/Mac_DiskExtension/XcodeCache/FujiCompanion \
  CODE_SIGNING_ALLOWED=NO build-for-testing
```

Verified on 2026-07-25 with an iPhone 15 Pro Max simulator running iOS 26.5:
all 13 unit tests and all 4 functional UI flows passed. Debug and Release
arm64 device builds also passed; the signed Debug app was installed and launched
on an iPhone 15 Pro Max running iOS 26.5.2.

Real AMap queries, navigation fallback, and AirPods routing/ducking still need
interactive Release-device verification before release.
