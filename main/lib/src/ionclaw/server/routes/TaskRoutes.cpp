#include "ionclaw/server/Routes.hpp"

#include "Poco/URI.h"

#include "ionclaw/task/Task.hpp"

namespace ionclaw
{
namespace server
{

void Routes::handleTasksList(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{
    // extract optional agent filter from query params
    Poco::URI uri(req.getURI());
    std::string agentFilter;

    for (const auto &param : uri.getQueryParameters())
    {
        if (param.first == "agent")
        {
            agentFilter = param.second;
        }
    }

    auto tasks = taskManager->listTasks(agentFilter);
    nlohmann::json result = nlohmann::json::array();

    for (const auto &t : tasks)
    {
        result.push_back(t.toJson());
    }

    sendJson(resp, result);
}

void Routes::handleTaskGet(Poco::Net::HTTPServerRequest &, Poco::Net::HTTPServerResponse &resp, const std::string &taskId)
{
    auto task = taskManager->getTask(taskId);

    if (task.id.empty())
    {
        sendError(resp, "Task not found", 404);
        return;
    }

    sendJson(resp, task.toJson());
}

void Routes::handleTaskUpdate(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp, const std::string &taskId)
{
    try
    {
        auto body = nlohmann::json::parse(readBody(req));
        auto stateStr = body.value("state", "");

        if (stateStr.empty())
        {
            sendError(resp, "State is required");
            return;
        }

        // update task state and return updated task
        auto state = ionclaw::task::Task::stateFromString(stateStr);
        auto result = body.value("result", "");
        taskManager->updateState(taskId, state, result);

        auto task = taskManager->getTask(taskId);
        sendJson(resp, task.toJson());
    }
    catch (const std::exception &e)
    {
        sendError(resp, e.what(), 400);
    }
}

} // namespace server
} // namespace ionclaw
