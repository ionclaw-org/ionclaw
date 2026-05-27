import Foundation

// view-facing state machine that posts a transcribed message to the local server
@MainActor
final class MessageSender: ObservableObject {
    enum Status: Equatable {
        case idle
        case sending
        case sent
        case failed(String)
    }

    @Published private(set) var status: Status = .idle

    func send(_ message: String, using config: AppConfig) async {
        let trimmed = message.trimmingCharacters(in: .whitespacesAndNewlines)

        guard !trimmed.isEmpty else { return }
        guard let baseURL = config.serviceBaseURL else {
            status = .failed(ChatClientError.invalidBaseURL.localizedDescription)
            return
        }

        status = .sending

        let client = ChatClient(baseURL: baseURL, username: config.username, password: config.password)

        do {
            try await client.send(message: trimmed)
            status = .sent
        } catch {
            status = .failed(error.localizedDescription)
        }
    }

    func reset() {
        status = .idle
    }
}
