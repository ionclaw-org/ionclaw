import SwiftUI

// renders a scannable qr code by drawing the generated module matrix with a canvas.
// uses the pure-swift QRCode generator so it works on every platform, including watchos.
struct QRCodeView: View {
    let text: String

    var body: some View {
        if let code = QRCode.encode(text) {
            Canvas { context, size in
                // include the standard 4-module quiet zone on a white field so it scans cleanly
                let quietZone = 4
                let modules = code.size + quietZone * 2
                let scale = size.width / CGFloat(modules)

                context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(.white))

                for y in 0..<code.size {
                    for x in 0..<code.size where code.isDark(x: x, y: y) {
                        let rect = CGRect(
                            x: CGFloat(x + quietZone) * scale,
                            y: CGFloat(y + quietZone) * scale,
                            width: scale,
                            height: scale
                        )
                        context.fill(Path(rect), with: .color(.black))
                    }
                }
            }
            .aspectRatio(1, contentMode: .fit)
            .accessibilityLabel(Text(text))
        } else {
            Image(systemName: "qrcode")
                .resizable()
                .scaledToFit()
                .foregroundStyle(.secondary)
        }
    }
}
