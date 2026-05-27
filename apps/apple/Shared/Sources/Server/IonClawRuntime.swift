import CIonClaw
import Foundation

enum IonClawError: LocalizedError {
    case nativeFailure(String)

    var errorDescription: String? {
        switch self {
        case let .nativeFailure(message):
            return message
        }
    }
}

// thin swift wrapper over the native C ABI exposed by ionclaw.xcframework
enum IonClawRuntime {
    static func initializeProject(at path: String) throws {
        let payload = invoke { path.withCString { ionclaw_project_init($0) } }
        try ensureSuccess(payload, otherwise: "failed to initialize project")
    }

    // starts the server and returns the bound port
    static func startServer(projectPath: String, host: String, port: Int) throws -> Int {
        let payload = invoke {
            projectPath.withCString { projectPtr in
                host.withCString { hostPtr in
                    "".withCString { rootPtr in
                        "".withCString { webPtr in
                            ionclaw_server_start(projectPtr, hostPtr, Int32(port), rootPtr, webPtr)
                        }
                    }
                }
            }
        }

        try ensureSuccess(payload, otherwise: "failed to start server")

        return payload["port"] as? Int ?? port
    }

    static func stopServer() throws {
        let payload = invoke { ionclaw_server_stop() }
        try ensureSuccess(payload, otherwise: "failed to stop server")
    }

    // calls a native entry point, reads its owned json string, frees it, and decodes the object
    private static func invoke(_ body: () -> UnsafePointer<CChar>?) -> [String: Any] {
        guard let pointer = body() else { return [:] }

        defer { ionclaw_free(pointer) }

        guard
            let data = String(cString: pointer).data(using: .utf8),
            let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        else {
            return [:]
        }

        return object
    }

    private static func ensureSuccess(_ payload: [String: Any], otherwise message: String) throws {
        guard payload["success"] as? Bool == true else {
            throw IonClawError.nativeFailure(payload["error"] as? String ?? message)
        }
    }
}
