#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace Poco
{
class URI;
namespace Net
{
class HTTPClientSession;
class HTTPRequest;
class HTTPResponse;
} // namespace Net
} // namespace Poco

namespace ionclaw
{
namespace util
{

struct HttpResponse
{
    int statusCode = 0;
    std::string body;
    std::map<std::string, std::string> headers;
};

using StreamCallback = std::function<void(const std::string &chunk)>;
using CancelPredicate = std::function<bool()>;

class HttpClient
{
public:
    explicit HttpClient(const std::string &baseUrl, int timeoutSeconds = 60);

    void setHeader(const std::string &key, const std::string &value);

    HttpResponse post(const std::string &path, const std::string &body);
    HttpResponse get(const std::string &path);

    void postStream(const std::string &path, const std::string &body, StreamCallback callback, const CancelPredicate &isCancelled = {});

    using RedirectValidator = std::function<void(const std::string &url)>;

    static HttpResponse request(const std::string &method, const std::string &url, const std::map<std::string, std::string> &headers = {}, const std::string &body = "", int timeoutSeconds = 30, bool followRedirects = true, RedirectValidator redirectValidator = nullptr, const std::string &proxy = "");

    static std::unique_ptr<Poco::Net::HTTPClientSession> createSession(const Poco::URI &uri, int timeoutSeconds, const std::string &proxy = "");

private:
    std::string baseUrl;
    int timeoutSeconds;
    std::map<std::string, std::string> defaultHeaders;
    static void applyHeaders(Poco::Net::HTTPRequest &request, const std::map<std::string, std::string> &headers);
    static HttpResponse readResponse(Poco::Net::HTTPResponse &response, std::istream &responseStream);
};

} // namespace util
} // namespace ionclaw
