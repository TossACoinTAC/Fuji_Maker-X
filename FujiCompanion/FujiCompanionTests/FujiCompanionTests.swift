import MapKit
import AVFoundation
import Testing
@testable import FujiCompanion

@Suite("Fuji 协议")
@MainActor
struct FujiProtocolTests {
    @Test("JSON 往返保留版本化信封")
    func messageRoundTrip() throws {
        let original = FujiMessage.foodSearch(
            requestID: UUID(uuidString: "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE")!,
            criteria: .init(radiusM: 1_500, budgetRMB: 80, avoidTerms: ["花生"])
        )

        let data = try JSONEncoder().encode(original)
        let decoded = try JSONDecoder().decode(FujiMessage.self, from: data)

        #expect(decoded == original)
        #expect(decoded.version == FujiMessage.supportedVersion)
    }

    @Test("message_id 去重采用有界 TTL/LRU")
    func duplicateMessageAndEviction() throws {
        let validator = FujiMessageValidator(capacity: 2)
        let first = FujiMessage.foodSearch()

        _ = try validator.validate(first, receivedAtMS: 1_000)
        #expect(throws: FujiMessageValidationError.protocolError(.duplicate)) {
            try validator.validate(first, receivedAtMS: 1_001)
        }
        _ = try validator.validate(.foodSearch(), receivedAtMS: 1_002)
        _ = try validator.validate(.foodSearch(), receivedAtMS: 1_003)
        _ = try validator.validate(first, receivedAtMS: 1_004)
    }

    @Test("TTL 从接收单调时钟开始")
    func monotonicExpiry() throws {
        let message = FujiMessage.foodSearch(ttlMS: 5_000)
        let received = try FujiMessageValidator().validate(message, receivedAtMS: 10_000)

        #expect(!received.isExpired(at: 14_999))
        #expect(received.isExpired(at: 15_000))
    }

    @Test("拒绝不支持的协议版本")
    func unsupportedVersion() {
        let message = FujiMessage(
            version: 99,
            requestID: UUID(),
            direction: .deviceToPhone,
            type: .actionRequest,
            ttlMS: 30_000,
            payload: .actionRequest(.foodSearch(criteria: .init()))
        )

        #expect(throws: FujiMessageValidationError.protocolError(.unsupportedVersion)) {
            try FujiMessageValidator().validate(message, receivedAtMS: 0)
        }
    }

    @Test("Swift 校验全部共享 golden fixtures")
    func goldenFixtures() throws {
        let root = fixtureRoot
        let manifest = try JSONDecoder().decode(
            FixtureManifest.self,
            from: Data(contentsOf: root.appending(path: "manifest.json"))
        )
        let validator = FujiMessageValidator()
        var clock: UInt64 = 1_000

        for path in manifest.valid {
            let data = try Data(contentsOf: root.appending(path: path))
            _ = try validator.decode(data, receivedAtMS: clock)
            clock += 1
        }
        for fixture in manifest.invalid {
            let data = try Data(contentsOf: root.appending(path: fixture.path))
            #expect(throws: FujiMessageValidationError.protocolError(fixture.error)) {
                try validator.decode(data, receivedAtMS: clock)
            }
            clock += 1
        }
    }

    @Test("MTU 23、185、517 都可分片并乱序重组")
    func fragmentationAcrossMTUs() throws {
        let data = try JSONEncoder().encode(FujiMessage.foodSearch(criteria: .init(avoidTerms: [String(repeating: "x", count: 40)])))

        for mtu in [23, 185, 517] {
            let frames = try FujiFramer.frames(for: data, mtu: mtu, transferID: UInt32(mtu))
            var assembler = FujiFrameAssembler()
            var result: Data?
            let reordered = [frames[0]] + Array(frames.dropFirst().reversed())
            for frame in reordered {
                if let complete = try assembler.accept(frame, nowMS: 1_000) {
                    result = complete
                }
            }
            #expect(result == data)
        }
    }

    @Test("缺片在五秒后清理")
    func missingFragmentTimesOut() throws {
        let data = Data(repeating: 0x41, count: 400)
        let frames = try FujiFramer.frames(for: data, mtu: 185, transferID: 42)
        var assembler = FujiFrameAssembler()

        _ = try assembler.accept(frames[0], nowMS: 100)
        #expect(throws: FujiFrameError.timedOut) {
            try assembler.accept(frames[1], nowMS: 5_100)
        }
    }

    @Test("重复分片与并发 transfer 被拒绝")
    func duplicateAndCollidingFragments() throws {
        let data = Data(repeating: 0x42, count: 400)
        let first = try FujiFramer.frames(for: data, mtu: 185, transferID: 1)
        let second = try FujiFramer.frames(for: data, mtu: 185, transferID: 2)
        var assembler = FujiFrameAssembler()

        _ = try assembler.accept(first[0], nowMS: 100)
        #expect(throws: FujiFrameError.inconsistentTransfer) {
            try assembler.accept(first[0], nowMS: 101)
        }
        assembler.reset()
        _ = try assembler.accept(first[0], nowMS: 102)
        #expect(throws: FujiFrameError.transferInProgress) {
            try assembler.accept(second[0], nowMS: 103)
        }
    }

    @Test("已执行动作缓存有界且重复命中原结果")
    func actionResultCache() {
        let cache = FujiActionResultCache(capacity: 2)
        let requestID = UUID()
        let original = FujiActionResultPayload(
            action: .startNavigation,
            status: .succeeded,
            navigationState: .launched
        )

        cache.store(original, for: requestID, nowMS: 100)
        #expect(cache.result(for: requestID, nowMS: 101) == original)
        cache.store(.init(action: .foodSearch, status: .failed), for: UUID(), nowMS: 102)
        cache.store(.init(action: .foodSearch, status: .cancelled), for: UUID(), nowMS: 103)
        #expect(cache.result(for: requestID, nowMS: 104) == nil)
    }

    private var fixtureRoot: URL {
        URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .appending(path: "Shared/FujiProtocolV1/fixtures", directoryHint: .isDirectory)
    }
}

