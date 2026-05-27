import CIonClaw
import Foundation
import UserNotifications

// bridges the native invoke_platform tool to apple platform features.
// the agent calls invoke_platform("local-notification.send", {title, message}) and the core
// forwards it here via the async c callback; we must respond exactly once per request.
final class IonClawPlatform: NSObject, UNUserNotificationCenterDelegate {
    static let shared = IonClawPlatform()

    private let platformName: String = {
        #if os(tvOS)
        return "tvos"
        #elseif os(watchOS)
        return "watchos"
        #else
        return "ios"
        #endif
    }()

    // registers the async platform handler with the native core. safe to call once at launch.
    func register() {
        UNUserNotificationCenter.current().delegate = self
        ionclaw_set_platform_handler(platformCallback, 30)
    }

    // dispatches a request coming from the native core. runs on a core thread and must not block;
    // the response is delivered later from the async completion handler.
    func handle(requestId: Int64, function: String, paramsJson: String) {
        switch function {
        case "local-notification.send":
            sendLocalNotification(requestId: requestId, paramsJson: paramsJson)
        default:
            respond(requestId, "Error: '\(function)' is not implemented on \(platformName).")
        }
    }

    private func sendLocalNotification(requestId: Int64, paramsJson: String) {
        #if os(tvOS)
        // tvos only supports app-icon badges, not alert notifications, so a message cannot be shown
        respond(requestId, "Error: local notifications are not supported on tvOS")
        #else
        let params = (try? JSONSerialization.jsonObject(with: Data(paramsJson.utf8))) as? [String: Any] ?? [:]
        let title = params["title"] as? String ?? "IonClaw"
        let body = params["message"] as? String ?? ""

        let center = UNUserNotificationCenter.current()

        // request authorization lazily so the prompt appears on first real use, not at launch
        center.requestAuthorization(options: [.alert, .sound, .badge]) { [weak self] granted, error in
            guard let self else { return }

            if let error {
                self.respond(requestId, "Error: \(error.localizedDescription)")
                return
            }

            guard granted else {
                self.respond(requestId, "Error: notifications not authorized by the user")
                return
            }

            let content = UNMutableNotificationContent()
            content.title = title
            content.body = body
            content.sound = .default

            let request = UNNotificationRequest(identifier: UUID().uuidString, content: content, trigger: nil)

            center.add(request) { addError in
                self.respond(requestId, addError.map { "Error: \($0.localizedDescription)" } ?? "OK")
            }
        }
        #endif
    }

    // presents notifications even while the app is in the foreground
    func userNotificationCenter(_ center: UNUserNotificationCenter, willPresent notification: UNNotification, withCompletionHandler completionHandler: @escaping (UNNotificationPresentationOptions) -> Void) {
        #if os(tvOS)
        completionHandler([.badge])
        #else
        completionHandler([.banner, .sound, .badge])
        #endif
    }

    private func respond(_ requestId: Int64, _ result: String) {
        result.withCString { ionclaw_platform_respond(requestId, $0) }
    }
}

// the c callback cannot capture context, so it forwards to the shared singleton
private let platformCallback: ionclaw_platform_callback_t = { requestId, functionName, paramsJson in
    let function = functionName.map { String(cString: $0) } ?? ""
    let params = paramsJson.map { String(cString: $0) } ?? ""
    IonClawPlatform.shared.handle(requestId: requestId, function: function, paramsJson: params)
}
