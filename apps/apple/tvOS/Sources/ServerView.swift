import SwiftUI

struct ServerView: View {
    @EnvironmentObject private var config: AppConfig
    @EnvironmentObject private var server: ServerController

    @State private var showInteract = false

    var body: some View {
        NavigationStack {
            VStack(spacing: 48) {
                Image("Logo")
                    .resizable()
                    .scaledToFit()
                    .frame(maxWidth: 300, maxHeight: 80)

                HStack(alignment: .top, spacing: 48) {
                    controlCard
                    connectionCard
                }
                .frame(maxWidth: 1500)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .padding(60)
            .background(Color(hex: 0x1A1A2E).ignoresSafeArea())
            .navigationDestination(isPresented: $showInteract) {
                InteractView()
            }
        }
        .tint(Theme.primary)
    }

    private var controlCard: some View {
        VStack(spacing: 28) {
            ServerStatusView(isRunning: server.isRunning)

            VStack(spacing: 8) {
                fieldRow("Host", text: $config.host)
                Divider()
                fieldRow("Port", text: portText)
            }
            .disabled(server.isRunning || server.isBusy)

            VStack(spacing: 16) {
                primaryButton

                if server.isRunning {
                    Button {
                        showInteract = true
                    } label: {
                        Label("Interact", systemImage: "mic.fill")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(FilledActionButtonStyle(tint: Theme.primary))
                }
            }

            if let error = server.lastError {
                Text(error)
                    .font(.callout)
                    .foregroundStyle(Theme.danger)
                    .multilineTextAlignment(.center)
            }
        }
        .padding(40)
        .frame(maxWidth: .infinity, alignment: .top)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 28))
    }

    private var connectionCard: some View {
        VStack(spacing: 24) {
            if server.isRunning {
                Text("Scan to connect")
                    .font(.title3.weight(.semibold))

                QRCodeView(text: connectionURL)
                    .frame(width: 280, height: 280)
                    .padding(20)
                    .background(.white, in: RoundedRectangle(cornerRadius: 16))

                VStack(spacing: 8) {
                    ForEach(server.addresses, id: \.self) { address in
                        Text(verbatim: "http://\(address):\(server.port)")
                            .font(.callout.monospaced())
                            .foregroundStyle(.secondary)
                    }
                }
            } else {
                Image(systemName: "qrcode")
                    .font(.system(size: 90))
                    .foregroundStyle(.secondary)

                Text("Start the server to get a connection QR code")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
            }
        }
        .padding(40)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 28))
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
            Group {
                if server.isBusy {
                    ProgressView()
                } else {
                    Label(server.isRunning ? "Stop Server" : "Start Server",
                          systemImage: server.isRunning ? "stop.fill" : "play.fill")
                }
            }
            .frame(maxWidth: .infinity)
        }
        .buttonStyle(FilledActionButtonStyle(tint: server.isRunning ? Theme.danger : Theme.primary))
        .disabled(server.isBusy)
    }

    private func fieldRow(_ label: String, text: Binding<String>) -> some View {
        HStack {
            Text(label)
                .foregroundStyle(.secondary)

            Spacer()

            TextField(label, text: text)
                .multilineTextAlignment(.trailing)
                .frame(maxWidth: 360)
        }
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
