import SwiftUI

@main
struct IonClawApp: App {
    @StateObject private var config = AppConfig()
    @StateObject private var server = ServerController()

    var body: some Scene {
        WindowGroup {
            ServerView()
                .environmentObject(config)
                .environmentObject(server)
                .tint(Theme.primary)
        }
    }
}
