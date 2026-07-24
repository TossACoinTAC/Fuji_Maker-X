@preconcurrency import MapKit
import UIKit

@MainActor
protocol AppleMapItemResolving: AnyObject {
    func resolve(_ restaurant: Restaurant, near origin: GeoCoordinate?) async -> MKMapItem?
}

@MainActor
protocol ExternalMapOpening: AnyObject {
    func openAppleMaps(_ item: MKMapItem) -> Bool
    func canOpen(_ url: URL) -> Bool
    func open(_ url: URL) async -> Bool
}

@MainActor
final class AppleMapItemResolver: AppleMapItemResolving {
    func resolve(_ restaurant: Restaurant, near origin: GeoCoordinate?) async -> MKMapItem? {
        let request = MKLocalSearch.Request()
        request.naturalLanguageQuery = [restaurant.name, restaurant.address]
            .compactMap { $0 }
            .joined(separator: " ")
        request.resultTypes = .pointOfInterest
        let center = origin?.coreLocationCoordinate ?? restaurant.coordinate.coreLocationCoordinate
        request.region = MKCoordinateRegion(
            center: center,
            latitudinalMeters: 4_000,
            longitudinalMeters: 4_000
        )

        guard let response = try? await MKLocalSearch(request: request).start() else {
            return nil
        }
        let expectedName = normalized(restaurant.name)
        let matches = response.mapItems.filter { item in
            guard let candidate = item.name else { return false }
            let candidateName = normalized(candidate)
            return candidateName == expectedName
                || candidateName.contains(expectedName)
                || expectedName.contains(candidateName)
        }
        return matches.count == 1 ? matches[0] : nil
    }

    private func normalized(_ value: String) -> String {
        value
            .lowercased()
            .filter { $0.isLetter || $0.isNumber }
    }
}

@MainActor
final class SystemExternalMapOpener: ExternalMapOpening {
    func openAppleMaps(_ item: MKMapItem) -> Bool {
        item.openInMaps(launchOptions: [
            MKLaunchOptionsDirectionsModeKey: MKLaunchOptionsDirectionsModeWalking
        ])
    }

    func canOpen(_ url: URL) -> Bool {
        UIApplication.shared.canOpenURL(url)
    }

    func open(_ url: URL) async -> Bool {
        await withCheckedContinuation { continuation in
            UIApplication.shared.open(url, options: [:]) { succeeded in
                continuation.resume(returning: succeeded)
            }
        }
    }
}

@MainActor
final class HybridNavigationService: NavigationLaunching {
    private let resolver: AppleMapItemResolving
    private let opener: ExternalMapOpening

    init(
        resolver: AppleMapItemResolving? = nil,
        opener: ExternalMapOpening? = nil
    ) {
        self.resolver = resolver ?? AppleMapItemResolver()
        self.opener = opener ?? SystemExternalMapOpener()
    }

    func launchWalkingRoute(
        to restaurant: Restaurant,
        from origin: GeoCoordinate?
    ) async -> NavigationOutcome {
        if let item = await resolver.resolve(restaurant, near: origin),
           opener.openAppleMaps(item) {
            return .launched(.appleMaps)
        }

        if let appURL = Self.amapAppURL(for: restaurant),
           opener.canOpen(appURL),
           await opener.open(appURL) {
            return .launched(.amapApp)
        }

        if let webURL = Self.amapWebURL(for: restaurant),
           await opener.open(webURL) {
            return .launched(.amapWeb)
        }
        return .failed("Apple 地图与高德地图均未能打开")
    }

    static func amapAppURL(for restaurant: Restaurant) -> URL? {
        var components = URLComponents()
        components.scheme = "iosamap"
        components.host = "path"
        components.queryItems = [
            URLQueryItem(name: "sourceApplication", value: "FujiCompanion"),
            URLQueryItem(name: "did", value: restaurant.navigationPOIID),
            URLQueryItem(name: "dlat", value: String(restaurant.coordinate.latitude)),
            URLQueryItem(name: "dlon", value: String(restaurant.coordinate.longitude)),
            URLQueryItem(name: "dname", value: restaurant.name),
            URLQueryItem(name: "dev", value: "0"),
            URLQueryItem(name: "t", value: "2")
        ]
        return components.url
    }

    static func amapWebURL(for restaurant: Restaurant) -> URL? {
        var components = URLComponents(string: "https://uri.amap.com/navigation")
        let destination = "\(restaurant.coordinate.longitude),\(restaurant.coordinate.latitude),\(restaurant.name)"
        components?.queryItems = [
            URLQueryItem(name: "from", value: ""),
            URLQueryItem(name: "to", value: destination),
            URLQueryItem(name: "mode", value: "walk"),
            URLQueryItem(name: "policy", value: "0"),
            URLQueryItem(name: "src", value: "FujiCompanion"),
            URLQueryItem(name: "callnative", value: "1")
        ]
        return components?.url
    }
}

@MainActor
final class FixtureNavigationService: NavigationLaunching {
    private let outcome: NavigationOutcome

    init(outcome: NavigationOutcome? = nil) {
        self.outcome = outcome ?? .launched(.appleMaps)
    }

    func launchWalkingRoute(
        to restaurant: Restaurant,
        from origin: GeoCoordinate?
    ) async -> NavigationOutcome {
        outcome
    }
}
