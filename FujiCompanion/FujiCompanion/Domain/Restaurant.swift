import CoreLocation
import Foundation

struct GeoCoordinate: Codable, Equatable {
    let latitude: Double
    let longitude: Double

    init(latitude: Double, longitude: Double) {
        self.latitude = latitude
        self.longitude = longitude
    }

    init(_ coordinate: CLLocationCoordinate2D) {
        self.init(latitude: coordinate.latitude, longitude: coordinate.longitude)
    }

    var coreLocationCoordinate: CLLocationCoordinate2D {
        CLLocationCoordinate2D(latitude: latitude, longitude: longitude)
    }
}

struct RestaurantSearchCriteria: Equatable {
    var radiusMeters = 1_500
    var budgetRMB = 80
    var avoidText = ""

    var avoidTerms: [String] {
        avoidText
            .split(whereSeparator: { ",，、 ".contains($0) })
            .map { String($0).trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty }
    }
}

struct Restaurant: Identifiable, Equatable {
    let id: String
    let name: String
    let coordinate: GeoCoordinate
    let address: String?
    let type: String?
    let tags: [String]
    let averageCostRMB: Double?
    let openingHoursToday: String?
    let distanceMeters: Double?
    let navigationPOIID: String?

    var category: String {
        type?
            .split(separator: ";")
            .dropFirst()
            .first
            .map(String.init) ?? "餐饮"
    }

    var searchableText: String {
        ([name] + [type, address].compactMap { $0 } + tags)
            .joined(separator: " ")
            .lowercased()
    }
}

struct RestaurantRecommendation: Identifiable, Equatable {
    let restaurant: Restaurant
    let reason: String
    let dietaryNeedsConfirmation: Bool

    var id: String { restaurant.id }
}

enum RestaurantSearchError: LocalizedError, Equatable {
    case privacyConsentRequired
    case missingAPIKey
    case requestInProgress
    case noResults
    case provider(String)

    var errorDescription: String? {
        switch self {
        case .privacyConsentRequired:
            "请先在设置中同意高德搜索隐私说明"
        case .missingAPIKey:
            "高德搜索尚未配置"
        case .requestInProgress:
            "已有一次餐馆搜索正在进行"
        case .noResults:
            "附近没有找到符合条件的餐馆"
        case .provider(let message):
            "餐馆搜索失败：\(message)"
        }
    }
}
