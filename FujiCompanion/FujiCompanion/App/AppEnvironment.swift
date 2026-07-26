import Foundation

@MainActor
struct AppEnvironment {
    let settings: AppSettings
    let audioRouteMonitor: AudioRouteMonitor
    let transport: DeviceTransport
    let locationProvider: LocationProviding
    let restaurantSearch: RestaurantSearching
    let navigationLauncher: NavigationLaunching
    let speechOutput: SpeechOutput

    static func live() -> AppEnvironment {
        let isUITesting = ProcessInfo.processInfo.environment["UITEST_MODE"] == "1"
        let isOfflineTest = ProcessInfo.processInfo.environment["UITEST_OFFLINE"] == "1"
        let isNavigationFailureTest = ProcessInfo.processInfo.environment["UITEST_NAVIGATION_FAILURE"] == "1"
        let settings = AppSettings()
        let audioRouteMonitor = AudioRouteMonitor()

        if isUITesting {
            settings.amapPrivacyAccepted = true
            return AppEnvironment(
                settings: settings,
                audioRouteMonitor: audioRouteMonitor,
                transport: MockDeviceTransport(connectsSuccessfully: !isOfflineTest),
                locationProvider: FixtureLocationService(),
                restaurantSearch: FixtureRestaurantSearchService(),
                navigationLauncher: FixtureNavigationService(
                    outcome: isNavigationFailureTest ? .failed("测试地图不可用") : nil
                ),
                speechOutput: FixtureSpeechOutput()
            )
        }

        return AppEnvironment(
            settings: settings,
            audioRouteMonitor: audioRouteMonitor,
            transport: CoreBluetoothDeviceTransport(),
            locationProvider: LocationService(),
            restaurantSearch: AMapRestaurantSearchService {
                settings.amapPrivacyAccepted
            },
            navigationLauncher: HybridNavigationService(),
            speechOutput: SystemSpeechOutput(routeMonitor: audioRouteMonitor)
        )
    }
}
