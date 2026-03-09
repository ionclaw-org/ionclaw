#pragma once

#include <string>

#include "Poco/DOM/Element.h"

#include "ionclaw/tool/Tool.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

class RssReaderTool : public Tool
{
public:
    std::string execute(const nlohmann::json &params, const ToolContext &context) override;
    ToolSchema schema() const override;

private:
    // constants
    static constexpr size_t MAX_FEED_SIZE = 2 * 1024 * 1024; // 2MB
    static constexpr int MAX_SUMMARY_CHARS = 500;

    // helpers
    static std::string stripHtmlTags(const std::string &html);
    static std::string getElementText(Poco::XML::Element *parent, const std::string &tagName);
};

} // namespace builtin
} // namespace tool
} // namespace ionclaw
