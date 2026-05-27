import Foundation

// persisted server configuration shared across every screen
@MainActor
final class AppConfig: ObservableObject {
    @Published var host: String { didSet { defaults.set(host, forKey: Keys.host) } }
    @Published var port: Int { didSet { defaults.set(port, forKey: Keys.port) } }
    @Published var username: String { didSet { defaults.set(username, forKey: Keys.username) } }
    @Published var password: String { didSet { defaults.set(password, forKey: Keys.password) } }

    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        host = defaults.string(forKey: Keys.host) ?? Defaults.host
        port = defaults.object(forKey: Keys.port) as? Int ?? Defaults.port
        username = defaults.string(forKey: Keys.username) ?? Defaults.username
        password = defaults.string(forKey: Keys.password) ?? Defaults.password
    }

    // address used to reach the locally running server from the same device
    var serviceBaseURL: URL? {
        URL(string: "http://127.0.0.1:\(port)")
    }

    private enum Keys {
        static let host = "server_host"
        static let port = "server_port"
        static let username = "service_username"
        static let password = "service_password"
    }

    private enum Defaults {
        static let host = "0.0.0.0"
        static let port = 8080
        static let username = "admin"
        static let password = "admin"
    }
}
