#include "ionclaw/util/HttpClient.hpp"

#include <istream>
#include <memory>
#include <sstream>

#include "Poco/Net/HTTPClientSession.h"
#include "Poco/Net/HTTPRequest.h"
#include "Poco/Net/HTTPResponse.h"
#include "Poco/StreamCopier.h"
#include "Poco/URI.h"

#ifdef IONCLAW_HAS_SSL
#include "Poco/Net/Context.h"
#include "Poco/Net/HTTPSClientSession.h"
#endif

namespace ionclaw
{
namespace util
{

HttpClient::HttpClient(const std::string &baseUrl, int timeoutSeconds)
    : baseUrl(baseUrl)
    , timeoutSeconds(timeoutSeconds)
{
}

void HttpClient::setHeader(const std::string &key, const std::string &value)
{
    defaultHeaders[key] = value;
}

std::unique_ptr<Poco::Net::HTTPClientSession> HttpClient::createSession(const Poco::URI &uri, int timeoutSeconds, const std::string &proxy)
{
    std::unique_ptr<Poco::Net::HTTPClientSession> session;

    if (uri.getScheme() == "https")
    {
#ifdef IONCLAW_HAS_SSL
#ifdef _WIN32
        auto context = new Poco::Net::Context(
            Poco::Net::Context::CLIENT_USE, "");
#else
        auto context = new Poco::Net::Context(
            Poco::Net::Context::CLIENT_USE,
            "",
            "",
            "",
            Poco::Net::Context::VERIFY_NONE,
            9,
            true,
            "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
#endif

        session = std::make_unique<Poco::Net::HTTPSClientSession>(
            uri.getHost(),
            uri.getPort(),
            context);
#else
        throw std::runtime_error("HTTPS is not supported (built without SSL)");
#endif
    }
    else
    {
        session = std::make_unique<Poco::Net::HTTPClientSession>(
            uri.getHost(),
            uri.getPort());
    }

    session->setTimeout(Poco::Timespan(timeoutSeconds, 0));

    if (!proxy.empty())
    {
        std::string target = proxy;
        auto schemeEnd = proxy.find("://");
        if (schemeEnd != std::string::npos)
        {
            target = proxy.substr(schemeEnd + 3);
        }
        std::string host = target;
        unsigned short port = 8080;
        auto colon = target.find(':');
        if (colon != std::string::npos)
        {
            host = target.substr(0, colon);
            port = static_cast<unsigned short>(std::stoul(target.substr(colon + 1)));
        }
        session->setProxyHost(host);
        session->setProxyPort(port);
    }

    return session;
}

void HttpClient::applyHeaders(Poco::Net::HTTPRequest &request, const std::map<std::string, std::string> &headers)
{
    for (const auto &pair : headers)
    {
        request.set(pair.first, pair.second);
    }
}

HttpResponse HttpClient::readResponse(Poco::Net::HTTPResponse &response, std::istream &responseStream)
{
    HttpResponse result;
    result.statusCode = static_cast<int>(response.getStatus());

    Poco::StreamCopier::copyToString(responseStream, result.body);

    for (const auto &header : response)
    {
        result.headers[header.first] = header.second;
    }

    return result;
}

HttpResponse HttpClient::post(const std::string &path, const std::string &body)
{
    Poco::URI uri(baseUrl + path);
    auto session = createSession(uri, timeoutSeconds);

    auto requestPath = uri.getPathAndQuery();

    if (requestPath.empty())
    {
        requestPath = "/";
    }

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, requestPath, Poco::Net::HTTPMessage::HTTP_1_1);
    request.setContentType("application/json");
    request.setContentLength(static_cast<std::streamsize>(body.size()));
    applyHeaders(request, defaultHeaders);

    auto &os = session->sendRequest(request);
    os << body;

    Poco::Net::HTTPResponse response;
    auto &rs = session->receiveResponse(response);
    return readResponse(response, rs);
}

HttpResponse HttpClient::get(const std::string &path)
{
    Poco::URI uri(baseUrl + path);
    auto session = createSession(uri, timeoutSeconds);

    auto requestPath = uri.getPathAndQuery();

    if (requestPath.empty())
    {
        requestPath = "/";
    }

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, requestPath, Poco::Net::HTTPMessage::HTTP_1_1);
    applyHeaders(request, defaultHeaders);

