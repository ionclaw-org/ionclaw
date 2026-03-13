#include "ionclaw/server/Routes.hpp"

#include <algorithm>
#include <set>

#include "Poco/URI.h"

namespace ionclaw
{
namespace server
{

std::pair<bool, std::string> Routes::extractAgentParam(Poco::Net::HTTPServerRequest &req)
{
    Poco::URI uri(req.getURI());

    for (const auto &param : uri.getQueryParameters())
    {
        if (param.first == "agent")
        {
            return {true, param.second};
        }
    }

    return {false, ""};
}

agent::SkillsLoader Routes::createSkillsLoader(const config::Config &cfg, const std::string &workspacePath)
{
    return agent::SkillsLoader(cfg.projectPath, workspacePath);
}

// find the workspace that contains a skill by searching all agent workspaces
std::string Routes::resolveWorkspaceForSkill(const std::string &skillName) const
{
    // try project-level first (no workspace)
    auto rootLoader = createSkillsLoader(*config, "");
    auto rootSkills = rootLoader.discoverSkills();

    if (rootSkills.count(skillName))
    {
        return "";
    }

    // search each agent workspace
    std::set<std::string> seen;

    for (const auto &[agentName, agentCfg] : config->agents)
    {
        if (agentCfg.workspace.empty() || !seen.insert(agentCfg.workspace).second)
        {
            continue;
        }

        auto loader = createSkillsLoader(*config, agentCfg.workspace);
        auto skills = loader.discoverSkills();

        if (skills.count(skillName))
        {
            return agentCfg.workspace;
        }
    }

    return "";
}

void Routes::handleSkillsList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    auto [hasAgent, agentName] = extractAgentParam(req);

    nlohmann::json result = nlohmann::json::array();

    if (hasAgent && !agentName.empty())
    {
        // specific agent: builtin + project + that agent's workspace
        auto it = config->agents.find(agentName);

        if (it == config->agents.end())
        {
            sendError(resp, "Unknown agent", 404);
            return;
        }

        auto loader = createSkillsLoader(*config, it->second.workspace);

        for (const auto &skill : loader.listSkills())
        {
            result.push_back({
                {"name", skill.name},
                {"description", skill.description},
                {"always", skill.always},
                {"available", skill.available},
                {"source", skill.source},
                {"publisher", skill.publisher},
                {"agent", skill.source == "workspace" ? agentName : std::string()},
            });
        }
    }
    else if (hasAgent && agentName.empty())
    {
        // ?agent= (empty): builtin + project only
        auto loader = createSkillsLoader(*config, "");

        for (const auto &skill : loader.listSkills())
        {
            result.push_back({
                {"name", skill.name},
                {"description", skill.description},
                {"always", skill.always},
                {"available", skill.available},
                {"source", skill.source},
                {"publisher", skill.publisher},
                {"agent", ""},
            });
        }
    }
    else
    {
        // no ?agent param: merge ALL (builtin + project + all agent workspaces)
        // step 1: builtin + project (root loader, no workspace)
        auto rootLoader = createSkillsLoader(*config, "");

        for (const auto &skill : rootLoader.listSkills())
        {
            result.push_back({
                {"name", skill.name},
                {"description", skill.description},
                {"always", skill.always},
                {"available", skill.available},
                {"source", skill.source},
                {"publisher", skill.publisher},
                {"agent", ""},
            });
        }

        // step 2: each unique agent workspace → only workspace-source skills
        std::set<std::string> seenWorkspaces;

        for (const auto &[name, agentCfg] : config->agents)
        {
            if (agentCfg.workspace.empty())
            {
                continue;
            }

            if (!seenWorkspaces.insert(agentCfg.workspace).second)
            {
                continue; // already processed this workspace path
            }

            auto loader = createSkillsLoader(*config, agentCfg.workspace);

            for (const auto &skill : loader.listSkills())
            {
                if (skill.source != "workspace")
                {
                    continue; // skip builtin/project (already added from root)
                }

                result.push_back({
                    {"name", skill.name},
                    {"description", skill.description},
                    {"always", skill.always},
                    {"available", skill.available},
                    {"source", skill.source},
                    {"publisher", skill.publisher},
                    {"agent", name},
                });
            }
        }
    }

    // sort by short name (after last "/") alphabetically
    std::sort(result.begin(), result.end(), [](const nlohmann::json &a, const nlohmann::json &b)
              {
        auto aName = a["name"].get<std::string>();
        auto bName = b["name"].get<std::string>();
        auto aSlash = aName.rfind('/');
        auto bSlash = bName.rfind('/');
        auto aShort = (aSlash != std::string::npos) ? aName.substr(aSlash + 1) : aName;
        auto bShort = (bSlash != std::string::npos) ? bName.substr(bSlash + 1) : bName;
        return aShort < bShort; });

    sendJson(resp, result);
}

void Routes::handleSkillGet(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    auto [hasAgent, agentName] = extractAgentParam(req);
    std::string workspace;

    if (hasAgent && !agentName.empty())
    {
        auto it = config->agents.find(agentName);

        if (it == config->agents.end())
        {
            sendError(resp, "Unknown agent", 404);
            return;
        }

        workspace = it->second.workspace;
    }
    else
    {
        workspace = resolveWorkspaceForSkill(name);
    }

    auto loader = createSkillsLoader(*config, workspace);
    auto content = loader.loadSkillRaw(name);

    if (content.empty())
    {
        sendError(resp, "Skill not found", 404);
        return;
    }

    sendJson(resp, {{"name", name}, {"content", content}});
}

void Routes::handleSkillUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    try
    {
        auto [hasAgent, agentName] = extractAgentParam(req);
        std::string workspace;

        if (hasAgent && !agentName.empty())
        {
            auto it = config->agents.find(agentName);

            if (it == config->agents.end())
            {
                sendError(resp, "Unknown agent", 404);
                return;
            }

            workspace = it->second.workspace;
        }
        else
        {
            workspace = resolveWorkspaceForSkill(name);
        }

        auto body = nlohmann::json::parse(readBody(req));
        auto content = body.value("content", "");

        auto loader = createSkillsLoader(*config, workspace);
        auto error = loader.saveSkill(name, content);

        if (!error.empty())
        {
            sendError(resp, error);
            return;
        }

        sendJson(resp, {{"ok", true}});
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what());
    }
}

void Routes::handleSkillDelete(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &name)
{
    auto [hasAgent, agentName] = extractAgentParam(req);
    std::string workspace;

    if (hasAgent && !agentName.empty())
    {
        auto it = config->agents.find(agentName);

        if (it == config->agents.end())
        {
            sendError(resp, "Unknown agent", 404);
            return;
        }

        workspace = it->second.workspace;
    }
    else
    {
        workspace = resolveWorkspaceForSkill(name);
    }

    auto loader = createSkillsLoader(*config, workspace);
    auto error = loader.deleteSkill(name);

    if (!error.empty())
    {
        sendError(resp, error);
        return;
    }

    sendJson(resp, {{"ok", true}});
}

} // namespace server
} // namespace ionclaw
