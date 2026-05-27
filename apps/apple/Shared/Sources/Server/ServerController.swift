import Foundation

@MainActor
final class ServerController: ObservableObject {
    @Published private(set) var isRunning = false
    @Published private(set) var isBusy = false
    @Published private(set) var port = 0
    @Published private(set) var addresses: [String] = []
    @Published var lastError: String?

    func start(host: String, port: Int) async {
        guard !isBusy, !isRunning else { return }

        isBusy = true
        lastError = nil

        do {
            self.port = try await launch(host: host, port: port)
            isRunning = true
            addresses = NetworkInterfaces.localIPv4Addresses()
        } catch {
            lastError = error.localizedDescription
        }

        isBusy = false
    }

    func stop() async {
        guard !isBusy, isRunning else { return }

        isBusy = true

        do {
            try await Task.detached(priority: .userInitiated) {
                try IonClawRuntime.stopServer()
            }.value

            isRunning = false
            port = 0
            addresses = []
        } catch {
            lastError = error.localizedDescription
        }

        isBusy = false
    }

    // initializes the project skeleton and boots the server off the main actor, returning the bound port
    private func launch(host: String, port: Int) async throws -> Int {
        let projectPath = Self.projectPath()

        return try await Task.detached(priority: .userInitiated) {
            try IonClawRuntime.initializeProject(at: projectPath)
            return try IonClawRuntime.startServer(projectPath: projectPath, host: host, port: port)
        }.value
    }

    // writable project location inside the app sandbox, created on demand by the native init call
    private static func projectPath() -> String {
        // tvos only permits writing to caches, application support is read-only there
        #if os(tvOS)
        let directory = FileManager.SearchPathDirectory.cachesDirectory
        #else
        let directory = FileManager.SearchPathDirectory.applicationSupportDirectory
        #endif

        let base = FileManager.default.urls(for: directory, in: .userDomainMask).first
            ?? FileManager.default.temporaryDirectory

        return base.appendingPathComponent("ionclaw/project", isDirectory: true).path
    }
}
