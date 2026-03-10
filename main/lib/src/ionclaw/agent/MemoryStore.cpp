#include "ionclaw/agent/MemoryStore.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <sys/stat.h>

#include "spdlog/spdlog.h"

namespace fs = std::filesystem;

namespace ionclaw
{
namespace agent
{

// english stop words
const std::set<std::string> MemoryStore::STOP_WORDS = {
    // articles and determiners
    "a",
    "an",
    "the",
    "this",
    "that",
    "these",
    "those",
    // pronouns
    "i",
    "me",
    "my",
    "we",
    "our",
    "you",
    "your",
    "he",
    "she",
    "it",
    "him",
    "her",
    "they",
    "them",
    "their",
    "its",
    // common verbs
    "is",
    "are",
    "was",
    "were",
    "be",
    "been",
    "being",
    "have",
    "has",
    "had",
    "do",
    "does",
    "did",
    "will",
    "would",
    "could",
    "should",
    "can",
    "may",
    "might",
    "shall",
    // prepositions
    "in",
    "on",
    "at",
    "to",
    "for",
    "of",
    "with",
    "by",
    "from",
    "about",
    "into",
    "through",
    "during",
    "before",
    "after",
    "above",
    "below",
    "between",
    "under",
    "over",
    // conjunctions
    "and",
    "or",
    "but",
    "if",
    "then",
    "because",
    "as",
    "while",
    "when",
    "where",
    "what",
    "which",
    "who",
    "how",
    "why",
    // time references
    "yesterday",
    "today",
    "tomorrow",
    "earlier",
    "later",
    "recently",
    "ago",
    "just",
    "now",
    // vague references
    "thing",
    "things",
    "stuff",
    "something",
    "anything",
    "everything",
    "nothing",
    // misc
    "not",
    "no",
    "so",
    "than",
    "too",
    "very",
    "also",
    "only",
    "some",
    "such",
    "there",
    "all",
    "up",
    "out",
    // question/request words
    "please",
    "help",
    "find",
    "show",
    "get",
    "tell",
    "give",
};

// multilingual stop words stored as UTF-8 strings
static const std::set<std::string> STOP_WORDS_PT = {
    "o",
    "a",
    "os",
    "as",
    "um",
    "uma",
    "uns",
    "umas",
    "este",
    "esta",
    "esse",
    "essa",
    "eu",
    "me",
    "meu",
    "minha",
    "nos",
    "de",
    "do",
    "da",
    "em",
    "com",
    "por",
    "para",
    "sobre",
    "entre",
    "e",
    "ou",
    "mas",
    "se",
    "porque",
    "como",
    "foi",
    "foram",
    "ser",
    "estar",
    "ter",
    "fazer",
    "ontem",
    "hoje",
    "antes",
    "depois",
    "agora",
    "recentemente",
    "que",
    "quando",
    "onde",
    "favor",
    "ajuda",
};

static const std::set<std::string> STOP_WORDS_ES = {
    "el",
    "la",
    "los",
    "las",
    "un",
    "una",
    "unos",
    "unas",
    "este",
    "esta",
    "ese",
    "esa",
    "yo",
    "me",
    "mi",
    "tu",
    "tus",
    "usted",
    "ustedes",
    "de",
    "del",
    "en",
    "con",
    "por",
    "para",
    "sobre",
    "entre",
    "y",
    "o",
    "pero",
    "si",
    "porque",
    "como",
    "es",
    "son",
    "fue",
    "fueron",
    "ser",
    "estar",
    "haber",
    "tener",
    "hacer",
    "ayer",
    "hoy",
    "antes",
    "ahora",
    "recientemente",
    "que",
    "cuando",
    "donde",
    "favor",
    "ayuda",
};

// CJK stop words (Chinese)
static const std::set<std::string> STOP_WORDS_ZH = {
    "\xe6\x88\x91",             // 我
    "\xe4\xbd\xa0",             // 你
    "\xe4\xbb\x96",             // 他
    "\xe5\xa5\xb9",             // 她
    "\xe5\xae\x83",             // 它
    "\xe7\x9a\x84",             // 的
    "\xe4\xba\x86",             // 了
    "\xe7\x9d\x80",             // 着
    "\xe8\xbf\x87",             // 过
    "\xe5\x92\x8c",             // 和
    "\xe6\x98\xaf",             // 是
    "\xe6\x9c\x89",             // 有
    "\xe5\x9c\xa8",             // 在
    "\xe8\xbf\x99",             // 这
    "\xe9\x82\xa3",             // 那
    "\xe4\xb8\x8d",             // 不
    "\xe4\xb9\x9f",             // 也
    "\xe9\x83\xbd",             // 都
    "\xe5\xb0\xb1",             // 就
    "\xe8\xbf\x98",             // 还
    "\xe8\xa6\x81",             // 要
    "\xe8\x83\xbd",             // 能
    "\xe4\xbc\x9a",             // 会
    "\xe5\x8f\xaf\xe4\xbb\xa5", // 可以
    "\xe4\xbb\x80\xe4\xb9\x88", // 什么
    "\xe6\x80\x8e\xe4\xb9\x88", // 怎么
    "\xe8\xaf\xb7",             // 请
};

// Japanese stop words
static const std::set<std::string> STOP_WORDS_JA = {
    "\xe3\x81\x93\xe3\x82\x8c",             // これ
    "\xe3\x81\x9d\xe3\x82\x8c",             // それ
    "\xe3\x81\x82\xe3\x82\x8c",             // あれ
    "\xe3\x81\x99\xe3\x82\x8b",             // する
    "\xe3\x81\xa7\xe3\x81\x99",             // です
    "\xe3\x81\xbe\xe3\x81\x99",             // ます
    "\xe3\x81\x84\xe3\x82\x8b",             // いる
    "\xe3\x81\x82\xe3\x82\x8b",             // ある
    "\xe3\x81\xaa\xe3\x82\x8b",             // なる
    "\xe3\x81\xae",                         // の
    "\xe3\x81\x93\xe3\x81\xa8",             // こと
    "\xe3\x81\x9d\xe3\x81\x97\xe3\x81\xa6", // そして
    "\xe3\x81\x97\xe3\x81\x8b\xe3\x81\x97", // しかし
    "\xe3\x81\xbe\xe3\x81\x9f",             // また
    "\xe3\x81\xaa\xe3\x81\x9c",             // なぜ
    "\xe4\xbd\x95",                         // 何
};

// Korean stop words
static const std::set<std::string> STOP_WORDS_KO = {
    "\xec\x9d\x80",             // 은
    "\xeb\x8a\x94",             // 는
    "\xec\x9d\xb4",             // 이
    "\xea\xb0\x80",             // 가
    "\xec\x9d\x84",             // 을
    "\xeb\xa5\xbc",             // 를
    "\xec\x9d\x98",             // 의
    "\xec\x97\x90",             // 에
    "\xeb\x8f\x84",             // 도
    "\xeb\xa7\x8c",             // 만
    "\xeb\x82\x98",             // 나
    "\xec\x9a\xb0\xeb\xa6\xac", // 우리
    "\xea\xb7\xb8",             // 그
    "\xec\x9e\x88\xeb\x8b\xa4", // 있다
    "\xec\x97\x86\xeb\x8b\xa4", // 없다
    "\xed\x95\x98\xeb\x8b\xa4", // 하다
    "\xea\xb2\x83",             // 것
    "\xec\x99\x9c",             // 왜
    "\xeb\xad\x90",             // 뭐
    "\xec\xa7\x80\xea\xb8\x88", // 지금
};

// Arabic stop words
static const std::set<std::string> STOP_WORDS_AR = {
    "\xd9\x88",                                 // و
    "\xd9\x81\xd9\x8a",                         // في
    "\xd9\x85\xd9\x86",                         // من
    "\xd8\xb9\xd9\x84\xd9\x89",                 // على
    "\xd9\x87\xd8\xb0\xd8\xa7",                 // هذا
    "\xd8\xa3\xd9\x86\xd8\xa7",                 // أنا
    "\xd9\x87\xd9\x88",                         // هو
    "\xd9\x87\xd9\x8a",                         // هي
    "\xd9\x87\xd9\x85",                         // هم
    "\xd9\x83\xd8\xa7\xd9\x86",                 // كان
    "\xd8\xa5\xd9\x84\xd9\x89",                 // إلى
    "\xd9\x84\xd9\x85\xd8\xa7\xd8\xb0\xd8\xa7", // لماذا
    "\xd9\x83\xd9\x8a\xd9\x81",                 // كيف
};

bool MemoryStore::isStopWord(const std::string &token)
{
    return STOP_WORDS.count(token) > 0 ||
           STOP_WORDS_PT.count(token) > 0 ||
           STOP_WORDS_ES.count(token) > 0 ||
           STOP_WORDS_ZH.count(token) > 0 ||
           STOP_WORDS_JA.count(token) > 0 ||
           STOP_WORDS_KO.count(token) > 0 ||
           STOP_WORDS_AR.count(token) > 0;
}

// decode UTF-8 string to codepoints
std::vector<uint32_t> MemoryStore::toCodepoints(const std::string &utf8)
{
    std::vector<uint32_t> result;
    result.reserve(utf8.size());

    for (size_t i = 0; i < utf8.size();)
    {
        uint32_t cp = 0;
        auto c = static_cast<unsigned char>(utf8[i]);

        if (c < 0x80)
        {
            cp = c;
            i += 1;
        }
        else if (c < 0xE0)
        {
            if (i + 1 >= utf8.size())
                break;
            cp = (c & 0x1F) << 6 | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F);
            i += 2;
        }
        else if (c < 0xF0)
        {
            if (i + 2 >= utf8.size())
                break;
            cp = (c & 0x0F) << 12 |
                 (static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 6 |
                 (static_cast<unsigned char>(utf8[i + 2]) & 0x3F);
            i += 3;
        }
        else
        {
            if (i + 3 >= utf8.size())
                break;
            cp = (c & 0x07) << 18 |
                 (static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 12 |
                 (static_cast<unsigned char>(utf8[i + 2]) & 0x3F) << 6 |
                 (static_cast<unsigned char>(utf8[i + 3]) & 0x3F);
            i += 4;
        }

        result.push_back(cp);
    }

    return result;
}

std::string MemoryStore::codepointsToUtf8(const std::vector<uint32_t> &cps)
{
    std::string result;
    result.reserve(cps.size() * 3);

    for (auto cp : cps)
    {
        if (cp < 0x80)
        {
            result += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    return result;
}

bool MemoryStore::isCjkCodepoint(uint32_t cp)
{
    // CJK Unified Ideographs
    if (cp >= 0x4E00 && cp <= 0x9FFF)
        return true;
    // CJK Extension A
    if (cp >= 0x3400 && cp <= 0x4DBF)
        return true;
    // Hiragana
    if (cp >= 0x3040 && cp <= 0x309F)
        return true;
    // Katakana
    if (cp >= 0x30A0 && cp <= 0x30FF)
        return true;
    // Hangul syllables
    if (cp >= 0xAC00 && cp <= 0xD7AF)
        return true;
    // Hangul jamo
    if (cp >= 0x3131 && cp <= 0x3163)
        return true;
    return false;
}

MemoryStore::MemoryStore(const std::string &workspacePath)
    : memoryDir(workspacePath + "/memory")
{
    std::error_code ec;
    fs::create_directories(memoryDir, ec);

    if (ec)
    {
        spdlog::warn("[MemoryStore] Could not create memory directory '{}': {}", memoryDir, ec.message());
    }
}

std::string MemoryStore::getMemoryContext() const
{
    auto memoryPath = fs::path(memoryDir) / "MEMORY.md";

    if (!fs::exists(memoryPath) || !fs::is_regular_file(memoryPath))
    {
        return "";
    }

    try
    {
        std::ifstream file(memoryPath, std::ios::binary);

        if (!file.is_open())
        {
            return "";
        }

        std::ostringstream content;
        content << file.rdbuf();
        auto result = content.str();

        auto start = result.find_first_not_of(" \t\n\r");
        auto end = result.find_last_not_of(" \t\n\r");

        if (start == std::string::npos || end == std::string::npos)
        {
            return "";
        }

        return result.substr(start, end - start + 1);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[MemoryStore] Failed to read memory file: {}", e.what());
        return "";
    }
}

std::string MemoryStore::getHistoryContext(int maxLines) const
{
    auto historyPath = fs::path(memoryDir) / "HISTORY.md";

    if (!fs::exists(historyPath) || !fs::is_regular_file(historyPath))
    {
        return "";
    }

    try
    {
        std::ifstream file(historyPath, std::ios::binary);

        if (!file.is_open())
        {
            return "";
        }

        if (maxLines <= 0)
        {
            std::ostringstream content;
            content << file.rdbuf();
            return content.str();
        }

        std::vector<std::string> lines;
        std::string line;

        while (std::getline(file, line))
        {
            lines.push_back(line);
        }

        auto start = (static_cast<int>(lines.size()) > maxLines)
                         ? lines.size() - maxLines
                         : 0;

        std::ostringstream output;

        for (auto i = start; i < lines.size(); ++i)
        {
            output << lines[i] << "\n";
        }

        return output.str();
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[MemoryStore] Failed to read history file: {}", e.what());
        return "";
    }
}

std::vector<std::string> MemoryStore::extractKeywords(const std::string &query)
{
    auto codepoints = toCodepoints(query);
    std::vector<std::string> keywords;
    std::set<std::string> seen;

    std::vector<uint32_t> currentWord;

    auto flushWord = [&]()
    {
        if (currentWord.empty())
            return;

        auto word = codepointsToUtf8(currentWord);

        // lowercase ASCII portion
        std::string lower;
        lower.reserve(word.size());
        for (auto c : word)
        {
            auto uc = static_cast<unsigned char>(c);
            lower += static_cast<char>(uc < 0x80 ? std::tolower(uc) : uc);
        }

        currentWord.clear();

        if (lower.size() < 2)
            return;
        if (isStopWord(lower))
            return;

        // skip pure numbers
        bool allDigits = true;
        for (auto c : lower)
        {
            if (!std::isdigit(static_cast<unsigned char>(c)))
            {
                allDigits = false;
                break;
            }
        }
        if (allDigits)
            return;

        if (seen.insert(lower).second)
        {
            keywords.push_back(lower);
        }
    };

    // CJK character accumulator for bigram extraction
    std::vector<uint32_t> cjkRun;

    auto flushCjkRun = [&]()
    {
        if (cjkRun.empty())
            return;

        // emit individual characters and bigrams
        for (size_t i = 0; i < cjkRun.size(); ++i)
        {
            auto ch = codepointsToUtf8({cjkRun[i]});

            if (!isStopWord(ch) && seen.insert(ch).second)
            {
                keywords.push_back(ch);
            }

            // bigrams for better phrase matching
            if (i + 1 < cjkRun.size())
            {
                auto bigram = codepointsToUtf8({cjkRun[i], cjkRun[i + 1]});

                if (!isStopWord(bigram) && seen.insert(bigram).second)
                {
                    keywords.push_back(bigram);
                }
            }
        }

        cjkRun.clear();
    };

    for (auto cp : codepoints)
    {
        if (isCjkCodepoint(cp))
        {
            flushWord();
            cjkRun.push_back(cp);
        }
        else if ((cp < 0x80 && (std::isalnum(static_cast<int>(cp)) || cp == '_' || cp == '-' || cp == '.')) ||
                 (cp >= 0x80 && !isCjkCodepoint(cp)))
        {
            // non-CJK multibyte or ASCII word char
            flushCjkRun();
            currentWord.push_back(cp);
        }
        else
        {
            // separator
            flushCjkRun();
            flushWord();
        }
    }

    flushCjkRun();
    flushWord();

    return keywords;
}

double MemoryStore::computeTemporalDecay(const std::string &filePath, const std::string &baseDir)
{
    static thread_local const std::regex datePattern(R"((\d{4})-(\d{2})-(\d{2}))");
    auto filename = fs::path(filePath).filename().string();
    std::smatch match;

    // check for date in filename first
    if (std::regex_search(filename, match, datePattern))
    {
        try
        {
            auto year = std::stoi(match[1].str());
            auto month = std::stoi(match[2].str());
            auto day = std::stoi(match[3].str());

            std::tm tm = {};
            tm.tm_year = year - 1900;
            tm.tm_mon = month - 1;
            tm.tm_mday = day;
#if defined(_WIN32)
            auto fileTime = std::chrono::system_clock::from_time_t(_mkgmtime(&tm));
#else
            auto fileTime = std::chrono::system_clock::from_time_t(timegm(&tm));
#endif

            auto now = std::chrono::system_clock::now();
            auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - fileTime).count();
            auto ageDays = static_cast<double>(ageMs > 0 ? ageMs : 0) / (24.0 * 60 * 60 * 1000);

            // half-life of 30 days: exp(-ln2 * ageDays / 30)
            return std::exp(-0.693147 * ageDays / 30.0);
        }
        catch (...)
        {
            return 1.0;
        }
    }

    // evergreen memory files (MEMORY.md, topic files without date) do not decay
    auto basename = fs::path(filePath).filename().string();
    auto lowerBase = basename;
    std::transform(lowerBase.begin(), lowerBase.end(), lowerBase.begin(),
                   [](unsigned char c)
                   { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });

    if (lowerBase == "memory.md" || lowerBase == "history.md")
    {
        return 1.0;
    }

    // fallback: use file modification time
    try
    {
        auto absPath = fs::path(filePath).is_absolute()
                           ? fs::path(filePath)
                           : fs::path(baseDir) / filePath;

        if (!fs::exists(absPath))
        {
            return 1.0;
        }

        // use stat() for portable mtime access in C++17
        struct stat st;
        if (stat(absPath.string().c_str(), &st) != 0)
        {
            return 1.0;
        }
        auto sctp = std::chrono::system_clock::from_time_t(st.st_mtime);
        auto now = std::chrono::system_clock::now();
        auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - sctp).count();
        auto ageDays = static_cast<double>(ageMs > 0 ? ageMs : 0) / (24.0 * 60 * 60 * 1000);

        return std::exp(-0.693147 * ageDays / 30.0);
    }
    catch (...)
    {
        return 1.0;
    }
}

std::vector<MemorySearchResult> MemoryStore::searchMemory(const std::string &query) const
{
    auto keywords = extractKeywords(query);

    if (keywords.empty())
    {
        return {};
    }

    std::vector<MemorySearchResult> results;
    std::error_code ec;

    if (!fs::exists(memoryDir, ec) || !fs::is_directory(memoryDir, ec))
    {
        return {};
    }

    for (const auto &entry : fs::directory_iterator(memoryDir, ec))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c)
                       { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });

