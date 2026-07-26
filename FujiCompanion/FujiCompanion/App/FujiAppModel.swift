import Foundation
import Observation

enum AppTab: Hashable {
    case fuji
    case activity
    case settings
}

enum FoodFlowStage: Equatable {
    case idle
    case criteria
    case searching
    case results
    case confirming
    case navigating
    case completed
    case failed
}

enum SessionEventKind: Equatable {
    case device
    case search
    case action
    case error

    var symbol: String {
        switch self {
        case .device: "dot.radiowaves.left.and.right"
        case .search: "fork.knife"
        case .action: "arrow.triangle.turn.up.right.diamond"
        case .error: "exclamationmark.triangle"
        }
    }
}

struct SessionEvent: Identifiable {
    let id = UUID()
    let timestamp: Date
    let title: String
    let detail: String
    let kind: SessionEventKind
}

@Observable
@MainActor
final class FujiAppModel {
    var settings: AppSettings
    let audioRouteMonitor: AudioRouteMonitor

    var selectedTab: AppTab = .fuji
    private(set) var connectionState: DeviceConnectionState = .disconnected
    private(set) var deviceState: FujiState = .disconnected
    private(set) var flowStage: FoodFlowStage = .idle
    var criteria = RestaurantSearchCriteria()
    private(set) var recommendations: [RestaurantRecommendation] = []
    private(set) var pendingRecommendation: RestaurantRecommendation?
    private(set) var activity: [SessionEvent] = []
    private(set) var statusMessage = "等待 Fuji 的请求"
    private(set) var currentOrigin: GeoCoordinate?
    private(set) var activeRequestID: UUID?
    var presentedError: String?

    private let transport: DeviceTransport
    private let locationProvider: LocationProviding
    private let restaurantSearch: RestaurantSearching
    private let navigationLauncher: NavigationLaunching
    private let speechOutput: SpeechOutput
    private let validator = FujiMessageValidator()
    private var messageTask: Task<Void, Never>?
    private var started = false

    init(environment: AppEnvironment) {
        settings = environment.settings
        audioRouteMonitor = environment.audioRouteMonitor
        transport = environment.transport
        locationProvider = environment.locationProvider
        restaurantSearch = environment.restaurantSearch
        navigationLauncher = environment.navigationLauncher
        speechOutput = environment.speechOutput
    }

    var locationAuthorizationLabel: String {
        locationProvider.authorizationLabel
    }

    func start() {
        guard !started else { return }
        started = true
        transport.connect()
        connectionState = transport.connectionState
        deviceState = connectionState == .connected ? .idle : .disconnected
        record("演示设备已连接", detail: "业务层通过 DeviceTransport 接收请求", kind: .device)

        messageTask = Task { [weak self, events = transport.events] in
            for await event in events {
                guard let self else { return }
                self.handle(event)
            }
        }
    }

    func simulateFoodRequest() {
        guard let mock = transport as? MockDeviceTransport else { return }
        mock.simulateFoodRequest()
    }

    func searchRestaurants() async {
        guard settings.amapPrivacyAccepted else {
            presentedError = RestaurantSearchError.privacyConsentRequired.localizedDescription
            selectedTab = .settings
            record("搜索未开始", detail: "等待高德搜索隐私授权", kind: .error)
            return
        }

        flowStage = .searching
        deviceState = .thinking
        statusMessage = "正在查找附近餐馆"
        record(
            "开始附近搜索",
            detail: "范围 \(criteria.radiusMeters) 米，预算 ¥\(criteria.budgetRMB)",
            kind: .search
        )

        do {
            let coordinate = try await locationProvider.currentCoordinate()
            currentOrigin = coordinate
            let restaurants = try await restaurantSearch.search(near: coordinate, criteria: criteria)
            let ranked = RestaurantRanker.rank(restaurants, for: criteria)
            guard !ranked.isEmpty else { throw RestaurantSearchError.noResults }

            recommendations = ranked
            flowStage = .results
            deviceState = .idle
            statusMessage = "找到 \(ranked.count) 个合适选项"
            record(
                "推荐已就绪",
                detail: ranked.map(\.restaurant.name).joined(separator: "、"),
                kind: .search
            )

            if !settings.quietMode {
                let summary = ranked.enumerated()
                    .map { "第 \($0.offset + 1) 个，\($0.element.restaurant.name)，\($0.element.reason)" }
                    .joined(separator: "。")
                let outcome = await speechOutput.speak(
                    "Fuji 找到了 \(ranked.count) 个选择。\(summary)",
                    policy: settings.audioPolicy
                )
                recordSpeechOutcome(outcome)
            }
        } catch {
            show(error)
        }
    }

    func select(_ recommendation: RestaurantRecommendation) {
        pendingRecommendation = recommendation
        flowStage = .confirming
        deviceState = .confirming
        statusMessage = "等待确认导航"
        record(
            "已选择 \(recommendation.restaurant.name)",
            detail: "尚未打开外部地图",
            kind: .action
        )
    }

    func rejectNavigation() {
        pendingRecommendation = nil
        flowStage = .results
        deviceState = .idle
        statusMessage = "可以选择其他餐馆"
        record("已取消导航", detail: "没有执行外部动作", kind: .action)
    }

