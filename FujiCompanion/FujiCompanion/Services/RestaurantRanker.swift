import Foundation

enum RestaurantRanker {
    static func rank(
        _ restaurants: [Restaurant],
        for criteria: RestaurantSearchCriteria,
        limit: Int = 3
    ) -> [RestaurantRecommendation] {
        let avoidTerms = criteria.avoidTerms.map { $0.lowercased() }

        let eligible = restaurants.filter { restaurant in
            if let distance = restaurant.distanceMeters,
               distance > Double(criteria.radiusMeters) {
                return false
            }
            if let cost = restaurant.averageCostRMB,
               cost > Double(criteria.budgetRMB) {
                return false
            }
            return !avoidTerms.contains { restaurant.searchableText.contains($0) }
        }

        let sorted = eligible.sorted { lhs, rhs in
            score(lhs, criteria: criteria) > score(rhs, criteria: criteria)
        }

        var selected: [Restaurant] = []
        var categories = Set<String>()

        for restaurant in sorted where selected.count < limit {
            if categories.insert(restaurant.category).inserted {
                selected.append(restaurant)
            }
        }
        for restaurant in sorted where selected.count < limit && !selected.contains(where: { $0.id == restaurant.id }) {
            selected.append(restaurant)
        }

        return selected.map { restaurant in
            RestaurantRecommendation(
                restaurant: restaurant,
                reason: reason(for: restaurant, criteria: criteria),
                dietaryNeedsConfirmation: !avoidTerms.isEmpty
            )
        }
    }

    private static func score(_ restaurant: Restaurant, criteria: RestaurantSearchCriteria) -> Double {
        var value = 0.0
        if let distance = restaurant.distanceMeters {
            value += max(0, 40 - distance / 100)
        }
        if let cost = restaurant.averageCostRMB {
            value += cost <= Double(criteria.budgetRMB) ? 35 : 0
        } else {
            value += 8
        }
        if restaurant.openingHoursToday?.isEmpty == false {
            value += 6
        }
        if !restaurant.tags.isEmpty {
            value += 4
        }
        return value
    }

    private static func reason(for restaurant: Restaurant, criteria: RestaurantSearchCriteria) -> String {
        var parts: [String] = []
        if let distance = restaurant.distanceMeters {
            parts.append(distance < 1_000
                ? "约 \(Int(distance)) 米"
                : String(format: "约 %.1f 公里", distance / 1_000))
        }
        if let cost = restaurant.averageCostRMB {
            parts.append("人均约 ¥\(Int(cost))")
        } else {
            parts.append("价格待确认")
        }
        if let firstTag = restaurant.tags.first, !firstTag.isEmpty {
            parts.append(firstTag)
        }
        return parts.joined(separator: " · ")
    }
}