        if (ext != ".md")
        {
            continue;
        }

        auto filePath = entry.path().string();
        auto decay = computeTemporalDecay(filePath, memoryDir);

        std::ifstream file(entry.path(), std::ios::binary);

        if (!file.is_open())
        {
            continue;
        }

        std::vector<std::string> lines;
        std::string line;

        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            lines.push_back(line);
        }

        for (size_t i = 0; i < lines.size(); ++i)
        {
            auto lineLower = lines[i];
            std::transform(lineLower.begin(), lineLower.end(), lineLower.begin(),
                           [](unsigned char c)
                           { return c < 0x80 ? static_cast<unsigned char>(std::tolower(c)) : c; });

            int matched = 0;

            for (const auto &kw : keywords)
            {
                if (lineLower.find(kw) != std::string::npos)
                {
                    matched++;
                }
            }

            if (matched == 0)
            {
                continue;
            }

            std::ostringstream ctx;

            if (i > 0)
            {
                ctx << lines[i - 1] << "\n";
            }

            ctx << lines[i];

            if (i + 1 < lines.size())
            {
                ctx << "\n"
                    << lines[i + 1];
            }

            double score = (static_cast<double>(matched) / static_cast<double>(keywords.size())) * decay;

            MemorySearchResult result;
            result.file = entry.path().filename().string();
            result.line = static_cast<int>(i + 1);
            result.context = ctx.str();
            result.score = score;
            results.push_back(result);
        }
    }

    std::sort(results.begin(), results.end(), [](const auto &a, const auto &b)
              { return a.score > b.score; });

    if (results.size() > 20)
    {
        results.resize(20);
    }

    return results;
}

