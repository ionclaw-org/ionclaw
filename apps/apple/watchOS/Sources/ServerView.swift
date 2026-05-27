import SwiftUI

struct ServerView: View {
    @EnvironmentObject private var config: AppConfig
    @EnvironmentObject private var server: ServerController

    var body: some View {
        NavigationStack {
            List {
                Section {
                    VStack(spacing: 10) {
                        Image("Logo")
                            .resizable()
                            .scaledToFit()
                            .frame(maxWidth: 120, maxHeight: 28)

                        ServerStatusView(isRunning: server.isRunning)
                    }
                    .frame(maxWidth: .infinity)
                    .listRowBackground(Color.clear)
                }

                if !server.isRunning {
                    Section("Server") {
                        TextField("Host", text: $config.host)
                        TextField("Port", text: portText)
                    }
                    .disabled(server.isBusy)
                }

                Section {
                    primaryButton

                    if server.isRunning {
                        NavigationLink {
                            InteractView()
                        } label: {
                            Label("Interact", systemImage: "mic.fill")
                        }
                    }
                }

                if server.isRunning, !server.addresses.isEmpty {
                    Section("Connect") {
                        QRCodeView(text: connectionURL)
                            .frame(width: 140, height: 140)
                            .padding(10)
                            .background(.white, in: RoundedRectangle(cornerRadius: 12))
                            .frame(maxWidth: .infinity)
                            .listRowBackground(Color.clear)

                        ForEach(server.addresses, id: \.self) { address in
                            Text(verbatim: "http://\(address):\(server.port)")
                                .font(.footnote.monospaced())
                                .foregroundStyle(.secondary)
                        }
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
            .scrollContentBackground(.hidden)
            .background(Color(hex: 0x1A1A2E).ignoresSafeArea())
        }
        .tint(Theme.primary)
    }

    private var primaryButton: some View {
        Button {
            Task {
                if server.isRunning {
                    await server.stop()
                } else {
                    await server.start(host: config.host, port: config.port)
                }
            }
        } label: {
            if server.isBusy {
                ProgressView()
            } else {
                Label(server.isRunning ? "Stop" : "Start",
                      systemImage: server.isRunning ? "stop.fill" : "play.fill")
            }
        }
        .disabled(server.isBusy)
        .foregroundStyle(server.isRunning ? Theme.danger : Theme.primary)
    }

    private var connectionURL: String {
        "http://\(server.addresses.first ?? "127.0.0.1"):\(server.port)"
    }

    private var portText: Binding<String> {
        Binding(
            get: { String(config.port) },
            set: { config.port = Int($0.filter(\.isNumber)) ?? config.port }
        )
    }
}
