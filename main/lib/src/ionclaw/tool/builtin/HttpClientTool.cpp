#include "ionclaw/tool/builtin/HttpClientTool.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "Poco/HMACEngine.h"
#include "Poco/Net/FilePartSource.h"
#include "Poco/Net/HTMLForm.h"
#include "Poco/Net/HTTPClientSession.h"
#include "Poco/Net/HTTPRequest.h"
#include "Poco/Net/HTTPResponse.h"
#include "Poco/SHA1Engine.h"
#include "Poco/StreamCopier.h"
#include "Poco/URI.h"

#ifdef IONCLAW_HAS_SSL
#include "Poco/Net/HTTPSClientSession.h"
#endif

#include "ionclaw/config/Config.hpp"
#include "ionclaw/tool/builtin/ToolHelper.hpp"
#include "ionclaw/util/Base64.hpp"
#include "ionclaw/util/HttpClient.hpp"
#include "ionclaw/util/MimeType.hpp"
#include "ionclaw/util/SsrfGuard.hpp"
#include "ionclaw/util/StringHelper.hpp"

namespace ionclaw
{
namespace tool
{
namespace builtin
{

// maximum response body size before truncation
const size_t HttpClientTool::MAX_RESPONSE_BYTES = 50 * 1024; // 50KB

// percent-encode per rfc 5849 (unreserved: A-Z a-z 0-9 - . _ ~)
std::string HttpClientTool::percentEncode(const std::string &value)
{
    std::ostringstream out;
    out.fill('0');
    out << std::hex << std::uppercase;

    for (unsigned char c : value)
    {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~')
        {
            out << c;
        }
        else
        {
            out << '%' << std::setw(2) << static_cast<int>(c);
        }
    }

    return out.str();
}

// generate random nonce for oauth1
std::string HttpClientTool::generateNonce()
{
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);

    std::string nonce;
    nonce.reserve(32);

    for (int i = 0; i < 32; ++i)
    {
        nonce += chars[dist(gen)];
    }