std::vector<ionclaw::provider::Message> MemoryStore::getConsolidationMessages(
    const ionclaw::session::Session &session, int window) const
{
    auto total = static_cast<int>(session.messages.size());
    auto start = session.lastConsolidated;
    auto unconsolidated = total - start;

    if (unconsolidated < window)
    {
        return {};
    }

    auto currentMemory = getMemoryContext();

    std::vector<ionclaw::provider::Message> messages;

    ionclaw::provider::Message systemMsg;
    systemMsg.role = "system";
    systemMsg.content =
        "You are a memory consolidation agent. "
        "Analyze the conversation below and:\n"
        "1. Write a timestamped summary for HISTORY.md\n"
        "2. Update the long-term MEMORY.md with important facts, "
        "preferences, and context about the user.\n\n"
        "Current MEMORY.md content:\n" +
        (currentMemory.empty() ? std::string("(empty)") : currentMemory) +
        "\n\nUse the memory_save tool to persist both.";
    messages.push_back(std::move(systemMsg));

    for (int i = start; i < total; ++i)
    {
        const auto &msg = session.messages[i];

        if ((msg.role == "user" || msg.role == "assistant") && !msg.content.empty())
        {
            ionclaw::provider::Message m;
            m.role = msg.role;
            m.content = msg.content;
            messages.push_back(std::move(m));
        }
    }

    return messages;
}

void MemoryStore::markConsolidated(ionclaw::session::Session &session, int count)
{
    session.lastConsolidated = count;
}

} // namespace agent
} // namespace ionclaw
