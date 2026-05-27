import SwiftUI

// run-state indicator
struct ServerStatusView: View {
    let isRunning: Bool

    var body: some View {
        VStack(spacing: 8) {
            Image(systemName: isRunning ? "circle.fill" : "circle")
                .font(.title)
                .foregroundStyle(isRunning ? Theme.success : Color.secondary)

            Text(isRunning ? "Running" : "Stopped")
                .font(.headline)
                .foregroundStyle(isRunning ? Theme.success : Color.secondary)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 4)
    }
}
