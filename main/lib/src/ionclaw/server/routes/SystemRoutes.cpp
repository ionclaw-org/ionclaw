#include "ionclaw/server/Routes.hpp"

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
            // ARM: "Hardware", x86: "model name"
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
#else
    info["cpu"] = {{"processor", "unknown"}, {"cores", static_cast<int>(std::thread::hardware_concurrency())}};
    info["memory"] = {{"total_gb", nullptr}};
#endif

    info["version"] = ionclaw::VERSION;

    sendJson(resp, info);
}

} // namespace server
} // namespace ionclaw
