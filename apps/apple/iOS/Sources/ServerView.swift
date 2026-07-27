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
            ScrollView {
                VStack(spacing: 28) {
                    ServerStatusView(isRunning: server.isRunning)

                    serverCard

                    actionButtons

                    if server.isRunning, !server.addresses.isEmpty {
                        networkCard
                    }

                    if let error = server.lastError {
                        Text(error)
                            .font(.footnote)
                            .foregroundStyle(Theme.danger)
                    }
                }
                .padding(.horizontal, 24)
                .padding(.vertical, 40)
                .frame(maxWidth: .infinity)
            }
            .background(Theme.screen)
            .scrollDismissesKeyboard(.interactively)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .principal) {
                    Image("HeaderLogo")
                        .resizable()
                        .scaledToFit()
                        .frame(height: 34)
                }
            }
            .toolbarBackground(Theme.header, for: .navigationBar)
            .toolbarBackground(.visible, for: .navigationBar)
            .toolbarColorScheme(.dark, for: .navigationBar)
            .navigationDestination(isPresented: $showPanel) {
                PanelView(url: panelURL)
            }
        }
        .tint(Theme.primary)
    }

    private var serverCard: some View {
        SectionCard(title: "Server", systemImage: "server.rack", tint: Theme.primary) {
            HStack(alignment: .top, spacing: 12) {
                borderedField("Host", placeholder: "0.0.0.0", text: $config.host)
                    .focused($focusedField, equals: .host)

                borderedField("Port", placeholder: "8080", text: portText)
                    .keyboardType(.numberPad)
                    .focused($focusedField, equals: .port)
                    .frame(width: 96)
            }
            .disabled(server.isRunning || server.isBusy)
        }
    }

    private var actionButtons: some View {
        VStack(spacing: 12) {
            if server.isBusy {
                ProgressView()
                    .frame(height: 52)
            } else if server.isRunning {
                outlinedButton("Stop Server", systemImage: "stop.fill", color: Theme.danger) {
                    Task { await server.stop() }
                }

                outlinedButton("Open Panel", systemImage: "globe", color: Theme.primary) {
                    showPanel = true
                }
            } else {
                Button {
                    focusedField = nil
                    Task { await server.start(host: config.host, port: config.port) }
                } label: {
                    actionLabel("Start Server", systemImage: "play.fill")
                        .foregroundStyle(.white)
                        .background(Theme.primary)
                        .clipShape(RoundedRectangle(cornerRadius: 12))
                }

                outlinedButton("Initialize Project", systemImage: "folder", color: Theme.primary) {
                    focusedField = nil
                    Task { await server.initializeProject() }
                }
            }
        }
    }

    private var networkCard: some View {
        SectionCard(title: "Network", systemImage: "wifi", tint: Theme.success) {
            VStack(spacing: 4) {
                ForEach(server.addresses, id: \.self, content: networkRow)
            }
        }
    }

    private func networkRow(_ address: String) -> some View {
        let url = "http://\(address):\(server.port)"

        return Button {
            UIPasteboard.general.string = url
            copiedAddress = address
            Task {
                try? await Task.sleep(for: .seconds(2))
                if copiedAddress == address {
                    copiedAddress = nil
                }
            }
        } label: {
            HStack {
                Text(url)
                    .font(.callout.monospaced())
                    .foregroundStyle(Color(hex: 0x616161))

                Spacer()

                Image(systemName: copiedAddress == address ? "checkmark" : "doc.on.doc")
                    .foregroundStyle(copiedAddress == address ? Theme.success : Theme.primary)
            }
            .padding(.vertical, 8)
            .contentShape(Rectangle())
        }
    }

    private func borderedField(_ label: String, placeholder: String, text: Binding<String>) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(label)
                .font(.caption)
                .foregroundStyle(Theme.secondaryLabel)

            TextField(placeholder, text: text)
                .autocorrectionDisabled()
                .textInputAutocapitalization(.never)
                .foregroundStyle(Theme.label)
                .padding(12)
                .overlay(RoundedRectangle(cornerRadius: 12).stroke(Theme.cardBorder, lineWidth: 1))
        }
    }

    private func outlinedButton(_ title: String, systemImage: String, color: Color, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            actionLabel(title, systemImage: systemImage)
                .foregroundStyle(color)
                .overlay(RoundedRectangle(cornerRadius: 12).stroke(color, lineWidth: 1.5))
        }
    }

    private func actionLabel(_ title: String, systemImage: String) -> some View {
        HStack(spacing: 8) {
            Image(systemName: systemImage)
            Text(title).fontWeight(.semibold)
        }
        .frame(width: 240, height: 52)
    }

    private var panelURL: URL? {
        let host = config.host == "0.0.0.0" ? "localhost" : config.host
        return URL(string: "http://\(host):\(server.port)/app/")
    }

    private var portText: Binding<String> {
        Binding(
            get: { String(config.port) },
            set: { config.port = Int($0.filter(\.isNumber)) ?? config.port }
        )
    }
}

// a white rounded card with a tinted icon badge and a title, mirroring the flutter cards
private struct SectionCard<Content: View>: View {
    let title: String
    let systemImage: String
    let tint: Color
    @ViewBuilder let content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            HStack(spacing: 12) {
                Image(systemName: systemImage)
                    .font(.system(size: 16))
                    .foregroundStyle(tint)
                    .frame(width: 32, height: 32)
                    .background(tint.opacity(0.1))
                    .clipShape(RoundedRectangle(cornerRadius: 8))

                Text(title)
                    .font(.headline)
                    .foregroundStyle(Theme.label)
            }

            content
        }
        .padding(20)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Theme.card)
        .clipShape(RoundedRectangle(cornerRadius: 16))
        .overlay(RoundedRectangle(cornerRadius: 16).stroke(Theme.cardBorder, lineWidth: 1))
    }
}
