@preconcurrency import AVFoundation
import Observation

enum SpeechRoutePolicy {
    private static let privatePorts: Set<AVAudioSession.Port> = [
        .headphones,
        .bluetoothA2DP,
        .bluetoothHFP,
        .bluetoothLE,
        .usbAudio
    ]

    static func hasPrivateRoute(portTypes: [AVAudioSession.Port]) -> Bool {
        portTypes.contains { privatePorts.contains($0) }
    }

    static func canStart(_ policy: AudioPolicy, hasPrivateRoute: Bool) -> Bool {
        policy == .allowPhoneSpeaker || hasPrivateRoute
    }

    static func shouldStop(_ policy: AudioPolicy?, hasPrivateRoute: Bool) -> Bool {
        policy == .privateOnly && !hasPrivateRoute
    }

    static func shouldStopAfterRouteChange(
        policy: AudioPolicy?,
        reason: AVAudioSession.RouteChangeReason?,
        previousPortTypes: [AVAudioSession.Port],
        currentPortTypes: [AVAudioSession.Port]
    ) -> Bool {
        guard policy == .privateOnly else { return false }
        if reason == .oldDeviceUnavailable,
           hasPrivateRoute(portTypes: previousPortTypes) {
            return true
        }
        return !hasPrivateRoute(portTypes: currentPortTypes)
    }

    static func isRouteDisconnectInterruption(
        type: AVAudioSession.InterruptionType?,
        reason: AVAudioSession.InterruptionReason?
    ) -> Bool {
        type == .began && reason == .routeDisconnected
    }
}

@Observable
@MainActor
final class AudioRouteMonitor: NSObject {
    private(set) var isPrivateRouteAvailable = false
    private(set) var routeName = "手机扬声器"

    override init() {
        super.init()
        refresh()
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(routeDidChange),
            name: AVAudioSession.routeChangeNotification,
            object: nil
        )
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    @objc private func routeDidChange() {
        refresh()
    }

    private func refresh() {
        let outputs = AVAudioSession.sharedInstance().currentRoute.outputs
        isPrivateRouteAvailable = SpeechRoutePolicy.hasPrivateRoute(
            portTypes: outputs.map(\.portType)
        )
        routeName = outputs.first?.portName ?? "无可用音频输出"
    }
}

@MainActor
final class SystemSpeechOutput: NSObject, SpeechOutput, AVSpeechSynthesizerDelegate {
    private let synthesizer = AVSpeechSynthesizer()
    private let routeMonitor: AudioRouteMonitor
    private var continuation: CheckedContinuation<SpeechOutcome, Never>?
    private var activePolicy: AudioPolicy?
    private var cancellationOutcome: SpeechOutcome?
    private var routeWatchTask: Task<Void, Never>?

