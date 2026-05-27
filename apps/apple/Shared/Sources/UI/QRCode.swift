import Foundation

// pure-swift qr code generator (byte mode), so it works on watchos where coreimage is unavailable.
// follows the qr code model 2 specification (iso/iec 18004); algorithm based on project nayuki (mit).
struct QRCode {
    enum Correction: Int {
        case low = 0
        case medium = 1
        case quartile = 2
        case high = 3

        // format indicator bits used in the format information
        var formatBits: Int {
            switch self {
            case .low: return 0b01
            case .medium: return 0b00
            case .quartile: return 0b11
            case .high: return 0b10
            }
        }
    }

    let size: Int
    private let modules: [[Bool]]

    fileprivate init(size: Int, modules: [[Bool]]) {
        self.size = size
        self.modules = modules
    }

    func isDark(x: Int, y: Int) -> Bool {
        guard x >= 0, x < size, y >= 0, y < size else { return false }
        return modules[y][x]
    }

    // encodes text as utf-8 bytes, picking the smallest version that fits the given correction level
    static func encode(_ text: String, correction: Correction = .medium) -> QRCode? {
        let data = Array(text.utf8)
        let builder = Builder(data: data, correction: correction)
        return builder?.build()
    }
}

private struct Builder {
    let data: [UInt8]
    let correction: QRCode.Correction
    let version: Int
    let size: Int

    var modules: [[Bool]]
    var isFunction: [[Bool]]

    init?(data: [UInt8], correction: QRCode.Correction) {
        // byte mode segment length: 4-bit mode + char-count + 8 bits per byte
        var chosen = 0

        for candidate in 1...40 {
            let capacityBits = Builder.numDataCodewords(version: candidate, correction: correction) * 8
            let charCountBits = candidate < 10 ? 8 : 16
            let usedBits = 4 + charCountBits + data.count * 8

            if usedBits <= capacityBits {
                chosen = candidate
                break
            }
        }

        guard chosen != 0 else { return nil }

        self.data = data
        self.correction = correction
        version = chosen
        size = version * 4 + 17
        modules = Array(repeating: Array(repeating: false, count: size), count: size)
        isFunction = Array(repeating: Array(repeating: false, count: size), count: size)
    }

    func build() -> QRCode {
        var b = self
        b.drawFunctionPatterns()
        let codewords = b.makeCodewords()
        b.drawCodewords(codewords)
        b.applyBestMask()
        return QRCode(size: size, modules: b.modules)
    }

    // MARK: data and error correction

    func makeCodewords() -> [UInt8] {
        let charCountBits = version < 10 ? 8 : 16

        var bits: [Bool] = []
        appendBits(0b0100, 4, to: &bits)                 // byte mode indicator
        appendBits(data.count, charCountBits, to: &bits)  // character count

        for byte in data {
            appendBits(Int(byte), 8, to: &bits)
        }

        let dataCapacityBits = Builder.numDataCodewords(version: version, correction: correction) * 8

        // terminator and byte alignment
        let terminator = min(4, dataCapacityBits - bits.count)
        appendBits(0, terminator, to: &bits)
        appendBits(0, (8 - bits.count % 8) % 8, to: &bits)

        // pad bytes
        var padByte = 0xEC
        while bits.count < dataCapacityBits {
            appendBits(padByte, 8, to: &bits)
            padByte ^= 0xEC ^ 0x11
        }

        var dataCodewords = [UInt8](repeating: 0, count: bits.count / 8)
        for (i, bit) in bits.enumerated() where bit {
            dataCodewords[i >> 3] |= UInt8(1 << (7 - (i & 7)))
        }

        return addEccAndInterleave(dataCodewords)
    }

    func addEccAndInterleave(_ data: [UInt8]) -> [UInt8] {
        let numBlocks = Builder.eccBlocks(version: version, correction: correction)
        let blockEccLen = Builder.eccCodewordsPerBlock(version: version, correction: correction)
        let rawCodewords = Builder.numRawDataModules(version: version) / 8
        let numShortBlocks = numBlocks - rawCodewords % numBlocks
        let shortBlockLen = rawCodewords / numBlocks

        var blocks: [[UInt8]] = []
        let divisor = reedSolomonDivisor(degree: blockEccLen)
        var k = 0

        for i in 0..<numBlocks {
            let dataLen = shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1)
            var block = Array(data[k..<(k + dataLen)])
            k += dataLen

            let ecc = reedSolomonRemainder(block, divisor: divisor)

            if i < numShortBlocks {
                block.append(0) // placeholder to align interleaving
            }

            block.append(contentsOf: ecc)
            blocks.append(block)
        }

        // interleave the blocks
        var result: [UInt8] = []

        for i in 0..<blocks[0].count {
            for (j, block) in blocks.enumerated() {
                // skip the padding placeholder column in short blocks
                if i != shortBlockLen - blockEccLen || j >= numShortBlocks {
                    result.append(block[i])
                }
            }
        }

