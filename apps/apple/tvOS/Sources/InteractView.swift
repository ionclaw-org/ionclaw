import SwiftUI

struct InteractView: View {
    @EnvironmentObject private var config: AppConfig
    @StateObject private var sender = MessageSender()

    @State private var message = ""
    @FocusState private var fieldFocused: Bool

    var body: some View {
        VStack(spacing: 36) {
            Image(systemName: "mic.fill")
                .font(.system(size: 76))
                .foregroundStyle(Theme.primary)
                .frame(width: 200, height: 200)
                .background(Theme.primary.opacity(0.12), in: Circle())

            Text("Select the field and dictate your message")
                .font(.title3)
                .foregroundStyle(.secondary)

            TextField("Dictate your message", text: $message)
                .font(.title3)
                .multilineTextAlignment(.center)
                .focused($fieldFocused)
                .frame(maxWidth: 760)
                .padding(.vertical, 4)

            Button {
                Task { await submit() }
            } label: {
                Label("Send", systemImage: "paperplane.fill")
                    .frame(maxWidth: 420)
            }
            .buttonStyle(FilledActionButtonStyle())
            .disabled(trimmed.isEmpty || sender.status == .sending)

            statusView
                .frame(height: 40)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(60)
        .background(Color(hex: 0x1A1A2E).ignoresSafeArea())
        .navigationTitle("Talk to IonClaw")
        .defaultFocus($fieldFocused, true)
    }

    @ViewBuilder
    private var statusView: some View {
        switch sender.status {
        case .sending:
            ProgressView()
        case .sent:
            Label("Sent", systemImage: "checkmark.circle.fill")
                .foregroundStyle(Theme.success)
        case let .failed(reason):
            Text(reason)
                .foregroundStyle(Theme.danger)
                .multilineTextAlignment(.center)
        case .idle:
            EmptyView()
        }
    }

    private var trimmed: String {
        message.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private func submit() async {
        await sender.send(message, using: config)

        if sender.status == .sent {
            message = ""
        }
    }
}