private struct FixtureManifest: Decodable {
    struct InvalidFixture: Decodable {
        let path: String
        let error: FujiProtocolErrorCode
    }

    let valid: [String]
    let invalid: [InvalidFixture]
}

@Suite("私密音频路由")
struct SpeechRoutePolicyTests {
    @Test("AirPods 音频端口被识别为私密路由")
    func recognizesBluetoothAudioPorts() {
        #expect(SpeechRoutePolicy.hasPrivateRoute(portTypes: [.bluetoothA2DP]))
        #expect(SpeechRoutePolicy.hasPrivateRoute(portTypes: [.bluetoothHFP]))
        #expect(SpeechRoutePolicy.hasPrivateRoute(portTypes: [.bluetoothLE]))
    }

    @Test("手机扬声器不被识别为私密路由")
    func rejectsPhoneSpeaker() {
        #expect(!SpeechRoutePolicy.hasPrivateRoute(portTypes: [.builtInSpeaker]))
        #expect(!SpeechRoutePolicy.hasPrivateRoute(portTypes: []))
    }

    @Test("旧 AirPods 不可用时即使当前路由尚未刷新也停止")
    func stopsOnUnavailablePreviousAirPodsRoute() {
        #expect(
            SpeechRoutePolicy.shouldStopAfterRouteChange(
                policy: .privateOnly,
                reason: .oldDeviceUnavailable,
                previousPortTypes: [.bluetoothA2DP],
                currentPortTypes: [.bluetoothA2DP]
            )
        )
        #expect(
            !SpeechRoutePolicy.shouldStopAfterRouteChange(
                policy: .allowPhoneSpeaker,
                reason: .oldDeviceUnavailable,
                previousPortTypes: [.bluetoothA2DP],
                currentPortTypes: [.builtInSpeaker]
            )
        )
    }

    @Test("系统路由断开中断只在 began 时识别")
    func recognizesRouteDisconnectInterruption() {
        #expect(
            SpeechRoutePolicy.isRouteDisconnectInterruption(
                type: .began,
                reason: .routeDisconnected
            )
        )
        #expect(
            !SpeechRoutePolicy.isRouteDisconnectInterruption(
                type: .ended,
                reason: .routeDisconnected
            )
        )
        #expect(
            !SpeechRoutePolicy.isRouteDisconnectInterruption(
                type: .began,
                reason: .default
            )
        )
    }
}

