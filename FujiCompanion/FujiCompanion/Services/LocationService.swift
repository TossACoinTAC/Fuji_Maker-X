@preconcurrency import CoreLocation
import Foundation

enum LocationServiceError: LocalizedError {
    case servicesDisabled
    case permissionDenied
    case requestInProgress
    case unavailable

    var errorDescription: String? {
        switch self {
        case .servicesDisabled: "系统定位服务未开启"
        case .permissionDenied: "没有当前位置权限，请在系统设置中允许"
        case .requestInProgress: "正在获取当前位置"
        case .unavailable: "暂时无法取得当前位置"
        }
    }
}

@MainActor
final class LocationService: NSObject, LocationProviding, CLLocationManagerDelegate {
    private let manager = CLLocationManager()
    private var continuation: CheckedContinuation<GeoCoordinate, Error>?

    override init() {
        super.init()
        manager.delegate = self
        manager.desiredAccuracy = kCLLocationAccuracyHundredMeters
    }

    var authorizationLabel: String {
        switch manager.authorizationStatus {
        case .notDetermined: "使用时询问"
        case .restricted: "受系统限制"
        case .denied: "已拒绝"
        case .authorizedAlways, .authorizedWhenInUse: "使用 App 时允许"
        @unknown default: "未知"
        }
    }

    func currentCoordinate() async throws -> GeoCoordinate {
        guard CLLocationManager.locationServicesEnabled() else {
            throw LocationServiceError.servicesDisabled
        }
        guard continuation == nil else {
            throw LocationServiceError.requestInProgress
        }

        return try await withCheckedThrowingContinuation { continuation in
            self.continuation = continuation
            beginRequestIfAuthorized()
        }
    }

    private func beginRequestIfAuthorized() {
        switch manager.authorizationStatus {
        case .notDetermined:
            manager.requestWhenInUseAuthorization()
        case .authorizedAlways, .authorizedWhenInUse:
            manager.requestLocation()
        case .restricted, .denied:
            finish(.failure(LocationServiceError.permissionDenied))
        @unknown default:
            finish(.failure(LocationServiceError.unavailable))
        }
    }

    func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        guard continuation != nil else { return }
        beginRequestIfAuthorized()
    }

    func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        guard let location = locations.last else {
            finish(.failure(LocationServiceError.unavailable))
            return
        }
        finish(.success(GeoCoordinate(location.coordinate)))
    }

    func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        finish(.failure(error))
    }

    private func finish(_ result: Result<GeoCoordinate, Error>) {
        let pending = continuation
        continuation = nil
        pending?.resume(with: result)
    }
}

@MainActor
final class FixtureLocationService: LocationProviding {
    var authorizationLabel: String { "测试位置" }
    private let coordinate: GeoCoordinate

    init(coordinate: GeoCoordinate? = nil) {
        self.coordinate = coordinate ?? GeoCoordinate(latitude: 31.2304, longitude: 121.4737)
    }

    func currentCoordinate() async throws -> GeoCoordinate { coordinate }
}
