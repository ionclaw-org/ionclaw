import Foundation

enum ChatClientError: LocalizedError {
    case invalidBaseURL
    case unauthorized
    case requestFailed(status: Int)
    case missingToken

    var errorDescription: String? {
        switch self {
        case .invalidBaseURL:
            return "Invalid server address"
        case .unauthorized:
            return "Authentication failed"
        case let .requestFailed(status):
            return "Request failed (\(status))"
        case .missingToken:
            return "Server did not return a token"
        }
    }
}

// minimal client for the local server: authenticates once, then posts chat messages
actor ChatClient {
    private let baseURL: URL
    private let username: String
    private let password: String
    private let session: URLSession
    private var token: String?

    init(baseURL: URL, username: String, password: String, session: URLSession = .shared) {
        self.baseURL = baseURL
        self.username = username
        self.password = password
        self.session = session
    }

    func send(message: String, language: String? = nil) async throws {
        let payload: [String: Any] = {
            var body: [String: Any] = ["message": message, "session_id": "direct"]

            if let language, !language.isEmpty {
                body["language"] = language
            }

            return body
        }()

        do {
            try await post(path: "/api/chat", body: payload, authorized: true)
        } catch ChatClientError.unauthorized {
            // a stale token is the one recoverable case: re-authenticate and retry once
            token = nil
            try await post(path: "/api/chat", body: payload, authorized: true)
        }
    }

    private func authenticatedToken() async throws -> String {
        if let token {
            return token
        }

        let data = try await post(
            path: "/api/auth/login",
            body: ["username": username, "password": password],
            authorized: false
        )

        guard
            let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
            let issued = json["token"] as? String, !issued.isEmpty
        else {
            throw ChatClientError.missingToken
        }

        token = issued
        return issued
    }

    @discardableResult
    private func post(path: String, body: [String: Any], authorized: Bool) async throws -> Data {
        guard let url = URL(string: path, relativeTo: baseURL) else {
            throw ChatClientError.invalidBaseURL
        }

        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try JSONSerialization.data(withJSONObject: body)

        if authorized {
            request.setValue("Bearer \(try await authenticatedToken())", forHTTPHeaderField: "Authorization")
        }

        let (data, response) = try await session.data(for: request)
        let status = (response as? HTTPURLResponse)?.statusCode ?? 0

        if status == 401 {
            throw ChatClientError.unauthorized
        }

        guard (200...299).contains(status) else {
            throw ChatClientError.requestFailed(status: status)
        }

        return data
    }
}
