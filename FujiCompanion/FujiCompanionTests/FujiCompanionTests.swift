import MapKit
import Testing
@testable import FujiCompanion

@Suite("Fuji 协议")
@MainActor
struct FujiProtocolTests {
    @Test("JSON 往返保留版本化信封")
    func envelopeRoundTrip() throws {
        let now = Date(timeIntervalSince1970: 1_800_000_000)
        let original = FujiEnvelope.foodSearch(
            requestID: UUID(uuidString: "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE")!,
            now: now
        )

        let data = try JSONEncoder().encode(original)
        let decoded = try JSONDecoder().decode(FujiEnvelope.self, from: data)

        #expect(decoded == original)
        #expect(decoded.version == FujiEnvelope.supportedVersion)
    }

    @Test("拒绝重复请求")
    func duplicateRequest() throws {
        let validator = FujiEnvelopeValidator()
        let envelope = FujiEnvelope.foodSearch()

        try validator.validate(envelope)
        #expect(throws: FujiEnvelopeValidationError.duplicate) {
            try validator.validate(envelope)
        }
    }

    @Test("拒绝过期请求")
    func expiredRequest() {
        let now = Date(timeIntervalSince1970: 1_800_000_000)
        let envelope = FujiEnvelope.foodSearch(now: now, lifetime: -1)

        #expect(throws: FujiEnvelopeValidationError.expired) {
            try FujiEnvelopeValidator().validate(envelope, now: now)
        }
    }

    @Test("拒绝不支持的协议版本")
    func unsupportedVersion() {
        let envelope = FujiEnvelope(
            version: 99,
            requestID: UUID(),
            intent: .foodSearch,
            state: .clarifying,
            parameters: [:],
            requiresConfirmation: false,
            createdAt: .now,
            expiresAt: .now.addingTimeInterval(60)
        )

        #expect(throws: FujiEnvelopeValidationError.unsupportedVersion(99)) {
            try FujiEnvelopeValidator().validate(envelope)
        }
    }
}

@Suite("餐馆排序")
@MainActor
struct RestaurantRankerTests {
    @Test("按预算范围过滤，同时保留未知字段并保证类型多样")
    func rankingAndUnknownFields() {
        var criteria = RestaurantSearchCriteria()
        criteria.radiusMeters = 1_500
        criteria.budgetRMB = 80
        criteria.avoidText = "花生"

        let input = [
            restaurant("near", "近处面馆", type: "餐饮服务;中餐厅;面馆", cost: 35, distance: 250),
            restaurant("unknown", "价格未知小馆", type: "餐饮服务;快餐厅;简餐", cost: nil, distance: 400),
            restaurant("diverse", "清蔬料理", type: "餐饮服务;外国餐厅;料理", cost: 70, distance: 500),
            restaurant("over-budget", "昂贵餐厅", type: "餐饮服务;中餐厅;正餐", cost: 180, distance: 300),
            restaurant("outside", "远处餐厅", type: "餐饮服务;中餐厅;正餐", cost: 40, distance: 2_000),
            restaurant("allergen", "花生小馆", type: "餐饮服务;中餐厅;小吃", cost: 25, distance: 100)
        ]

        let results = RestaurantRanker.rank(input, for: criteria)
        let ids = Set(results.map(\.id))

        #expect(results.count == 3)
        #expect(ids == ["near", "unknown", "diverse"])
        #expect(results.first(where: { $0.id == "unknown" })?.restaurant.averageCostRMB == nil)
        #expect(results.allSatisfy { $0.dietaryNeedsConfirmation })
    }

    private func restaurant(
        _ id: String,
        _ name: String,
        type: String,
        cost: Double?,
        distance: Double
    ) -> Restaurant {
        Restaurant(
            id: id,
            name: name,
            coordinate: GeoCoordinate(latitude: 31.23, longitude: 121.47),
            address: nil,
            type: type,
            tags: [],
            averageCostRMB: cost,
            openingHoursToday: nil,
            distanceMeters: distance,
            navigationPOIID: nil
        )
    }
}

@Suite("导航回退")
@MainActor
struct NavigationTests {
    @Test("Apple 地图匹配成功时优先打开步行路线")
    func appleMapsFirst() async {
        let item = MKMapItem(
            placemark: MKPlacemark(
                coordinate: CLLocationCoordinate2D(latitude: 31.23, longitude: 121.47)
            )
        )
        let resolver = StubMapResolver(item: item)
        let opener = StubMapOpener(appleResult: true, canOpenAMap: true)
        let service = HybridNavigationService(resolver: resolver, opener: opener)

        let outcome = await service.launchWalkingRoute(to: sampleRestaurant, from: nil)

        #expect(outcome == .launched(.appleMaps))
        #expect(opener.openedURLs.isEmpty)
    }

    @Test("Apple 匹配失败后使用高德 App")
    func amapAppFallback() async {
        let opener = StubMapOpener(appleResult: false, canOpenAMap: true)
        let service = HybridNavigationService(resolver: StubMapResolver(item: nil), opener: opener)

        let outcome = await service.launchWalkingRoute(to: sampleRestaurant, from: nil)

        #expect(outcome == .launched(.amapApp))
        #expect(opener.openedURLs.first?.scheme == "iosamap")
    }

