import Foundation

// enumerates the device IPv4 addresses so the user can reach the server from the lan
enum NetworkInterfaces {
    static func localIPv4Addresses() -> [String] {
        var addresses: [String] = []
        var head: UnsafeMutablePointer<ifaddrs>?

        guard getifaddrs(&head) == 0, let first = head else {
            return ["127.0.0.1"]
        }

        defer { freeifaddrs(head) }

        for pointer in sequence(first: first, next: { $0.pointee.ifa_next }) {
            let flags = pointer.pointee.ifa_flags
            let isUp = (flags & UInt32(IFF_UP)) != 0
            let isLoopback = (flags & UInt32(IFF_LOOPBACK)) != 0

            guard isUp, !isLoopback, let rawAddr = pointer.pointee.ifa_addr else { continue }
            guard rawAddr.pointee.sa_family == sa_family_t(AF_INET) else { continue }

            var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
            let result = getnameinfo(
                rawAddr,
                socklen_t(rawAddr.pointee.sa_len),
                &host,
                socklen_t(host.count),
                nil,
                0,
                NI_NUMERICHOST
            )

            guard result == 0 else { continue }

            let address = String(cString: host)

            if !addresses.contains(address) {
                addresses.append(address)
            }
        }

        return addresses.isEmpty ? ["127.0.0.1"] : addresses
    }
}