        return result
    }

    // MARK: module drawing

    mutating func drawFunctionPatterns() {
        for i in 0..<size {
            setFunction(6, i, dark: i % 2 == 0)
            setFunction(i, 6, dark: i % 2 == 0)
        }

        drawFinderPattern(3, 3)
        drawFinderPattern(size - 4, 3)
        drawFinderPattern(3, size - 4)

        let align = alignmentPatternPositions()
        let count = align.count

        for i in 0..<count {
            for j in 0..<count {
                if (i == 0 && j == 0) || (i == 0 && j == count - 1) || (i == count - 1 && j == 0) {
                    continue
                }
                drawAlignmentPattern(align[i], align[j])
            }
        }

        drawFormatBits(mask: 0)
        drawVersion()
    }

    mutating func drawFinderPattern(_ x: Int, _ y: Int) {
        for dy in -4...4 {
            for dx in -4...4 {
                let dist = max(abs(dx), abs(dy))
                let xx = x + dx
                let yy = y + dy
                if xx >= 0, xx < size, yy >= 0, yy < size {
                    setFunction(xx, yy, dark: dist != 2 && dist != 4)
                }
            }
        }
    }

    mutating func drawAlignmentPattern(_ x: Int, _ y: Int) {
        for dy in -2...2 {
            for dx in -2...2 {
                setFunction(x + dx, y + dy, dark: max(abs(dx), abs(dy)) != 1)
            }
        }
    }

    mutating func drawFormatBits(mask: Int) {
        let data = correction.formatBits << 3 | mask
        var rem = data

        for _ in 0..<10 {
            rem = (rem << 1) ^ ((rem >> 9) * 0x537)
        }

        let bits = (data << 10 | rem) ^ 0x5412

        for i in 0..<6 { setFunction(8, i, dark: getBit(bits, i)) }
        setFunction(8, 7, dark: getBit(bits, 6))
        setFunction(8, 8, dark: getBit(bits, 7))
        setFunction(7, 8, dark: getBit(bits, 8))
        for i in 9..<15 { setFunction(14 - i, 8, dark: getBit(bits, i)) }

        for i in 0..<8 { setFunction(size - 1 - i, 8, dark: getBit(bits, i)) }
        for i in 8..<15 { setFunction(8, size - 15 + i, dark: getBit(bits, i)) }
        setFunction(8, size - 8, dark: true)
    }

    mutating func drawVersion() {
        guard version >= 7 else { return }

        var rem = version
        for _ in 0..<12 {
            rem = (rem << 1) ^ ((rem >> 11) * 0x1F25)
        }
        let bits = version << 12 | rem

        for i in 0..<18 {
            let dark = getBit(bits, i)
            let a = size - 11 + i % 3
            let b = i / 3
            setFunction(a, b, dark: dark)
            setFunction(b, a, dark: dark)
        }
    }

    mutating func drawCodewords(_ codewords: [UInt8]) {
        var i = 0
        var col = size - 1

        while col >= 1 {
            if col == 6 { col = 5 }

            for vert in 0..<size {
                for j in 0..<2 {
                    let x = col - j
                    let upward = ((col + 1) & 2) == 0
                    let y = upward ? size - 1 - vert : vert

                    if !isFunction[y][x] && i < codewords.count * 8 {
                        modules[y][x] = getBit(Int(codewords[i >> 3]), 7 - (i & 7))
                        i += 1
                    }
                }
            }

            col -= 2
        }
    }

    // MARK: masking

    mutating func applyBestMask() {
        var bestMask = 0
        var minPenalty = Int.max

        for mask in 0..<8 {
            applyMask(mask)
            drawFormatBits(mask: mask)
            let penalty = penaltyScore()

            if penalty < minPenalty {
                minPenalty = penalty
                bestMask = mask
            }

            applyMask(mask) // xor again to revert
        }

        applyMask(bestMask)
        drawFormatBits(mask: bestMask)
    }

    mutating func applyMask(_ mask: Int) {
        for y in 0..<size {
            for x in 0..<size where !isFunction[y][x] {
                let invert: Bool
                switch mask {
                case 0: invert = (x + y) % 2 == 0
                case 1: invert = y % 2 == 0
                case 2: invert = x % 3 == 0
                case 3: invert = (x + y) % 3 == 0
                case 4: invert = (x / 3 + y / 2) % 2 == 0
                case 5: invert = x * y % 2 + x * y % 3 == 0
                case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0
                default: invert = ((x + y) % 2 + x * y % 3) % 2 == 0
                }

                if invert {
                    modules[y][x].toggle()
                }
            }
        }
    }

    func penaltyScore() -> Int {
        var result = 0
        let penaltyN1 = 3, penaltyN2 = 3, penaltyN4 = 10

        // rows and columns of consecutive same-color modules
        for y in 0..<size {
            var runColor = false
            var runLen = 0
            for x in 0..<size {
                if modules[y][x] == runColor {
                    runLen += 1
                    if runLen == 5 { result += penaltyN1 }
                    else if runLen > 5 { result += 1 }
                } else {
                    runColor = modules[y][x]
                    runLen = 1
                }
            }
        }
        for x in 0..<size {
            var runColor = false
            var runLen = 0
            for y in 0..<size {
                if modules[y][x] == runColor {
                    runLen += 1
                    if runLen == 5 { result += penaltyN1 }
                    else if runLen > 5 { result += 1 }
                } else {
                    runColor = modules[y][x]
                    runLen = 1
                }
            }
        }

        // 2x2 blocks of the same color
        for y in 0..<(size - 1) {
            for x in 0..<(size - 1) {
                let c = modules[y][x]
                if c == modules[y][x + 1] && c == modules[y + 1][x] && c == modules[y + 1][x + 1] {
                    result += penaltyN2
                }
            }
        }

        // proportion of dark modules
        var dark = 0
        for row in modules { for cell in row where cell { dark += 1 } }
        let total = size * size
        let k = (abs(dark * 20 - total * 10) + total - 1) / total - 1
        result += k * penaltyN4

        return result
    }

    // MARK: helpers

    mutating func setFunction(_ x: Int, _ y: Int, dark: Bool) {
        guard x >= 0, x < size, y >= 0, y < size else { return }
        modules[y][x] = dark
        isFunction[y][x] = true
    }

    func alignmentPatternPositions() -> [Int] {
        guard version > 1 else { return [] }

        let count = version / 7 + 2
        let step = version == 32 ? 26 : (version * 4 + count * 2 + 1) / (count * 2 - 2) * 2
        var positions = [6]
        var pos = size - 7

        while positions.count < count {
            positions.insert(pos, at: 1)
            pos -= step
        }

        return positions
    }

    func appendBits(_ value: Int, _ length: Int, to bits: inout [Bool]) {
        guard length > 0 else { return }
        for i in stride(from: length - 1, through: 0, by: -1) {
            bits.append((value >> i) & 1 == 1)
        }
    }

    func getBit(_ value: Int, _ index: Int) -> Bool {
        (value >> index) & 1 == 1
    }

    // MARK: capacity tables

    static func numRawDataModules(version: Int) -> Int {
        var result = (16 * version + 128) * version + 64
        if version >= 2 {
            let numAlign = version / 7 + 2
            result -= (25 * numAlign - 10) * numAlign - 55
            if version >= 7 {
                result -= 36
            }
        }
        return result
    }

    static func numDataCodewords(version: Int, correction: QRCode.Correction) -> Int {
        numRawDataModules(version: version) / 8
            - eccCodewordsPerBlock(version: version, correction: correction)
            * eccBlocks(version: version, correction: correction)
    }

    static func eccCodewordsPerBlock(version: Int, correction: QRCode.Correction) -> Int {
        ECC_CODEWORDS_PER_BLOCK[correction.rawValue][version]
    }

    static func eccBlocks(version: Int, correction: QRCode.Correction) -> Int {
        NUM_ERROR_CORRECTION_BLOCKS[correction.rawValue][version]
    }
}