    return nonce;
}

// build oauth1 authorization header per rfc 5849
std::string HttpClientTool::buildOAuth1Header(
    const std::string &method,
    const std::string &url,
    const std::string &consumerKey,
    const std::string &consumerSecret,
    const std::string &accessToken,
    const std::string &tokenSecret,
    const std::vector<std::pair<std::string, std::string>> &bodyParams)
{
    auto timestamp = std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    auto nonce = generateNonce();

    // collect oauth params
    std::vector<std::pair<std::string, std::string>> oauthParams = {
        {"oauth_consumer_key", consumerKey},
        {"oauth_nonce", nonce},
        {"oauth_signature_method", "HMAC-SHA1"},
        {"oauth_timestamp", timestamp},
        {"oauth_token", accessToken},
        {"oauth_version", "1.0"},
    };

    // collect all params for signature base string (oauth + body params)
    std::vector<std::pair<std::string, std::string>> allParams = oauthParams;
    allParams.insert(allParams.end(), bodyParams.begin(), bodyParams.end());

    // also include query params from the url
    Poco::URI uri(url);

    for (const auto &qp : uri.getQueryParameters())
    {
        allParams.emplace_back(qp.first, qp.second);
    }

    // sort params lexicographically
    std::sort(allParams.begin(), allParams.end());

    // build normalized parameter string
    std::ostringstream paramStr;
    bool first = true;

    for (const auto &[key, value] : allParams)
    {
        if (!first)
        {
            paramStr << "&";
        }

        paramStr << percentEncode(key) << "=" << percentEncode(value);
        first = false;
    }

    // build base url (scheme + host + path, no query)
    std::string baseUrl = uri.getScheme() + "://" + uri.getHost();

    if ((uri.getScheme() == "http" && uri.getPort() != 80) ||
        (uri.getScheme() == "https" && uri.getPort() != 443))
    {
        baseUrl += ":" + std::to_string(uri.getPort());
    }

    baseUrl += uri.getPath();

    // build signature base string: METHOD&URL&PARAMS
    std::string baseString = method + "&" + percentEncode(baseUrl) + "&" + percentEncode(paramStr.str());

    // signing key: consumer_secret&token_secret
    std::string signingKey = percentEncode(consumerSecret) + "&" + percentEncode(tokenSecret);

    // compute hmac-sha1
    Poco::HMACEngine<Poco::SHA1Engine> hmac(signingKey);
    hmac.update(baseString);
    auto digest = hmac.digest();

    std::string signature = ionclaw::util::Base64::encode(digest.data(), digest.size());

    // build authorization header
    std::ostringstream header;
    header << "OAuth "
           << "oauth_consumer_key=\"" << percentEncode(consumerKey) << "\", "
           << "oauth_nonce=\"" << percentEncode(nonce) << "\", "
           << "oauth_signature=\"" << percentEncode(signature) << "\", "
           << "oauth_signature_method=\"HMAC-SHA1\", "
           << "oauth_timestamp=\"" << timestamp << "\", "
           << "oauth_token=\"" << percentEncode(accessToken) << "\", "
           << "oauth_version=\"1.0\"";

    return header.str();
}

// apply auth profile to request headers
void HttpClientTool::applyAuth(
    const std::string &profileName,
    const ionclaw::config::Config &config,
    const std::string &method,
    const std::string &url,
    std::map<std::string, std::string> &headers,
    const std::string &body,
    const std::string &contentType)
{
    auto it = config.credentials.find(profileName);

    if (it == config.credentials.end())
    {
        throw std::runtime_error("auth profile not found: " + profileName);
    }

    const auto &cred = it->second;

    // oauth1 auth
    if (cred.type == "oauth1")
    {
        auto consumerKey = cred.raw.value("consumer_key", "");
        auto consumerSecret = cred.raw.value("consumer_secret", "");
        auto accessToken = cred.raw.value("access_token", "");
        auto accessTokenSecret = cred.raw.value("access_token_secret", "");

        // include form-encoded body params in signature per rfc 5849
        std::vector<std::pair<std::string, std::string>> bodyParams;

        if (!body.empty() && contentType == "form")
        {
            Poco::URI dummy("?" + body);

            for (const auto &qp : dummy.getQueryParameters())
            {
                bodyParams.emplace_back(qp.first, qp.second);
            }
        }

        headers["Authorization"] = buildOAuth1Header(
            method, url, consumerKey, consumerSecret,
            accessToken, accessTokenSecret, bodyParams);
    }

    // bearer token auth
    else if (cred.type == "bearer")
    {
        if (!cred.token.empty())
        {
            headers["Authorization"] = "Bearer " + cred.token;
        }
        else if (!cred.key.empty())
        {
            headers["Authorization"] = "Bearer " + cred.key;
        }
    }

    // basic auth
    else if (cred.type == "basic")
    {
        std::string credentials = cred.username + ":" + cred.password;
        headers["Authorization"] = "Basic " + ionclaw::util::Base64::encode(credentials);
    }

    // simple key auth
    else if (cred.type == "simple")
    {
        if (!cred.key.empty())
        {
            headers["Authorization"] = cred.key;
        }
    }

    // custom header auth
    else if (cred.type == "header")
    {
        auto headerName = cred.raw.value("header_name", "");
        auto headerValue = cred.raw.value("value", "");

        if (!headerName.empty() && !headerValue.empty())
        {
            headers[headerName] = headerValue;
        }
    }

    // unsupported auth type
    else
    {
        throw std::runtime_error("unsupported auth type: " + cred.type);
    }
}

// execute http request
ToolResult HttpClientTool::execute(const nlohmann::json &params, const ToolContext &context)
{
    auto method = params.at("method").get<std::string>();
    auto url = params.at("url").get<std::string>();

    // ssrf validation
    try
    {
        ionclaw::util::SsrfGuard::validateUrl(url);
    }
    catch (const std::exception &e)
    {
        return "Error: " + std::string(e.what());
    }

    // collect headers
    std::map<std::string, std::string> headers;
    headers["User-Agent"] = "IonClaw/1.0";

    if (params.contains("headers") && params["headers"].is_object())
    {
        for (auto &[key, value] : params["headers"].items())
        {
            if (value.is_string())
            {
                headers[key] = value.get<std::string>();
            }
        }
    }

    // body and content type (extracted before auth for oauth1 signature)
    std::string body;
    std::string contentTypeParam = "json";

    if (params.contains("body") && params["body"].is_string())
    {
        body = params["body"].get<std::string>();
    }

    if (params.contains("content_type") && params["content_type"].is_string())
    {
        contentTypeParam = params["content_type"].get<std::string>();
    }

    // apply auth profile
    if (params.contains("auth") && params["auth"].is_string() && context.config)
    {
        try
        {
            applyAuth(params["auth"].get<std::string>(), *context.config,
                      method, url, headers, body, contentTypeParam);
        }
        catch (const std::exception &e)
        {
            return "Error: " + std::string(e.what());
        }
    }

    // set content-type header
    if (params.contains("content_type") && params["content_type"].is_string())
    {
        auto ct = params["content_type"].get<std::string>();

        if (ct == "json")
        {
            headers["Content-Type"] = "application/json";
        }
        else if (ct == "form")
        {
            headers["Content-Type"] = "application/x-www-form-urlencoded";
        }
        else if (ct == "text")
        {
            headers["Content-Type"] = "text/plain";
        }
        else if (ct == "xml")
        {
            headers["Content-Type"] = "application/xml";
        }
    }

    // timeout
    int timeout = 30;

    if (params.contains("timeout") && params["timeout"].is_number_integer())
    {
        timeout = std::max(1, std::min(300, params["timeout"].get<int>()));
    }

    // redirect behavior
    bool followRedirects = true;

    if (params.contains("follow_redirects") && params["follow_redirects"].is_boolean())
    {
        followRedirects = params["follow_redirects"].get<bool>();
    }

    // download path
    if (params.contains("download_path") && params["download_path"].is_string())
    {
        auto downloadPath = params["download_path"].get<std::string>();
        auto resolvedPath = ToolHelper::validateAndResolvePath(context.workspacePath, downloadPath, context.publicPath);

        try
        {
            auto response = ionclaw::util::HttpClient::request(
                method, url, headers, body, timeout, followRedirects,
                ionclaw::util::SsrfGuard::validateUrl);

            if (response.statusCode < 200 || response.statusCode >= 400)
            {
                return "Error: HTTP " + std::to_string(response.statusCode);
            }

            // ensure parent directory exists
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(resolvedPath).parent_path(), ec);

            std::ofstream outFile(resolvedPath, std::ios::binary);

            if (!outFile.is_open())
            {
                return "Error: cannot write to: " + downloadPath;
            }

            outFile << response.body;
            outFile.close();

            nlohmann::json result = {
                {"status", response.statusCode},
                {"downloaded", downloadPath},
                {"bytes", response.body.size()},
            };

            return result.dump(2);
        }
        catch (const std::exception &e)
        {
            return "Error: download failed: " + std::string(e.what());
        }
    }

