#include "TransferLog.h"
#include "Utils.h"
#include <string>
#include <vector>

TransferLog::TransferLog() {}
TransferLog::~TransferLog() {}

std::wstring TransferLog::FormatSerial(DWORD serial) {
    wchar_t buf[16];
    swprintf_s(buf, L"%08X", serial);
    return buf;
}

std::wstring TransferLog::HashSourcePath(const std::wstring& path) {
    // FNV-1a 64-bit hash on the lowercased wide chars
    uint64_t hash = 14695981039346656037ULL;
    for (wchar_t ch : path) {
        wchar_t lower = (ch >= L'A' && ch <= L'Z') ? (ch + 32) : ch;
        // Hash both bytes of wchar_t
        hash ^= static_cast<uint64_t>(lower & 0xFF);
        hash *= 1099511628211ULL;
        hash ^= static_cast<uint64_t>((lower >> 8) & 0xFF);
        hash *= 1099511628211ULL;
    }
    wchar_t buf[20];
    swprintf_s(buf, L"%016llX", hash);
    return buf;
}

std::wstring TransferLog::GetLogPath(const std::wstring& exeDir, const std::wstring& sourcePath) {
    std::wstring logsDir = Utils::CombinePaths(exeDir, L"logs");
    return Utils::CombinePaths(logsDir, L"DSplit_" + HashSourcePath(sourcePath) + L".json");
}

// --- Simple JSON helpers for fixed schema ---

static void SkipWS(const std::wstring& s, size_t& pos) {
    while (pos < s.size() && (s[pos] == L' ' || s[pos] == L'\t' ||
           s[pos] == L'\r' || s[pos] == L'\n'))
        pos++;
}

static bool Expect(const std::wstring& s, size_t& pos, wchar_t ch) {
    SkipWS(s, pos);
    if (pos < s.size() && s[pos] == ch) { pos++; return true; }
    return false;
}

static std::wstring ParseString(const std::wstring& s, size_t& pos) {
    SkipWS(s, pos);
    if (pos >= s.size() || s[pos] != L'"') return L"";
    pos++; // skip opening quote
    std::wstring result;
    while (pos < s.size() && s[pos] != L'"') {
        if (s[pos] == L'\\' && pos + 1 < s.size()) {
            pos++;
            switch (s[pos]) {
            case L'"':  result += L'"'; break;
            case L'\\': result += L'\\'; break;
            case L'/':  result += L'/'; break;
            case L'n':  result += L'\n'; break;
            case L't':  result += L'\t'; break;
            case L'r':  result += L'\r'; break;
            default:    result += s[pos]; break;
            }
        } else {
            result += s[pos];
        }
        pos++;
    }
    if (pos < s.size()) pos++; // skip closing quote
    return result;
}

static uint64_t ParseNumber(const std::wstring& s, size_t& pos) {
    SkipWS(s, pos);
    uint64_t val = 0;
    while (pos < s.size() && s[pos] >= L'0' && s[pos] <= L'9') {
        val = val * 10 + (s[pos] - L'0');
        pos++;
    }
    return val;
}

// Skip a JSON value (string, number, object, array)
static void SkipValue(const std::wstring& s, size_t& pos) {
    SkipWS(s, pos);
    if (pos >= s.size()) return;
    if (s[pos] == L'"') {
        ParseString(s, pos);
    } else if (s[pos] == L'{') {
        int depth = 1; pos++;
        while (pos < s.size() && depth > 0) {
            if (s[pos] == L'{') depth++;
            else if (s[pos] == L'}') depth--;
            else if (s[pos] == L'"') { ParseString(s, pos); continue; }
            pos++;
        }
    } else if (s[pos] == L'[') {
        int depth = 1; pos++;
        while (pos < s.size() && depth > 0) {
            if (s[pos] == L'[') depth++;
            else if (s[pos] == L']') depth--;
            else if (s[pos] == L'"') { ParseString(s, pos); continue; }
            pos++;
        }
    } else {
        // number, true, false, null
        while (pos < s.size() && s[pos] != L',' && s[pos] != L'}' && s[pos] != L']'
               && s[pos] != L' ' && s[pos] != L'\t' && s[pos] != L'\r' && s[pos] != L'\n')
            pos++;
    }
}

