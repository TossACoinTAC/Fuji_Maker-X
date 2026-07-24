import Foundation
import Observation

@Observable
@MainActor
final class AppSettings {
    @ObservationIgnored private let defaults: UserDefaults

    var amapPrivacyAccepted: Bool {
        didSet { defaults.set(amapPrivacyAccepted, forKey: Keys.amapPrivacyAccepted) }
    }

    var audioPolicy: AudioPolicy {
        didSet { defaults.set(audioPolicy.rawValue, forKey: Keys.audioPolicy) }
    }

    var quietMode: Bool {
        didSet { defaults.set(quietMode, forKey: Keys.quietMode) }
    }

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        amapPrivacyAccepted = defaults.bool(forKey: Keys.amapPrivacyAccepted)
        audioPolicy = defaults.string(forKey: Keys.audioPolicy)
            .flatMap(AudioPolicy.init(rawValue:)) ?? .privateOnly
        quietMode = defaults.bool(forKey: Keys.quietMode)
    }

    func resetLocalPreferences() {
        amapPrivacyAccepted = false
        audioPolicy = .privateOnly
        quietMode = false
    }

    private enum Keys {
        static let amapPrivacyAccepted = "settings.amapPrivacyAccepted"
        static let audioPolicy = "settings.audioPolicy"
        static let quietMode = "settings.quietMode"
    }
}