    // upload file via multipart form data
    if (params.contains("upload_file") && params["upload_file"].is_string())
    {
        auto uploadFile = params["upload_file"].get<std::string>();
        auto resolvedUpload = ToolHelper::validateAndResolvePath(context.workspacePath, uploadFile, context.publicPath);

        if (!std::filesystem::exists(resolvedUpload))
        {
            return "Error: upload file not found: " + uploadFile;
        }

        if (!std::filesystem::is_regular_file(resolvedUpload))
        {
            return "Error: not a file: " + uploadFile;
        }

        std::string fieldName = "file";

        if (params.contains("upload_field") && params["upload_field"].is_string())
        {
            fieldName = params["upload_field"].get<std::string>();
        }

        try
        {
            Poco::URI uri(url);
            auto scheme = uri.getScheme();
            auto host = uri.getHost();
            auto port = uri.getPort();
            auto path = uri.getPathAndQuery();

            if (path.empty())
            {
                path = "/";
            }

            std::unique_ptr<Poco::Net::HTTPClientSession> session;

#ifdef IONCLAW_HAS_SSL
            if (scheme == "https")
            {
                session = std::make_unique<Poco::Net::HTTPSClientSession>(host, port);
            }
            else
#endif
            {
                session = std::make_unique<Poco::Net::HTTPClientSession>(host, port);
            }

            session->setTimeout(Poco::Timespan(timeout, 0));

            Poco::Net::HTTPRequest request(method, path, Poco::Net::HTTPMessage::HTTP_1_1);
            request.set("Host", host);

            for (const auto &[key, value] : headers)
            {
                request.set(key, value);
            }

            // detect mime type for the uploaded file
            auto mimeType = ionclaw::util::MimeType::forPath(resolvedUpload);

            Poco::Net::HTMLForm form;
            form.setEncoding(Poco::Net::HTMLForm::ENCODING_MULTIPART);
            form.addPart(fieldName, new Poco::Net::FilePartSource(resolvedUpload, mimeType));
            form.prepareSubmit(request);

            form.write(session->sendRequest(request));

            Poco::Net::HTTPResponse httpResp;
            auto &rs = session->receiveResponse(httpResp);
            std::string respBody;
            Poco::StreamCopier::copyToString(rs, respBody);

            bool truncated = false;

            if (respBody.size() > MAX_RESPONSE_BYTES)
            {
                respBody = ionclaw::util::StringHelper::utf8SafeTruncate(respBody, MAX_RESPONSE_BYTES);
                truncated = true;
            }

            nlohmann::json result = {
                {"status", static_cast<int>(httpResp.getStatus())},
                {"body", respBody},
                {"truncated", truncated},
            };

            return result.dump(2);
        }
        catch (const std::exception &e)
        {
            return "Error: upload failed: " + std::string(e.what());
        }
    }

