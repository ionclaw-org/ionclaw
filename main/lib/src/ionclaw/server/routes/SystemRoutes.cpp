#include "ionclaw/server/Routes.hpp"

#include <cstring>
#include <fstream>
#include <thread>

#include "ionclaw/Version.hpp"

#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/utsname.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace ionclaw
{
namespace server
{

void Routes::handleHealth(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    sendJson(resp, {{"status", "ok"}, {"version", ionclaw::VERSION}});
}

void Routes::handleVersion(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    sendJson(resp, {{"version", ionclaw::VERSION}});
}

void Routes::handleSystemInfo(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp)
{
    nlohmann::json info;

    // os information
#if defined(__APPLE__) || defined(__linux__)
    struct utsname unameData;

    if (uname(&unameData) == 0)
    {
        info["os"] = {
            {"system", std::string(unameData.sysname)},
            {"release", std::string(unameData.release)},
            {"version", std::string(unameData.version)},
            {"machine", std::string(unameData.machine)},
        };
    }
    else
    {
        info["os"] = {{"system", "Unknown"}, {"release", ""}, {"version", ""}, {"machine", ""}};
    }
#elif defined(_WIN32)
    std::string arch = "unknown";
    SYSTEM_INFO sysInfo;
    GetNativeSystemInfo(&sysInfo);

    if (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
    {
        arch = "x86_64";
    }
    else if (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)
    {
        arch = "arm64";
    }
    else if (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
    {
        arch = "x86";
    }

    std::string release;
    HKEY osKey = nullptr;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &osKey) == ERROR_SUCCESS)
    {
        char value[256];
        std::string productName, displayVersion, build;

        DWORD size = sizeof(value);
        if (RegQueryValueExA(osKey, "ProductName", nullptr, nullptr, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS)
        {
            productName.assign(value, strnlen(value, size));
        }

        size = sizeof(value);
        if (RegQueryValueExA(osKey, "DisplayVersion", nullptr, nullptr, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS)
        {
            displayVersion.assign(value, strnlen(value, size));
        }

        size = sizeof(value);
        if (RegQueryValueExA(osKey, "CurrentBuild", nullptr, nullptr, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS)
        {
            build.assign(value, strnlen(value, size));
        }

        RegCloseKey(osKey);

        release = productName;
        if (!displayVersion.empty())
        {
            release += " " + displayVersion;
        }
        if (!build.empty())
        {
            release += " (build " + build + ")";
        }
    }

    if (release.empty())
    {
        release = "Windows";
    }

    info["os"] = {{"system", "Windows"}, {"release", release}, {"version", release}, {"machine", arch}};
#else
    info["os"] = {{"system", "Unknown"}, {"release", ""}, {"version", ""}, {"machine", ""}};
#endif

    // cpu and memory information
#if defined(__APPLE__)
    std::string processorName;

    // machdep.cpu.brand_string works on macOS but not iOS
    char cpuBrand[256] = "";
    size_t size = sizeof(cpuBrand);
    if (sysctlbyname("machdep.cpu.brand_string", cpuBrand, &size, nullptr, 0) == 0 && cpuBrand[0] != '\0')
    {
        processorName = cpuBrand;
    }
    else
    {
        // iOS: use hw.machine (e.g. "iPhone15,2") and hw.model as fallback
        char machine[128] = "";
        size = sizeof(machine);
        sysctlbyname("hw.machine", machine, &size, nullptr, 0);
        processorName = (machine[0] != '\0') ? std::string(machine) : "Apple";
    }

    int cpuCount = 0;
    size = sizeof(cpuCount);
    sysctlbyname("hw.ncpu", &cpuCount, &size, nullptr, 0);

    info["cpu"] = {{"processor", processorName}, {"cores", cpuCount}};

    int64_t memSize = 0;
    size = sizeof(memSize);
    sysctlbyname("hw.memsize", &memSize, &size, nullptr, 0);

    info["memory"] = {{"total_gb", static_cast<double>(memSize) / (1024.0 * 1024.0 * 1024.0)}};
#elif defined(__linux__)
    // read CPU model from /proc/cpuinfo
    std::string processorName;
    {
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        while (std::getline(cpuinfo, line))
        {
            // arm: "Hardware", x86: "model name"
            if (line.compare(0, 10, "model name") == 0 || line.compare(0, 8, "Hardware") == 0)
            {
                auto pos = line.find(':');
                if (pos != std::string::npos)
                {
                    processorName = line.substr(pos + 1);
                    // trim leading whitespace
                    auto start = processorName.find_first_not_of(" \t");
                    if (start != std::string::npos)
                        processorName = processorName.substr(start);
                    break;
                }
            }
        }
    }
    if (processorName.empty())
        processorName = "Linux";

    info["cpu"] = {{"processor", processorName}, {"cores", static_cast<int>(std::thread::hardware_concurrency())}};

    struct sysinfo si;

    if (sysinfo(&si) == 0)
    {
        info["memory"] = {{"total_gb", static_cast<double>(si.totalram) * si.mem_unit / (1024.0 * 1024.0 * 1024.0)}};
    }
    else
    {
        info["memory"] = {{"total_gb", nullptr}};
    }
#elif defined(_WIN32)
    std::string processorName;
    HKEY cpuKey = nullptr;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &cpuKey) == ERROR_SUCCESS)
    {
        char value[256];
        DWORD size = sizeof(value);

        if (RegQueryValueExA(cpuKey, "ProcessorNameString", nullptr, nullptr, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS)
        {
            processorName.assign(value, strnlen(value, size));
        }

        RegCloseKey(cpuKey);
    }

    // registry processor names are commonly padded with trailing spaces
    auto nameEnd = processorName.find_last_not_of(" \t");
    if (nameEnd != std::string::npos)
    {
        processorName = processorName.substr(0, nameEnd + 1);
    }

    if (processorName.empty())
    {
        processorName = "unknown";
    }

    info["cpu"] = {{"processor", processorName}, {"cores", static_cast<int>(std::thread::hardware_concurrency())}};

    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);

    if (GlobalMemoryStatusEx(&memStatus))
    {
        info["memory"] = {{"total_gb", static_cast<double>(memStatus.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0)}};
    }
    else
    {
        info["memory"] = {{"total_gb", nullptr}};
    }
#else
    info["cpu"] = {{"processor", "unknown"}, {"cores", static_cast<int>(std::thread::hardware_concurrency())}};
    info["memory"] = {{"total_gb", nullptr}};
#endif

    info["version"] = ionclaw::VERSION;

    sendJson(resp, info);
}

} // namespace server
} // namespace ionclaw