@Suite("设备连接记录")
@MainActor
struct DeviceConnectionHistoryTests {
    @Test("重连尝试不重复写入活动记录")
    func reconnectAttemptsDoNotSpamHistory() async {
        let transport = MockDeviceTransport()
        let suiteName = "FujiCompanionTests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suiteName)!
        defaults.removePersistentDomain(forName: suiteName)
        let model = FujiAppModel(
            environment: AppEnvironment(
                settings: AppSettings(defaults: defaults),
                audioRouteMonitor: AudioRouteMonitor(),
                transport: transport,
                locationProvider: FixtureLocationService(),
                restaurantSearch: FixtureRestaurantSearchService(),
                navigationLauncher: FixtureNavigationService(),
                speechOutput: FixtureSpeechOutput()
            )
        )

        model.start()
        await waitForHistory(count: 1, in: model)
        #expect(model.activity.map(\.title) == ["Fuji 已连接"])

        transport.simulateConnectionState(.connecting)
        transport.simulateConnectionState(.disconnected)
        transport.simulateConnectionState(.connecting)
        transport.simulateConnectionState(.disconnected)
        await waitForHistory(count: 2, in: model)
        #expect(model.activity.map(\.title) == ["Fuji 已断开", "Fuji 已连接"])

        transport.simulateConnectionState(.connecting)
        transport.simulateConnectionState(.connected)
        await waitForHistory(count: 3, in: model)
        #expect(model.activity.map(\.title) == ["Fuji 已连接", "Fuji 已断开", "Fuji 已连接"])
    }

    private func waitForHistory(count: Int, in model: FujiAppModel) async {
        for _ in 0..<20 where model.activity.count < count {
            try? await Task.sleep(for: .milliseconds(5))
        }
    }
}

#if DEBUG
@Suite("确定性 BLE 链路测试")
@MainActor
struct BLETransportDiagnosticTests {
    @Test("真实与 Mock transport 共用成功重复超时取消断线编排")
    func runsDeterministicTransportSequence() async {
        let transport = MockDeviceTransport()
        let suiteName = "FujiCompanionTests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suiteName)!
        defaults.removePersistentDomain(forName: suiteName)
        let model = FujiAppModel(
            environment: AppEnvironment(
                settings: AppSettings(defaults: defaults),
                audioRouteMonitor: AudioRouteMonitor(),
                transport: transport,
                locationProvider: FixtureLocationService(),
                restaurantSearch: FixtureRestaurantSearchService(),
                navigationLauncher: FixtureNavigationService(),
                speechOutput: FixtureSpeechOutput()
            )
        )

        model.start()
        await model.runBLETransportTest()

        #expect(model.statusMessage == "蓝牙链路测试通过")
        #expect(model.presentedError == nil)
        #expect(!model.isBLETransportTestRunning)
        #expect(model.bleTransportTestResult?.succeeded == true)
        #expect(model.bleTransportTestResult?.message.contains("断线恢复") == true)
        #expect(transport.sentMessages.count == 4)
        #expect(transport.sentMessages[0].messageID == transport.sentMessages[1].messageID)
        #expect(transport.sentMessages[2].type == .actionResult)
        #expect(transport.sentMessages[3].type == .cancel)
        let titles = Set(model.activity.map(\.title))
        for expected in [
            "BLE 成功路径通过",
            "BLE 重复路径通过",
            "BLE 超时路径通过",
            "BLE 取消路径通过",
            "BLE 链路测试通过"
        ] {
            #expect(titles.contains(expected))
        }
    }
}
#endif

