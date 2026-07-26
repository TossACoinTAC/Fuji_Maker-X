import SwiftUI

struct ContentView: View {
    @Bindable var model: FujiAppModel

    var body: some View {
        TabView(selection: $model.selectedTab) {
            FujiHomeView(model: model)
                .tabItem {
                    Label("Fuji", systemImage: "face.smiling")
                }
                .tag(AppTab.fuji)

            ActivityView(model: model)
                .tabItem {
                    Label("记录", systemImage: "clock.arrow.circlepath")
                }
                .tag(AppTab.activity)

            SettingsView(model: model)
                .tabItem {
                    Label("设置", systemImage: "gearshape")
                }
                .tag(AppTab.settings)
        }
        .tint(FujiPalette.coral)
        .alert(
            "操作未完成",
            isPresented: Binding(
                get: { model.presentedError != nil },
                set: { if !$0 { model.clearPresentedError() } }
            )
        ) {
            Button("知道了") { model.clearPresentedError() }
        } message: {
            Text(model.presentedError ?? "请稍后重试")
        }
    }
}

private struct FujiHomeView: View {
    @Bindable var model: FujiAppModel

    var body: some View {
        NavigationStack {
            ScrollView {
                LazyVStack(spacing: 18) {
                    FujiStatusBand(model: model)
                    flowContent
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 12)
            }
            .background(FujiPalette.canvas)
            .navigationTitle("Fuji")
            .navigationBarTitleDisplayMode(.inline)
            .accessibilityIdentifier("fuji.home")
        }
    }

    @ViewBuilder
    private var flowContent: some View {
        switch model.flowStage {
        case .idle:
            IdlePanel(model: model)
        case .criteria:
            CriteriaPanel(model: model)
        case .searching:
            SearchingPanel()
        case .results, .confirming:
            RecommendationsPanel(model: model)
        case .navigating:
            ProgressView("正在打开步行路线")
                .frame(maxWidth: .infinity, minHeight: 180)
                .accessibilityIdentifier("navigation.progress")
        case .completed:
            ResultPanel(
                symbol: "figure.walk.motion",
                title: "路线已准备好",
                detail: model.statusMessage,
                tint: FujiPalette.teal,
                action: model.restartFoodFlow
            )
            .accessibilityIdentifier("navigation.success")
        case .failed:
            ResultPanel(
                symbol: "exclamationmark.triangle.fill",
                title: "这次没有完成",
                detail: model.statusMessage,
                tint: FujiPalette.coral,
                action: model.restartFoodFlow
            )
            .accessibilityIdentifier("navigation.failure")
        }
    }
}

private struct FujiStatusBand: View {
    let model: FujiAppModel

    var body: some View {
        HStack(spacing: 14) {
            ZStack {
                RoundedRectangle(cornerRadius: 8)
                    .fill(FujiPalette.sun)
                    .frame(width: 58, height: 58)
                Image(systemName: stateSymbol)
                    .font(.system(size: 27, weight: .semibold))
                    .foregroundStyle(FujiPalette.ink)
            }
            .accessibilityHidden(true)

            VStack(alignment: .leading, spacing: 5) {
                HStack(spacing: 6) {
                    Circle()
                        .fill(model.connectionState == .connected ? FujiPalette.teal : Color.secondary)
                        .frame(width: 8, height: 8)
                    Text(model.connectionState.label)
                        .font(.caption.weight(.semibold))
                }
                Text(model.statusMessage)
                    .font(.headline)
                    .foregroundStyle(FujiPalette.ink)
                    .fixedSize(horizontal: false, vertical: true)
                Text(routeLabel)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Spacer(minLength: 0)
        }
        .padding(14)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(.background)
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .overlay {
            RoundedRectangle(cornerRadius: 8)
                .stroke(FujiPalette.separator.opacity(0.35), lineWidth: 1)
                .allowsHitTesting(false)
        }
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("fuji.status")
    }

    private var stateSymbol: String {
        switch model.deviceState {
        case .disconnected: "wifi.slash"
        case .idle: "face.smiling"
        case .listening: "waveform"
        case .clarifying: "questionmark.bubble"
        case .thinking: "ellipsis.bubble"
        case .confirming: "checkmark.bubble"
        case .acting: "figure.walk"
        case .success: "checkmark.circle"
        case .error: "exclamationmark"
        case .quiet: "moon.zzz"
        case .muted: "speaker.slash"
        }
    }

    private var routeLabel: String {
        model.audioRouteMonitor.isPrivateRouteAvailable
            ? "语音将通过 \(model.audioRouteMonitor.routeName) 播放"
            : "未检测到耳机，默认不播报"
    }
}

private struct IdlePanel: View {
    let model: FujiAppModel

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "fork.knife.circle")
                .font(.system(size: 48, weight: .regular))
                .foregroundStyle(FujiPalette.teal)
                .accessibilityHidden(true)
            Text("今天吃什么？")
                .font(.title2.weight(.bold))
            Text("Fuji 发来请求后，这里会根据位置和预算给出最多三个选择。")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)

