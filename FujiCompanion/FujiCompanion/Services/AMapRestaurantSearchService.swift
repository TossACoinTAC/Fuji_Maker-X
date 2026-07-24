@preconcurrency import AMapFoundationKit
@preconcurrency import AMapSearchKit
import Foundation

@MainActor
final class AMapRestaurantSearchService: NSObject, RestaurantSearching, AMapSearchDelegate {
    private let consentProvider: () -> Bool
    private var searchAPI: AMapSearchAPI?
    private var continuation: CheckedContinuation<[Restaurant], Error>?

    init(consentProvider: @escaping () -> Bool) {
        self.consentProvider = consentProvider
        super.init()
    }

    func search(
        near coordinate: GeoCoordinate,
        criteria: RestaurantSearchCriteria
    ) async throws -> [Restaurant] {
        guard consentProvider() else {
            throw RestaurantSearchError.privacyConsentRequired
        }
        guard continuation == nil else {
            throw RestaurantSearchError.requestInProgress
        }

        let api = try configuredSearchAPI()
        let request = AMapPOIAroundSearchRequest()
        request.location = AMapGeoPoint.location(
            withLatitude: CGFloat(coordinate.latitude),
            longitude: CGFloat(coordinate.longitude)
        )
        request.keywords = "餐厅"
        request.types = "050000"
        request.radius = criteria.radiusMeters
        request.sortrule = 0
        request.offset = 25
        request.page = 1
        request.showFieldsType = [.business, .navi]

        return try await withCheckedThrowingContinuation { continuation in
            self.continuation = continuation
            api.aMapPOIAroundSearch(request)
        }
    }

    func withdrawPrivacyConsent() {
        AMapSearchAPI.updatePrivacyAgree(.notAgree)
        searchAPI?.cancelAllRequests()
        searchAPI = nil
        finish(.failure(RestaurantSearchError.privacyConsentRequired))
    }

    private func configuredSearchAPI() throws -> AMapSearchAPI {
        if let searchAPI { return searchAPI }

        let key = (Bundle.main.object(forInfoDictionaryKey: "AMapAPIKey") as? String)?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        guard !key.isEmpty, key != "$(AMAP_API_KEY)" else {
            throw RestaurantSearchError.missingAPIKey
        }

        AMapSearchAPI.updatePrivacyShow(.didShow, privacyInfo: .didContain)
        AMapSearchAPI.updatePrivacyAgree(.didAgree)
        AMapServices.shared().enableHTTPS = true
        AMapServices.shared().apiKey = key

        let api = AMapSearchAPI()
        api?.delegate = self
        guard let api else {
            throw RestaurantSearchError.provider("SDK 初始化失败")
        }
        searchAPI = api
        return api
    }

    func onPOISearchDone(
        _ request: AMapPOISearchBaseRequest!,
        response: AMapPOISearchResponse!
    ) {
        let restaurants = (response?.pois ?? []).compactMap(Self.makeRestaurant)
        guard !restaurants.isEmpty else {
            finish(.failure(RestaurantSearchError.noResults))
            return
        }
        finish(.success(restaurants))
    }

    func aMapSearchRequest(_ request: Any!, didFailWithError error: Error!) {
        finish(.failure(RestaurantSearchError.provider(error.localizedDescription)))
    }

    private func finish(_ result: Result<[Restaurant], Error>) {
        let pending = continuation
        continuation = nil
        pending?.resume(with: result)
    }

    private static func makeRestaurant(from poi: AMapPOI) -> Restaurant? {
        guard let location = poi.location,
              let name = (poi.name as String?)?.nonEmpty else { return nil }
        let business = poi.businessData
        let cost = business?.cost.flatMap(Double.init)
        let tags = business?.tag
            .split(whereSeparator: { ",，;；".contains($0) })
            .map(String.init) ?? []

        return Restaurant(
            id: (poi.uid as String?)?.nonEmpty ?? "\(name)-\(location.latitude)-\(location.longitude)",
            name: name,
            coordinate: GeoCoordinate(
                latitude: Double(location.latitude),
                longitude: Double(location.longitude)
            ),
            address: (poi.address as String?)?.nonEmpty,
            type: (poi.type as String?)?.nonEmpty,
            tags: tags,
            averageCostRMB: cost,
            openingHoursToday: (business?.opentimeToday as String?)?.nonEmpty,
            distanceMeters: poi.distance > 0 ? Double(poi.distance) : nil,
            navigationPOIID: (poi.naviPOIId as String?)?.nonEmpty ?? (poi.uid as String?)?.nonEmpty
        )
    }
}

private extension String {
    var nonEmpty: String? { isEmpty ? nil : self }
}

@MainActor
final class FixtureRestaurantSearchService: RestaurantSearching {
    func search(
        near coordinate: GeoCoordinate,
        criteria: RestaurantSearchCriteria
    ) async throws -> [Restaurant] {
        [
            Restaurant(
                id: "fixture-noodles",
                name: "青禾小面",
                coordinate: GeoCoordinate(latitude: coordinate.latitude + 0.002, longitude: coordinate.longitude + 0.001),
                address: "静安路 18 号",
                type: "餐饮服务;中餐厅;面馆",
                tags: ["清汤面", "小份可选"],
                averageCostRMB: 38,
                openingHoursToday: "10:00-21:30",
                distanceMeters: 420,
                navigationPOIID: "fixture-noodles"
            ),
            Restaurant(
                id: "fixture-rice",
                name: "树下食堂",
                coordinate: GeoCoordinate(latitude: coordinate.latitude - 0.003, longitude: coordinate.longitude + 0.001),
                address: "梧桐街 6 号",
                type: "餐饮服务;中餐厅;简餐",
                tags: ["米饭", "家常菜"],
                averageCostRMB: 52,
                openingHoursToday: "11:00-22:00",
                distanceMeters: 680,
                navigationPOIID: "fixture-rice"
            ),
            Restaurant(
                id: "fixture-dumplings",
                name: "月白饺子铺",
                coordinate: GeoCoordinate(latitude: coordinate.latitude + 0.004, longitude: coordinate.longitude - 0.002),
                address: "栖霞路 27 号",
                type: "餐饮服务;中餐厅;饺子馆",
                tags: ["现包水饺"],
                averageCostRMB: nil,
                openingHoursToday: nil,
                distanceMeters: 930,
                navigationPOIID: "fixture-dumplings"
            )
        ]
    }

    func withdrawPrivacyConsent() {}
}