    // standard request
    try
    {
        auto response = ionclaw::util::HttpClient::request(
            method, url, headers, body, timeout, followRedirects,
            ionclaw::util::SsrfGuard::validateUrl);

        bool truncated = false;
        auto responseBody = response.body;

        if (responseBody.size() > MAX_RESPONSE_BYTES)
        {
            responseBody = ionclaw::util::StringHelper::utf8SafeTruncate(responseBody, MAX_RESPONSE_BYTES);
            truncated = true;
        }

        nlohmann::json responseHeaders = nlohmann::json::object();

        for (const auto &[key, value] : response.headers)
        {
            if (key != "X-Final-URL")
            {
                responseHeaders[key] = value;
            }
        }

        nlohmann::json result = {
            {"status", response.statusCode},
            {"headers", responseHeaders},
            {"body", responseBody},
            {"truncated", truncated},
        };

        return result.dump(2);
    }
    catch (const std::exception &e)
    {
        return "Error: request failed: " + std::string(e.what());
    }
}

// schema definition
ToolSchema HttpClientTool::schema() const
{
    return {
        "http_client",
        "Make HTTP requests with full control over method, headers, body, and authentication. "
        "Use auth parameter with a configured profile name for authenticated requests (bearer, basic, simple, header).",
        {{"type", "object"},
         {"properties",
          {{"method", {{"type", "string"}, {"enum", nlohmann::json::array({"GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"})}, {"description", "HTTP method"}}},
           {"url", {{"type", "string"}, {"description", "Request URL"}}},
           {"headers", {{"type", "object"}, {"description", "Request headers as key-value pairs"}}},
           {"body", {{"type", "string"}, {"description", "Request body"}}},
           {"content_type", {{"type", "string"}, {"enum", nlohmann::json::array({"json", "form", "text", "xml"})}, {"description", "Body content type (default: json)"}}},
           {"auth", {{"type", "string"}, {"description", "Auth profile name from config for authenticated requests"}}},
           {"timeout", {{"type", "integer"}, {"description", "Timeout in seconds (default: 30)"}}},
           {"follow_redirects", {{"type", "boolean"}, {"description", "Follow redirects (default: true)"}}},
           {"download_path", {{"type", "string"}, {"description", "Save response body to this file path instead of returning it"}}},
           {"upload_file", {{"type", "string"}, {"description", "Path to a file to upload as multipart form data"}}},
           {"upload_field", {{"type", "string"}, {"description", "Form field name for the uploaded file (default: file)"}}}}},
         {"required", nlohmann::json::array({"method", "url"})}}};
}

} // namespace builtin
} // namespace tool
} // namespace ionclaw