#if DEBUG
            Button {
                model.simulateFoodRequest()
            } label: {
                Label("模拟 Fuji 发起请求", systemImage: "wave.3.right")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .tint(FujiPalette.coral)
            .controlSize(.large)
            .accessibilityIdentifier("debug.simulateFoodRequest")
#endif
        }
        .frame(maxWidth: .infinity, minHeight: 260)
        .padding(.horizontal, 20)
    }
}

private struct CriteriaPanel: View {
    @Bindable var model: FujiAppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            VStack(alignment: .leading, spacing: 5) {
                Text("这次想怎么吃")
                    .font(.title3.weight(.bold))
                Text("定位只会在开始搜索后申请，也不会保存原始位置。")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }

            VStack(spacing: 0) {
                Stepper(value: $model.criteria.radiusMeters, in: 500...5_000, step: 500) {
                    LabeledContent("附近范围", value: "\(model.criteria.radiusMeters) 米")
                }
                .padding(.vertical, 13)
                Divider()
                Stepper(value: $model.criteria.budgetRMB, in: 20...500, step: 10) {
                    LabeledContent("人均预算", value: "¥\(model.criteria.budgetRMB)")
                }
                .padding(.vertical, 13)
            }

            VStack(alignment: .leading, spacing: 7) {
                Text("饮食禁忌")
                    .font(.subheadline.weight(.semibold))
                TextField("例如：花生、甲壳类", text: $model.criteria.avoidText)
                    .textFieldStyle(.roundedBorder)
                    .accessibilityIdentifier("criteria.avoid")
                Text("餐馆资料通常不能验证全部禁忌，结果会明确提示需要自行确认。")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Button {
                Task { await model.searchRestaurants() }
            } label: {
                Label("查找附近餐馆", systemImage: "location.magnifyingglass")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .tint(FujiPalette.coral)
            .controlSize(.large)
            .accessibilityIdentifier("criteria.search")
        }
        .padding(16)
        .background(.background)
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .overlay {
            RoundedRectangle(cornerRadius: 8)
                .stroke(FujiPalette.separator.opacity(0.35), lineWidth: 1)
                .allowsHitTesting(false)
        }
    }
}

private struct SearchingPanel: View {
    var body: some View {
        VStack(spacing: 14) {
            ProgressView()
                .controlSize(.large)
            Text("正在整理附近的选择")
                .font(.headline)
            Text("会保留资料未知的餐馆，但不会把未知当作符合条件。")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity, minHeight: 220)
        .accessibilityIdentifier("search.progress")
    }
}

private struct RecommendationsPanel: View {
    let model: FujiAppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .firstTextBaseline) {
                Text("Fuji 的推荐")
                    .font(.title3.weight(.bold))
                Spacer()
                Text("\(model.recommendations.count) 个")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)
            }

            ForEach(Array(model.recommendations.enumerated()), id: \.element.id) { index, item in
                RestaurantRow(
                    rank: index + 1,
                    recommendation: item,
                    isPending: model.pendingRecommendation?.id == item.id,
                    select: { model.select(item) },
                    confirm: { Task { await model.confirmNavigation() } },
                    cancel: model.rejectNavigation
                )
            }
        }
    }
}

private struct RestaurantRow: View {
    let rank: Int
    let recommendation: RestaurantRecommendation
    let isPending: Bool
    let select: () -> Void
    let confirm: () -> Void
    let cancel: () -> Void

    private var restaurant: Restaurant { recommendation.restaurant }

