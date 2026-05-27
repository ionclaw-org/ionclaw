import SwiftUI

// brand accent colors, mirrored from the flutter runner
enum Theme {
    static let primary = Color(hex: 0x0A8DCF)
    static let success = Color(hex: 0x2E7D32)
    static let danger = Color(hex: 0xC62828)
}

extension Color {
    init(hex: UInt32) {
        let red = Double((hex >> 16) & 0xFF) / 255
        let green = Double((hex >> 8) & 0xFF) / 255
        let blue = Double(hex & 0xFF) / 255

        self.init(red: red, green: green, blue: blue)
    }
}
