import SwiftUI

// filled action button with an explicit label color, because tvos borderedProminent
// renders the label inconsistently across versions when the button is not focused
struct FilledActionButtonStyle: ButtonStyle {
    var tint: Color = Theme.primary

    func makeBody(configuration: Configuration) -> some View {
        FilledActionButton(tint: tint, configuration: configuration)
    }
}

private struct FilledActionButton: View {
    let tint: Color
    let configuration: ButtonStyleConfiguration

    @Environment(\.isFocused) private var isFocused: Bool
    @Environment(\.isEnabled) private var isEnabled: Bool

    var body: some View {
        configuration.label
            .font(.headline)
            .padding(.vertical, 16)
            .padding(.horizontal, 28)
            .foregroundStyle(isFocused ? tint : .white)
            .background(isFocused ? Color.white : tint, in: Capsule())
            .opacity(isEnabled ? (configuration.isPressed ? 0.85 : 1) : 0.4)
            .scaleEffect(isFocused ? 1.04 : 1)
            .animation(.easeOut(duration: 0.15), value: isFocused)
    }
}