    init(routeMonitor: AudioRouteMonitor) {
        self.routeMonitor = routeMonitor
        super.init()
        synthesizer.delegate = self
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(routeDidChange(_:)),
            name: AVAudioSession.routeChangeNotification,
            object: nil
        )
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(audioSessionInterrupted(_:)),
            name: AVAudioSession.interruptionNotification,
            object: nil
        )
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    func speak(_ text: String, policy: AudioPolicy) async -> SpeechOutcome {
        guard SpeechRoutePolicy.canStart(
            policy,
            hasPrivateRoute: routeMonitor.isPrivateRouteAvailable
        ) else {
            return .privateRouteUnavailable
        }
        if continuation != nil {
            stopCurrent(with: .failed("上一条播报已被替换"))
        }

        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .spokenAudio, options: [.duckOthers])
            try session.setPrefersInterruptionOnRouteDisconnect(true)
            try session.setActive(true)
            if !SpeechRoutePolicy.canStart(
                policy,
                hasPrivateRoute: routeMonitor.isPrivateRouteAvailable
            ) {
                try? session.setActive(false, options: [.notifyOthersOnDeactivation])
                return .privateRouteUnavailable
            }

            let utterance = AVSpeechUtterance(string: text)
            utterance.voice = AVSpeechSynthesisVoice(language: "zh-CN")
            utterance.rate = 0.48
            activePolicy = policy

            return await withCheckedContinuation { continuation in
                self.continuation = continuation
                startRouteWatch()
                synthesizer.speak(utterance)
            }
        } catch {
            return .failed(error.localizedDescription)
        }
    }

    func stop() {
        stopCurrent(with: .failed("播报已取消"))
    }

    @objc private func routeDidChange(_ notification: Notification) {
        let reason = (notification.userInfo?[AVAudioSessionRouteChangeReasonKey] as? NSNumber)
            .flatMap { AVAudioSession.RouteChangeReason(rawValue: $0.uintValue) }
        let previousPortTypes = (
            notification.userInfo?[AVAudioSessionRouteChangePreviousRouteKey]
                as? AVAudioSessionRouteDescription
        )?.outputs.map(\.portType) ?? []
        let currentPortTypes = AVAudioSession.sharedInstance().currentRoute.outputs.map(\.portType)
        guard continuation != nil,
              SpeechRoutePolicy.shouldStopAfterRouteChange(
                policy: activePolicy,
                reason: reason,
                previousPortTypes: previousPortTypes,
                currentPortTypes: currentPortTypes
              ) else { return }
        stopCurrent(with: .routeLost)
    }

    @objc private func audioSessionInterrupted(_ notification: Notification) {
        let type = (notification.userInfo?[AVAudioSessionInterruptionTypeKey] as? NSNumber)
            .flatMap { AVAudioSession.InterruptionType(rawValue: $0.uintValue) }
        let reason = (notification.userInfo?[AVAudioSessionInterruptionReasonKey] as? NSNumber)
            .flatMap { AVAudioSession.InterruptionReason(rawValue: $0.uintValue) }
        guard continuation != nil, type == .began else { return }
        if SpeechRoutePolicy.isRouteDisconnectInterruption(type: type, reason: reason) {
            stopCurrent(with: .routeLost)
        } else {
            stopCurrent(with: .failed("音频会话被系统中断"))
        }
    }

    private func startRouteWatch() {
        routeWatchTask?.cancel()
        routeWatchTask = Task { @MainActor [weak self] in
            while !Task.isCancelled {
                do {
                    try await Task.sleep(for: .milliseconds(100))
                } catch {
                    return
                }
                guard let self, self.continuation != nil else { return }
                self.stopForLostPrivateRouteIfNeeded()
            }
        }
    }

    private func stopForLostPrivateRouteIfNeeded() {
        let hasPrivateRoute = SpeechRoutePolicy.hasPrivateRoute(
            portTypes: AVAudioSession.sharedInstance().currentRoute.outputs.map(\.portType)
        )
        guard continuation != nil,
              SpeechRoutePolicy.shouldStop(
                activePolicy,
                hasPrivateRoute: hasPrivateRoute
              ) else { return }
        stopCurrent(with: .routeLost)
    }

    private func stopCurrent(with outcome: SpeechOutcome) {
        guard continuation != nil else { return }
        cancellationOutcome = outcome
        if !synthesizer.stopSpeaking(at: .immediate) {
            finish(outcome)
        }
    }

    private func finish(_ outcome: SpeechOutcome) {
        let pending = continuation
        continuation = nil
        activePolicy = nil
        cancellationOutcome = nil
        routeWatchTask?.cancel()
        routeWatchTask = nil
        try? AVAudioSession.sharedInstance().setActive(
            false,
            options: [.notifyOthersOnDeactivation]
        )
        pending?.resume(returning: outcome)
    }

    nonisolated func speechSynthesizer(
        _ synthesizer: AVSpeechSynthesizer,
        didFinish utterance: AVSpeechUtterance
    ) {
        Task { @MainActor [weak self] in
            self?.finish(.spoken)
        }
    }

    nonisolated func speechSynthesizer(
        _ synthesizer: AVSpeechSynthesizer,
        didCancel utterance: AVSpeechUtterance
    ) {
        Task { @MainActor [weak self] in
            guard let self else { return }
            self.finish(self.cancellationOutcome ?? .routeLost)
        }
    }
}

@MainActor
final class FixtureSpeechOutput: SpeechOutput {
    private(set) var spokenTexts: [String] = []

    func speak(_ text: String, policy: AudioPolicy) async -> SpeechOutcome {
        spokenTexts.append(text)
        return .spoken
    }

    func stop() {}
}
