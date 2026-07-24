@preconcurrency import AVFoundation
import Observation

enum SpeechRoutePolicy {
    static func canStart(_ policy: AudioPolicy, hasPrivateRoute: Bool) -> Bool {
        policy == .allowPhoneSpeaker || hasPrivateRoute
    }

    static func shouldStop(_ policy: AudioPolicy?, hasPrivateRoute: Bool) -> Bool {
        policy == .privateOnly && !hasPrivateRoute
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
        let privatePorts: Set<AVAudioSession.Port> = [
            .headphones,
            .bluetoothA2DP,
            .bluetoothHFP,
            .bluetoothLE,
            .usbAudio
        ]
        isPrivateRouteAvailable = outputs.contains { privatePorts.contains($0.portType) }
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

    init(routeMonitor: AudioRouteMonitor) {
        self.routeMonitor = routeMonitor
        super.init()
        synthesizer.delegate = self
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
                synthesizer.speak(utterance)
            }
        } catch {
            return .failed(error.localizedDescription)
        }
    }

    func stop() {
        stopCurrent(with: .failed("播报已取消"))
    }

    @objc private func routeDidChange() {
        guard continuation != nil,
              SpeechRoutePolicy.shouldStop(
                activePolicy,
                hasPrivateRoute: routeMonitor.isPrivateRouteAvailable
              ) else { return }
        cancellationOutcome = .routeLost
        synthesizer.stopSpeaking(at: .immediate)
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
