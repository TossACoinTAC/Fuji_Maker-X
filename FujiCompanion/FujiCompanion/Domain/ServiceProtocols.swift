import Foundation

enum DeviceConnectionState: String, Equatable {
    case disconnected
    case connecting
    case connected

    var label: String {
        switch self {
        case .disconnected: "未连接"
        case .connecting: "连接中"
        case .connected: "演示设备已连接"
        }
    }
}

@MainActor
protocol DeviceTransport: AnyObject {
    var connectionState: DeviceConnectionState { get }
    var messages: AsyncStream<FujiEnvelope> { get }

    func connect()
    func disconnect()
    func send(_ envelope: FujiEnvelope) async throws
}

@MainActor
protocol LocationProviding: AnyObject {
    var authorizationLabel: String { get }
    func currentCoordinate() async throws -> GeoCoordinate
}

@MainActor
protocol RestaurantSearching: AnyObject {
    func search(near coordinate: GeoCoordinate, criteria: RestaurantSearchCriteria) async throws -> [Restaurant]
    func withdrawPrivacyConsent()
}

enum NavigationDestination: String, Equatable {
    case appleMaps
    case amapApp
    case amapWeb
}

enum NavigationOutcome: Equatable {
    case launched(NavigationDestination)
    case failed(String)

    var succeeded: Bool {
        if case .launched = self { return true }
        return false
    }

    var message: String {
        switch self {
        case .launched(.appleMaps): "已在 Apple 地图中打开步行路线"
        case .launched(.amapApp): "已在高德地图中打开步行路线"
        case .launched(.amapWeb): "已打开高德网页版步行路线"
        case .failed(let reason): "导航启动失败：\(reason)"
        }
    }
}

@MainActor
protocol NavigationLaunching: AnyObject {
    func launchWalkingRoute(to restaurant: Restaurant, from origin: GeoCoordinate?) async -> NavigationOutcome
}

enum AudioPolicy: String, CaseIterable, Hashable {
    case privateOnly
    case allowPhoneSpeaker

    var label: String {
        switch self {
        case .privateOnly: "仅耳机"
        case .allowPhoneSpeaker: "允许手机扬声器"
        }
    }
}

enum SpeechOutcome: Equatable {
    case spoken
    case privateRouteUnavailable
    case routeLost
    case failed(String)
}

@MainActor
protocol SpeechOutput: AnyObject {
    func speak(_ text: String, policy: AudioPolicy) async -> SpeechOutcome
    func stop()
}
