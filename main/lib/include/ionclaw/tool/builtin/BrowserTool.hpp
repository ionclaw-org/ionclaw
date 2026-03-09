#pragma once

#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

// forward declarations for internal helper classes
class ChromeInstance;
class CdpClient;

class BrowserTool : public Tool
{
public:
    std::string execute(const nlohmann::json &params, const ToolContext &context) override;
    ToolSchema schema() const override;
    std::set<std::string> supportedPlatforms() const override;

private:
    // constants
    static constexpr int CDP_PORT = 9222;
    static constexpr int CDP_TIMEOUT_SECONDS = 30;
    static constexpr int MAX_INTERACTIVE_ELEMENTS = 50;

    // helpers
    static CdpClient &getCdpClient();
    static std::string ensureBrowser(bool headless);

    // grant access to private constants from helper classes
    friend class ChromeInstance;
    friend class CdpClient;
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
