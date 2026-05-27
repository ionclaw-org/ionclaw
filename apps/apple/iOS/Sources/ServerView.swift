import SwiftUI
import UIKit

struct ServerView: View {
    private enum Field {
        case host
        case port
    }

    @EnvironmentObject private var config: AppConfig
    @EnvironmentObject private var server: ServerController

    @State private var showPanel = false
    @State private var copiedAddress: String?
    @FocusState private var focusedField: Field?

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    VStack(spacing: 20) {
                        Image("Logo")
                            .resizable()
                            .scaledToFit()
                            .frame(maxWidth: 200, maxHeight: 60)

                        ServerStatusView(isRunning: server.isRunning)
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 12)
                    .contentShape(Rectangle())
                    .onTapGesture { focusedField = nil }
                    .listRowBackground(Color.clear)
                }

                Section("Server") {
                    LabeledContent("Host") {
                        TextField("0.0.0.0", text: $config.host)
                            .multilineTextAlignment(.trailing)
                            .autocorrectionDisabled()
                            .textInputAutocapitalization(.never)
                            .focused($focusedField, equals: .host)
                    }

                    LabeledContent("Port") {
                        TextField("8080", text: portText)
                            .multilineTextAlignment(.trailing)
                            .keyboardType(.numberPad)
                            .focused($focusedField, equals: .port)
                    }
                }
                .disabled(server.isRunning || server.isBusy)

                Section {
                    primaryButton
                        .alignmentGuide(.listRowSeparatorLeading) { _ in 0 }

                    if server.isRunning {
                        Button {
                            showPanel = true
                        } label: {
                            Label("Open Panel", systemImage: "safari")
                        }
                    }
                }

                if server.isRunning, !server.addresses.isEmpty {
                    Section("Network") {
                        ForEach(server.addresses, id: \.self, content: networkRow)
                    }
                }

                if let error = server.lastError {
                    Section {
                        Text(error)
                            .font(.footnote)
                            .foregroundStyle(Theme.danger)
                    }
                }
            }
            .navigationTitle("IonClaw")
            .toolbar(.hidden, for: .navigationBar)
            .scrollDismissesKeyboard(.interactively)
            .navigationDestination(isPresented: $showPanel) {
                PanelView(url: panelURL)
            }
        }
        .tint(Theme.primary)
    }

    private var primaryButton: some View {
        Button {
            focusedField = nil
            toggleServer()
        } label: {
            HStack {
                Spacer()

                if server.isBusy {
                    ProgressView()
                } else {
                    Label(server.isRunning ? "Stop Server" : "Start Server",
                          systemImage: server.isRunning ? "stop.fill" : "play.fill")
                        .fontWeight(.semibold)
                }

                Spacer()
            }
        }
        .disabled(server.isBusy)
        .foregroundStyle(server.isRunning ? Theme.danger : Theme.primary)
    }

    private func networkRow(_ address: String) -> some View {
        let url = "http://\(address):\(server.port)"

        return Button {
            UIPasteboard.general.string = url
            copiedAddress = address
        } label: {
            HStack {
                Text(url)
                    .font(.callout.monospaced())
                    .foregroundStyle(.primary)

                Spacer()

                Image(systemName: copiedAddress == address ? "checkmark" : "doc.on.doc")
                    .foregroundStyle(copiedAddress == address ? Theme.success : Theme.primary)
            }
        }
    }

    private func toggleServer() {
        Task {
            if server.isRunning {
                await server.stop()
            } else {
                await server.start(host: config.host, port: config.port)
            }
        }
    }

    private var panelURL: URL? {
        URL(string: "http://127.0.0.1:\(server.port)/app/")
    }

    private var portText: Binding<String> {
        Binding(
            get: { String(config.port) },
            set: { config.port = Int($0.filter(\.isNumber)) ?? config.port }
        )
    }
}