    func confirmNavigation() async {
        guard let recommendation = pendingRecommendation else { return }
        flowStage = .navigating
        deviceState = .acting
        statusMessage = "正在打开步行路线"

        let outcome = await navigationLauncher.launchWalkingRoute(
            to: recommendation.restaurant,
            from: currentOrigin
        )
        record(
            outcome.succeeded ? "导航已启动" : "导航启动失败",
            detail: outcome.message,
            kind: outcome.succeeded ? .action : .error
        )

        if let requestID = activeRequestID {
            let response = FujiMessage.actionResult(
                requestID: requestID,
                result: FujiActionResultPayload(
                    action: .startNavigation,
                    status: outcome.succeeded ? .succeeded : .failed,
                    navigationState: outcome.succeeded ? .launched : nil,
                    errorCode: outcome.succeeded ? nil : .mapLaunchFailed,
                    message: outcome.message
                )
            )
            try? await transport.send(response)
        }

        pendingRecommendation = nil
        flowStage = outcome.succeeded ? .completed : .failed
        deviceState = outcome.succeeded ? .success : .error
        statusMessage = outcome.message

        if !settings.quietMode {
            let speech = await speechOutput.speak(
                outcome.succeeded ? "路线已发送。" : "路线没有打开，请查看手机。",
                policy: settings.audioPolicy
            )
            recordSpeechOutcome(speech)
        }
    }

    func restartFoodFlow() {
        recommendations = []
        pendingRecommendation = nil
        flowStage = .criteria
        deviceState = .clarifying
        statusMessage = "告诉 Fuji 这次的条件"
    }

    func acceptAMapPrivacy() {
        settings.amapPrivacyAccepted = true
        record("已启用高德搜索", detail: "仅在主动搜索时调用", kind: .action)
    }

    func withdrawAMapPrivacy() {
        settings.amapPrivacyAccepted = false
        restaurantSearch.withdrawPrivacyConsent()
        record("已停用高德搜索", detail: "后续搜索不会调用高德 SDK", kind: .action)
    }

    func deleteSessionData() {
        speechOutput.stop()
        activity.removeAll()
        recommendations.removeAll()
        pendingRecommendation = nil
        currentOrigin = nil
        activeRequestID = nil
        flowStage = .idle
        deviceState = connectionState == .connected ? .idle : .disconnected
        statusMessage = "本次会话数据已清除"
    }

    func clearPresentedError() {
        presentedError = nil
    }

    private func handle(_ event: DeviceTransportEvent) {
        switch event {
        case .connectionChanged(let state):
            connectionState = state
            if state == .disconnected {
                deviceState = .disconnected
                validator.reset()
                activeRequestID = nil
            } else if state == .connected, deviceState == .disconnected {
                deviceState = .idle
            }
        case .stateSnapshot(let snapshot):
            activeRequestID = snapshot.activeRequestID
            deviceState = appState(from: snapshot.deviceState)
        case .message(let message):
            handle(message)
        }
    }

    private func handle(_ message: FujiMessage) {
        do {
            _ = try validator.validate(message, receivedAtMS: monotonicMilliseconds())
            switch message.payload {
            case .actionRequest(.foodSearch(let payload)):
                activeRequestID = message.requestID
                if let radius = payload.radiusM { criteria.radiusMeters = radius }
                if let budget = payload.budgetRMB { criteria.budgetRMB = budget }
                if let avoid = payload.avoidTerms { criteria.avoidText = avoid.joined(separator: "、") }
                recommendations = []
                pendingRecommendation = nil
                flowStage = .criteria
                deviceState = .clarifying
                statusMessage = "告诉 Fuji 这次的条件"
                selectedTab = .fuji
                record("收到吃什么请求", detail: "请求已通过协议校验", kind: .device)
            case .cancel(let payload):
                if activeRequestID == payload.targetRequestID {
                    deleteSessionData()
                }
            default:
                record("收到设备消息", detail: message.type.rawValue, kind: .device)
            }
        } catch {
            show(error)
        }
    }

    private func appState(from state: FujiDeviceState) -> FujiState {
        switch state {
        case .idle: .idle
        case .listening: .listening
        case .thinking: .thinking
        case .speaking: .acting
        case .success: .success
        case .error: .error
        case .offline: .disconnected
        case .muted: .muted
        }
    }

    private func monotonicMilliseconds() -> UInt64 {
        DispatchTime.now().uptimeNanoseconds / 1_000_000
    }

    private func recordSpeechOutcome(_ outcome: SpeechOutcome) {
        switch outcome {
        case .spoken:
            record("私密播报完成", detail: audioRouteMonitor.routeName, kind: .action)
        case .privateRouteUnavailable:
            record("未进行语音播报", detail: "未确认耳机路由", kind: .device)
        case .routeLost:
            record("播报已停止", detail: "耳机路由已断开", kind: .error)
        case .failed(let reason):
            record("播报失败", detail: reason, kind: .error)
        }
    }

    private func show(_ error: Error) {
        let message = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
        presentedError = message
        flowStage = .failed
        deviceState = .error
        statusMessage = message
        record("操作失败", detail: message, kind: .error)
    }

    private func record(_ title: String, detail: String, kind: SessionEventKind) {
        activity.insert(
            SessionEvent(timestamp: .now, title: title, detail: detail, kind: kind),
            at: 0
        )
    }
}