    session->sendRequest(request);

    Poco::Net::HTTPResponse response;
    auto &rs = session->receiveResponse(response);
    return readResponse(response, rs);
}

void HttpClient::postStream(const std::string &path, const std::string &body, StreamCallback callback)
{
    Poco::URI uri(baseUrl + path);
    auto session = createSession(uri, timeoutSeconds);

    auto requestPath = uri.getPathAndQuery();

    if (requestPath.empty())
    {
        requestPath = "/";
    }

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, requestPath, Poco::Net::HTTPMessage::HTTP_1_1);
    request.setContentType("application/json");
    request.setContentLength(static_cast<std::streamsize>(body.size()));
    applyHeaders(request, defaultHeaders);

    auto &os = session->sendRequest(request);
    os << body;

    Poco::Net::HTTPResponse response;
    auto &rs = session->receiveResponse(response);

    auto status = static_cast<int>(response.getStatus());

    if (status < 200 || status >= 300)
    {
        std::string errorBody;
        Poco::StreamCopier::copyToString(rs, errorBody);
        throw std::runtime_error("HTTP " + std::to_string(status) + ": " + errorBody);
    }

    std::string line;

    while (std::getline(rs, line))
    {
        if (line.empty() || line == "\r")
        {
            continue;
        }

        // remove trailing carriage return
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        // parse SSE data lines
        if (line.rfind("data: ", 0) == 0)
        {
            auto data = line.substr(6);
            callback(data);
        }
    }
}

HttpResponse HttpClient::request(
    const std::string &method,
    const std::string &url,
    const std::map<std::string, std::string> &headers,
    const std::string &body,
    int timeoutSeconds,
    bool followRedirects,
    RedirectValidator redirectValidator,
    const std::string &proxy)
{
    constexpr int MAX_REDIRECTS = 5;
    std::string currentUrl = url;

    for (int redirect = 0; redirect <= MAX_REDIRECTS; ++redirect)
    {
        Poco::URI uri(currentUrl);
        auto session = createSession(uri, timeoutSeconds, proxy);

        auto requestPath = uri.getPathAndQuery();

        if (requestPath.empty())
        {
            requestPath = "/";
        }

        Poco::Net::HTTPRequest request(method, requestPath, Poco::Net::HTTPMessage::HTTP_1_1);
        request.set("Host", uri.getHost());
        applyHeaders(request, headers);

        if (!body.empty())
        {
            request.setContentLength(static_cast<std::streamsize>(body.size()));

            // default to json if no content type was set via headers
            if (request.getContentType().empty())
            {
                request.setContentType("application/json");
            }

            auto &os = session->sendRequest(request);
            os << body;
        }
        else
        {
            session->sendRequest(request);
        }

        Poco::Net::HTTPResponse response;
        auto &rs = session->receiveResponse(response);

        auto status = static_cast<int>(response.getStatus());

        // handle redirects
        if (followRedirects && status >= 300 && status < 400)
        {
            auto location = response.get("Location", "");

            if (location.empty())
            {
                return readResponse(response, rs);
            }

            // resolve relative redirects
            if (location.rfind("http", 0) != 0)
            {
                Poco::URI base(currentUrl);
                Poco::URI resolved(base, location);
                currentUrl = resolved.toString();
            }
            else
            {
                currentUrl = location;
            }

            // validate redirect destination if validator is set
            if (redirectValidator)
            {
                redirectValidator(currentUrl);
            }

            continue;
        }

        auto result = readResponse(response, rs);
        result.headers["X-Final-URL"] = currentUrl;
        return result;
    }

    throw std::runtime_error("Too many redirects (max " + std::to_string(MAX_REDIRECTS) + ")");
}

} // namespace util
} // namespace ionclaw