// MARK: reed-solomon over gf(256)

private func reedSolomonDivisor(degree: Int) -> [UInt8] {
    var result = [UInt8](repeating: 0, count: degree)
    result[degree - 1] = 1
    var root: UInt8 = 1

    for _ in 0..<degree {
        for j in 0..<degree {
            result[j] = gfMultiply(result[j], root)
            if j + 1 < degree {
                result[j] ^= result[j + 1]
            }
        }
        root = gfMultiply(root, 0x02)
    }

    return result
}

private func reedSolomonRemainder(_ data: [UInt8], divisor: [UInt8]) -> [UInt8] {
    var result = [UInt8](repeating: 0, count: divisor.count)

    for byte in data {
        let factor = byte ^ result.removeFirst()
        result.append(0)
        for i in 0..<result.count {
            result[i] ^= gfMultiply(divisor[i], factor)
        }
    }

    return result
}

private func gfMultiply(_ x: UInt8, _ y: UInt8) -> UInt8 {
    var z: UInt8 = 0
    for i in stride(from: 7, through: 0, by: -1) {
        z = (z << 1) ^ ((z >> 7) * 0x1D)
        z ^= ((y >> i) & 1) * x
    }
    return z
}

// MARK: spec tables (indexed [correction][version], version 1...40)

private let ECC_CODEWORDS_PER_BLOCK: [[Int]] = [
    [-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30],
    [-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28],
    [-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30],
    [-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30],
]

private let NUM_ERROR_CORRECTION_BLOCKS: [[Int]] = [
    [-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25],
    [-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49],
    [-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68],
    [-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81],
]
