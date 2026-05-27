import SwiftUI
import WebKit

// embedded web panel served by the local server, mirroring the flutter browser screen
struct PanelView: View {
    let url: URL?

    @State private var isLoading = true

    var body: some View {
        Group {
            if let url {
                WebPanel(url: url, isLoading: $isLoading)
                    .overlay {
                        if isLoading {
                            ProgressView()
                        }
                    }
                    .ignoresSafeArea(edges: .bottom)
            } else {
                Label("Server is not running", systemImage: "bolt.slash")
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle("Panel")
        .navigationBarTitleDisplayMode(.inline)
    }
}

private struct WebPanel: UIViewRepresentable {
    let url: URL
    @Binding var isLoading: Bool

    func makeCoordinator() -> Coordinator {
        Coordinator(isLoading: $isLoading)
    }

    func makeUIView(context: Context) -> WKWebView {
        let configuration = WKWebViewConfiguration()
        configuration.allowsInlineMediaPlayback = true

        // force a fixed viewport so the page cannot be pinch/double-tap zoomed
        let viewport = """
        var meta = document.querySelector('meta[name=viewport]') || document.createElement('meta');
        meta.name = 'viewport';
        meta.content = 'width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no';
        document.head.appendChild(meta);
        """
        configuration.userContentController.addUserScript(
            WKUserScript(source: viewport, injectionTime: .atDocumentEnd, forMainFrameOnly: true)
        )

        let webView = WKWebView(frame: .zero, configuration: configuration)
        webView.navigationDelegate = context.coordinator
        webView.uiDelegate = context.coordinator
        webView.scrollView.bouncesZoom = false
        webView.load(URLRequest(url: url))

        return webView
    }

    func updateUIView(_ webView: WKWebView, context: Context) {}

    final class Coordinator: NSObject, WKNavigationDelegate, WKUIDelegate {
        private let isLoading: Binding<Bool>

        init(isLoading: Binding<Bool>) {
            self.isLoading = isLoading
        }

        func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
            isLoading.wrappedValue = false
        }

        func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
            isLoading.wrappedValue = false
        }

        // the panel may request the microphone for voice features served by the local server
        func webView(
            _ webView: WKWebView,
            requestMediaCapturePermissionFor origin: WKSecurityOrigin,
            initiatedByFrame frame: WKFrameInfo,
            type: WKMediaCaptureType,
            decisionHandler: @escaping (WKPermissionDecision) -> Void
        ) {
            decisionHandler(.grant)
        }
    }
}