bool TransferLog::Load(const std::wstring& logPath) {
    Clear();

    HANDLE hFile = CreateFileW(logPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return false;
    }

    std::vector<char> buf(fileSize);
    DWORD bytesRead;
    if (!ReadFile(hFile, buf.data(), fileSize, &bytesRead, nullptr)) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    // Skip UTF-8 BOM if present
    size_t start = 0;
    if (bytesRead >= 3 &&
        (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB &&
        (unsigned char)buf[2] == 0xBF) {
        start = 3;
    }

    // Convert UTF-8 to wide string
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, buf.data() + start,
        (int)(bytesRead - start), nullptr, 0);
    if (wideLen <= 0) return false;

    std::wstring content(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, buf.data() + start,
        (int)(bytesRead - start), &content[0], wideLen);

    // Parse JSON: { "source": "...", "transfers": [ {...}, ... ] }
    size_t pos = 0;
    if (!Expect(content, pos, L'{')) return false;

    while (pos < content.size()) {
        SkipWS(content, pos);
        if (pos < content.size() && content[pos] == L'}') break;

        // Skip comma between keys
        if (pos < content.size() && content[pos] == L',') pos++;

        std::wstring key = ParseString(content, pos);
        if (!Expect(content, pos, L':')) break;

        if (key == L"source") {
            sourcePath_ = ParseString(content, pos);
        } else if (key == L"transfers") {
            if (!Expect(content, pos, L'[')) break;

            while (pos < content.size()) {
                SkipWS(content, pos);
                if (pos < content.size() && content[pos] == L']') { pos++; break; }
                if (pos < content.size() && content[pos] == L',') pos++;

                // Parse transfer object
                if (!Expect(content, pos, L'{')) break;

                TransferEntry entry;
                while (pos < content.size()) {
                    SkipWS(content, pos);
                    if (pos < content.size() && content[pos] == L'}') { pos++; break; }
                    if (pos < content.size() && content[pos] == L',') pos++;

                    std::wstring field = ParseString(content, pos);
                    if (!Expect(content, pos, L':')) break;

                    if (field == L"path") {
                        entry.relativePath = ParseString(content, pos);
                    } else if (field == L"serial") {
                        entry.serialHex = ParseString(content, pos);
                    } else if (field == L"size") {
                        entry.size = ParseNumber(content, pos);
                    } else {
                        SkipValue(content, pos);
                    }
                }

                if (!entry.relativePath.empty()) {
                    pathMap_[entry.relativePath] = entry.serialHex;
                    entries_.push_back(std::move(entry));
                }
            }
        } else {
            SkipValue(content, pos);
        }
    }

    return true;
}

bool TransferLog::Save(const std::wstring& logPath) const {
    // Ensure parent directory exists
    size_t sep = logPath.find_last_of(L"\\/");
    if (sep != std::wstring::npos) {
        Utils::EnsureDirectoryExists(logPath.substr(0, sep));
    }

    // Build JSON string
    std::wstring json;
    json += L"{\n";
    json += L"  \"source\": \"" + Utils::JsonEscape(sourcePath_) + L"\",\n";
    json += L"  \"transfers\": [\n";

    for (size_t i = 0; i < entries_.size(); i++) {
        const auto& e = entries_[i];
        json += L"    {\"path\": \"" + Utils::JsonEscape(e.relativePath) +
                L"\", \"serial\": \"" + e.serialHex +
                L"\", \"size\": ";
        wchar_t sizeBuf[32];
        swprintf_s(sizeBuf, L"%llu", e.size);
        json += sizeBuf;
        json += L"}";
        if (i + 1 < entries_.size()) json += L",";
        json += L"\n";
    }

    json += L"  ]\n";
    json += L"}\n";

    // Convert to UTF-8
    int needed = WideCharToMultiByte(CP_UTF8, 0, json.c_str(), (int)json.size(),
        nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return false;

    std::string utf8(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, json.c_str(), (int)json.size(),
        &utf8[0], needed, nullptr, nullptr);

    // Write file
    HANDLE hFile = CreateFileW(logPath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    // Write UTF-8 BOM
    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    DWORD written;
    WriteFile(hFile, bom, 3, &written, nullptr);

    WriteFile(hFile, utf8.c_str(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(hFile);
    return true;
}

bool TransferLog::Contains(const std::wstring& relativePath) const {
    return pathMap_.count(relativePath) > 0;
}

std::wstring TransferLog::GetSerial(const std::wstring& relativePath) const {
    auto it = pathMap_.find(relativePath);
    return (it != pathMap_.end()) ? it->second : L"";
}

void TransferLog::AddEntry(const std::wstring& relativePath, const std::wstring& serialHex, uint64_t size) {
    // Update map (overwrite if duplicate path)
    pathMap_[relativePath] = serialHex;

    // Check if entry already exists (update it)
    for (auto& e : entries_) {
        if (e.relativePath == relativePath) {
            e.serialHex = serialHex;
            e.size = size;
            return;
        }
    }

    entries_.push_back({ relativePath, serialHex, size });
}

void TransferLog::Clear() {
    sourcePath_.clear();
    entries_.clear();
    pathMap_.clear();
}