    @Test("未安装高德时回退 HTTPS")
    func amapWebFallback() async {
        let opener = StubMapOpener(appleResult: false, canOpenAMap: false)
        let service = HybridNavigationService(resolver: StubMapResolver(item: nil), opener: opener)

        let outcome = await service.launchWalkingRoute(to: sampleRestaurant, from: nil)

        #expect(outcome == .launched(.amapWeb))
        #expect(opener.openedURLs.first?.scheme == "https")
        #expect(opener.openedURLs.first?.absoluteString.contains("mode=walk") == true)
    }

    @Test("高德 App URI 明确请求步行模式")
    func amapWalkingURI() {
        let items = URLComponents(
            url: HybridNavigationService.amapAppURL(for: sampleRestaurant)!,
            resolvingAgainstBaseURL: false
        )?.queryItems

        #expect(items?.first(where: { $0.name == "t" })?.value == "2")
        #expect(items?.first(where: { $0.name == "dev" })?.value == "0")
    }

    private var sampleRestaurant: Restaurant {
        Restaurant(
            id: "sample",
            name: "测试餐馆",
            coordinate: GeoCoordinate(latitude: 31.23, longitude: 121.47),
            address: "测试路 1 号",
            type: "餐饮服务;中餐厅;正餐",
            tags: [],
            averageCostRMB: 50,
            openingHoursToday: "11:00-21:00",
            distanceMeters: 500,
            navigationPOIID: "B000TEST"
        )
    }
}

@Suite("失败路径与音频策略")
@MainActor
struct FailurePathTests {
    @Test("仅耳机策略在开始前拒绝手机扬声器")
    func privateRouteRequired() {
        #expect(!SpeechRoutePolicy.canStart(.privateOnly, hasPrivateRoute: false))
        #expect(SpeechRoutePolicy.canStart(.privateOnly, hasPrivateRoute: true))
        #expect(SpeechRoutePolicy.canStart(.allowPhoneSpeaker, hasPrivateRoute: false))
    }

    @Test("耳机中途断开时停止播报")
    func routeLossStopsSpeech() {
        #expect(SpeechRoutePolicy.shouldStop(.privateOnly, hasPrivateRoute: false))
        #expect(!SpeechRoutePolicy.shouldStop(.allowPhoneSpeaker, hasPrivateRoute: false))
        #expect(!SpeechRoutePolicy.shouldStop(.privateOnly, hasPrivateRoute: true))
    }

    @Test("定位拒绝会终止搜索并显示真实错误")
    func locationDenied() async {
        let model = makeModel(
            location: FailingLocationProvider(error: LocationServiceError.permissionDenied),
            search: EmptyRestaurantSearch()
        )
        model.settings.amapPrivacyAccepted = true

        await model.searchRestaurants()

        #expect(model.flowStage == .failed)
        #expect(model.presentedError?.contains("位置权限") == true)
    }

    @Test("无搜索结果会进入失败状态")
    func noResults() async {
        let model = makeModel(
            location: FixtureLocationService(),
            search: EmptyRestaurantSearch()
        )
        model.settings.amapPrivacyAccepted = true

        await model.searchRestaurants()

        #expect(model.flowStage == .failed)
        #expect(model.presentedError == RestaurantSearchError.noResults.localizedDescription)
    }

    private func makeModel(
        location: LocationProviding,
        search: RestaurantSearching
    ) -> FujiAppModel {
        let suiteName = "FujiCompanionTests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suiteName)!
        defaults.removePersistentDomain(forName: suiteName)
        let routeMonitor = AudioRouteMonitor()
        return FujiAppModel(
            environment: AppEnvironment(
                settings: AppSettings(defaults: defaults),
                audioRouteMonitor: routeMonitor,
                transport: MockDeviceTransport(),
                locationProvider: location,
                restaurantSearch: search,
                navigationLauncher: FixtureNavigationService(),
                speechOutput: FixtureSpeechOutput()
            )
        )
    }
}

@MainActor
private final class StubMapResolver: AppleMapItemResolving {
    let item: MKMapItem?

    init(item: MKMapItem?) {
        self.item = item
    }

    func resolve(_ restaurant: Restaurant, near origin: GeoCoordinate?) async -> MKMapItem? {
        item
    }
}

@MainActor
private final class StubMapOpener: ExternalMapOpening {
    let appleResult: Bool
    let canOpenAMap: Bool
    var openedURLs: [URL] = []

    init(appleResult: Bool, canOpenAMap: Bool) {
        self.appleResult = appleResult
        self.canOpenAMap = canOpenAMap
    }

    func openAppleMaps(_ item: MKMapItem) -> Bool { appleResult }

    func canOpen(_ url: URL) -> Bool {
        url.scheme == "iosamap" && canOpenAMap
    }

    func open(_ url: URL) async -> Bool {
        openedURLs.append(url)
        return true
    }
}

@MainActor
private final class FailingLocationProvider: LocationProviding {
    var authorizationLabel: String { "已拒绝" }
    let error: Error

    init(error: Error) {
        self.error = error
    }

    func currentCoordinate() async throws -> GeoCoordinate {
        throw error
    }
}

@MainActor
private final class EmptyRestaurantSearch: RestaurantSearching {
    func search(
        near coordinate: GeoCoordinate,
        criteria: RestaurantSearchCriteria
    ) async throws -> [Restaurant] {
        []
    }

    func withdrawPrivacyConsent() {}
}