@Suite("Fuji BLE 会话")
@MainActor
struct FujiBLESessionTests {
    @Test("MTU 23 分片重组并在重连后保持去重")
    func reassemblyAndReconnectDeduplication() throws {
        let session = FujiBLESession()
        let message = FujiMessage.foodSearch(
            criteria: .init(radiusM: 1_500, budgetRMB: 80, avoidTerms: ["花生"])
        )
        let json = try JSONEncoder().encode(message)
        let firstTransfer = try FujiFramer.frames(for: json, mtu: 23, transferID: 1)

        var received: FujiMessage?
        for frame in firstTransfer {
            received = try session.accept(frame, nowMS: 1_000) ?? received
        }
        #expect(received == message)

        session.resetConnection()
        let repeatedTransfer = try FujiFramer.frames(for: json, mtu: 23, transferID: 2)
        #expect(throws: FujiMessageValidationError.protocolError(.duplicate)) {
            for frame in repeatedTransfer {
                _ = try session.accept(frame, nowMS: 1_001)
            }
        }

        session.resetForRestart()
        var acceptedAfterRestart: FujiMessage?
        for frame in repeatedTransfer {
            acceptedAfterRestart = try session.accept(frame, nowMS: 1_002) ?? acceptedAfterRestart
        }
        #expect(acceptedAfterRestart == message)
    }

    @Test("消息 TTL 从首个分片接收时刻计算")
    func ttlStartsAtFirstFragment() throws {
        let session = FujiBLESession()
        let message = FujiMessage(
            direction: .deviceToPhone,
            type: .protocolError,
            ttlMS: 1,
            payload: .protocolError(.init(errorCode: .internalError, message: "test"))
        )
        let json = try JSONEncoder().encode(message)
        let frames = try FujiFramer.frames(for: json, mtu: 23, transferID: 3)

        _ = try session.accept(frames[0], nowMS: 10_000)
        #expect(throws: FujiMessageValidationError.protocolError(.expired)) {
            for frame in frames.dropFirst() {
                _ = try session.accept(frame, nowMS: 10_001)
            }
        }
    }

    @Test("手机消息按 CoreBluetooth 写入长度分片")
    func outboundUsesMaximumWriteLength() throws {
        let session = FujiBLESession()
        let requestID = UUID()
        let message = FujiMessage.actionResult(
            requestID: requestID,
            result: .init(
                action: .foodSearch,
                status: .succeeded,
                candidates: [.init(candidateID: "candidate-1", name: "测试餐馆", reason: "距离近")]
            )
        )

        let frames = try session.frames(
            for: message,
            maximumWriteValueLength: 20,
            transferID: 4
        )
        #expect(frames.count > 1)
        #expect(frames.allSatisfy { $0.count <= 20 })

        var assembler = FujiFrameAssembler()
        var result: Data?
        for frame in frames {
            result = try assembler.accept(frame, nowMS: 20_000) ?? result
        }
        #expect(try JSONDecoder().decode(FujiMessage.self, from: result!) == message)
    }

    @Test("拒绝错误方向的设备入站消息")
    func rejectsPhoneDirectionInbound() throws {
        let session = FujiBLESession()
        let message = FujiMessage.protocolError(.internalError, message: "wrong direction")
        let frames = try FujiFramer.frames(
            for: JSONEncoder().encode(message),
            mtu: 185,
            transferID: 5
        )

        #expect(throws: FujiMessageValidationError.protocolError(.invalidPayload)) {
            for frame in frames {
                _ = try session.accept(frame, nowMS: 30_000)
            }
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
