import SwiftUI

struct InteractView: View {
    @EnvironmentObject private var config: AppConfig
    @StateObject private var sender = MessageSender()

    var body: some View {
        List {
            Section {
                // the watch input controller offers dictation and returns the transcribed text
                TextFieldLink(prompt: Text("Message")) {
                    Label("Speak", systemImage: "mic.fill")
                } onSubmit: { text in
                    Task { await sender.send(text, using: config) }
                }
            } footer: {
                Text("Dictate a message and send it to the server.")
            }

            statusSection
        }
        .scrollContentBackground(.hidden)
        .background(Color(hex: 0x1A1A2E).ignoresSafeArea())
        .navigationTitle("Talk")
    }

    @ViewBuilder
    private var statusSection: some View {
        switch sender.status {
        case .sending:
            Section { ProgressView() }
        case .sent:
            Section {
                Label("Sent", systemImage: "checkmark.circle.fill")
                    .foregroundStyle(Theme.success)
            }
        case let .failed(reason):
            Section {
                Text(reason)
                    .font(.footnote)
                    .foregroundStyle(Theme.danger)
            }
        case .idle:
            EmptyView()
        }
    }
}