    var body: some View {
        VStack(alignment: .leading, spacing: 11) {
            HStack(alignment: .top, spacing: 12) {
                Text("\(rank)")
                    .font(.headline.monospacedDigit())
                    .foregroundStyle(FujiPalette.ink)
                    .frame(width: 30, height: 30)
                    .background(FujiPalette.sun)
                    .clipShape(Circle())

                VStack(alignment: .leading, spacing: 4) {
                    Text(restaurant.name)
                        .font(.headline)
                        .foregroundStyle(FujiPalette.ink)
                    Text(recommendation.reason)
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer(minLength: 0)
            }

            HStack(spacing: 14) {
                MetadataLabel(
                    symbol: "figure.walk",
                    text: restaurant.distanceMeters.map { "\(Int($0)) 米" } ?? "距离未知"
                )
                MetadataLabel(
                    symbol: "banknote",
                    text: restaurant.averageCostRMB.map { "人均 ¥\(Int($0))" } ?? "人均未知"
                )
            }

            if let address = restaurant.address, !address.isEmpty {
                MetadataLabel(symbol: "mappin", text: address)
            }

            if recommendation.dietaryNeedsConfirmation {
                Label("饮食禁忌需向餐馆自行确认", systemImage: "exclamationmark.shield")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(FujiPalette.warning)
            }

            if isPending {
                Divider()
                Text("确认后将离开本 App 并打开步行路线。")
                    .font(.footnote.weight(.semibold))
                HStack {
                    Button("换一家", action: cancel)
                        .buttonStyle(.bordered)
                        .accessibilityIdentifier("navigation.cancel")
                    Button(action: confirm) {
                        Label("确认并导航", systemImage: "figure.walk")
                    }
                    .buttonStyle(.borderedProminent)
                    .tint(FujiPalette.teal)
                    .accessibilityIdentifier("navigation.confirm")
                }
            } else {
                Button(action: select) {
                    Text("选择这家")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .tint(FujiPalette.teal)
                .accessibilityIdentifier("restaurant.select.\(rank - 1)")
            }
        }
        .padding(14)
        .background(.background)
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .overlay {
            RoundedRectangle(cornerRadius: 8)
                .stroke(
                    isPending ? FujiPalette.teal : FujiPalette.separator.opacity(0.35),
                    lineWidth: isPending ? 2 : 1
                )
                .allowsHitTesting(false)
        }
    }
}

private struct MetadataLabel: View {
    let symbol: String
    let text: String

    var body: some View {
        Label(text, systemImage: symbol)
            .font(.caption)
            .foregroundStyle(.secondary)
            .lineLimit(2)
    }
}

private struct ResultPanel: View {
    let symbol: String
    let title: String
    let detail: String
    let tint: Color
    let action: () -> Void

    var body: some View {
        VStack(spacing: 14) {
            Image(systemName: symbol)
                .font(.system(size: 46))
                .foregroundStyle(tint)
            Text(title)
                .font(.title3.weight(.bold))
            Text(detail)
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
            Button("再选一次", action: action)
                .buttonStyle(.borderedProminent)
                .tint(tint)
        }
        .frame(maxWidth: .infinity, minHeight: 240)
        .padding(.horizontal, 20)
    }
}

private struct ActivityView: View {
    let model: FujiAppModel

    var body: some View {
        NavigationStack {
            Group {
                if model.activity.isEmpty {
                    ContentUnavailableView(
                        "暂无会话记录",
                        systemImage: "clock",
                        description: Text("Fuji 的请求和手机执行结果会显示在这里。")
                    )
                } else {
                    List(model.activity) { event in
                        HStack(alignment: .top, spacing: 12) {
                            Image(systemName: event.kind.symbol)
                                .foregroundStyle(event.kind == .error ? FujiPalette.coral : FujiPalette.teal)
                                .frame(width: 24)
                            VStack(alignment: .leading, spacing: 4) {
                                HStack(alignment: .firstTextBaseline) {
                                    Text(event.title)
                                        .font(.body.weight(.semibold))
                                    Spacer()
                                    Text(event.timestamp, format: .dateTime.hour().minute().second())
                                        .font(.caption.monospacedDigit())
                                        .foregroundStyle(.secondary)
                                }
                                Text(event.detail)
                                    .font(.subheadline)
                                    .foregroundStyle(.secondary)
                                    .fixedSize(horizontal: false, vertical: true)
                            }
                        }
                        .padding(.vertical, 4)
                    }
                    .listStyle(.plain)
                }
            }
            .navigationTitle("当前会话")
            .accessibilityIdentifier("activity.list")
        }
    }
}

private struct SettingsView: View {
    @Bindable var model: FujiAppModel
    @State private var showResetConfirmation = false

    var body: some View {
        NavigationStack {
            Form {
                Section("连接") {
                    LabeledContent("Fuji", value: model.connectionState.label)
                    LabeledContent("设备协议", value: "加密 BLE · v1")
                }

#if DEBUG
                Section("开发测试") {
                    Button {
                        Task { await model.runBLETransportTest() }
                    } label: {
                        Label("运行 BLE 链路测试", systemImage: "antenna.radiowaves.left.and.right")
                    }
                    .disabled(
                        model.connectionState != .connected || model.isBLETransportTestRunning
                    )
                    .accessibilityIdentifier("debug.runBLETransportTest")

                    if model.isBLETransportTestRunning {
                        ProgressView()
                            .frame(maxWidth: .infinity, alignment: .center)
                            .accessibilityLabel("BLE 链路测试进行中")
                    } else if let result = model.bleTransportTestResult {
                        Label(
                            result.message,
                            systemImage: result.succeeded
                                ? "checkmark.circle.fill"
                                : "xmark.octagon.fill"
                        )
                        .font(.footnote)
                        .foregroundStyle(result.succeeded ? Color.green : Color.red)
                        .accessibilityIdentifier("debug.bleTransportTestResult")
                    }
                }
#endif

                Section("音频") {
                    LabeledContent(
                        "当前路由",
                        value: model.audioRouteMonitor.isPrivateRouteAvailable
                            ? model.audioRouteMonitor.routeName
                            : "未检测到耳机"
                    )
                    Picker("播报方式", selection: $model.settings.audioPolicy) {
                        ForEach(AudioPolicy.allCases, id: \.self) { policy in
                            Text(policy.label).tag(policy)
                        }
                    }
                    Toggle("安静模式", isOn: $model.settings.quietMode)
                    Text("默认只通过已确认的耳机播报；耳机断开会立即停止，不会转到手机扬声器。")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }

                Section("位置") {
                    LabeledContent("系统权限", value: model.locationAuthorizationLabel)
                    Text("只在你发起餐馆搜索时请求“使用 App 时”定位，不保存原始坐标。")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }

                Section("高德搜索隐私") {
                    Text("附近餐馆搜索会把本次位置和检索条件交给高德位置服务处理。请阅读高德隐私说明后再启用。")
                        .font(.footnote)
                    Link(destination: URL(string: "https://lbs.amap.com/pages/privacy/")!) {
                        Label("查看高德隐私说明", systemImage: "arrow.up.right.square")
                    }
                    if model.settings.amapPrivacyAccepted {
                        Button("撤回同意", role: .destructive) {
                            model.withdrawAMapPrivacy()
                        }
                        .accessibilityIdentifier("privacy.withdraw")
                    } else {
                        Button("同意并启用附近搜索") {
                            model.acceptAMapPrivacy()
                        }
                        .accessibilityIdentifier("privacy.accept")
                    }
                }

                Section("本机数据") {
                    Button("清除本次会话与设置", role: .destructive) {
                        showResetConfirmation = true
                    }
                    .accessibilityIdentifier("privacy.clearData")
                }
            }
            .navigationTitle("设置")
            .confirmationDialog(
                "清除本机数据？",
                isPresented: $showResetConfirmation,
                titleVisibility: .visible
            ) {
                Button("清除", role: .destructive) {
                    model.deleteSessionData()
                    model.settings.resetLocalPreferences()
                }
                Button("取消", role: .cancel) {}
            } message: {
                Text("将删除当前会话、推荐结果和本机偏好，不会影响系统定位权限。")
            }
        }
    }
}

private enum FujiPalette {
    static let coral = Color(red: 0.84, green: 0.25, blue: 0.25)
    static let teal = Color(red: 0.04, green: 0.45, blue: 0.43)
    static let sun = Color(red: 0.98, green: 0.80, blue: 0.29)
    static let warning = Color(red: 0.72, green: 0.35, blue: 0.02)
    static let ink = Color(red: 0.12, green: 0.14, blue: 0.16)
    static let canvas = Color(uiColor: .secondarySystemBackground)
    static let separator = Color(uiColor: .separator)
}
