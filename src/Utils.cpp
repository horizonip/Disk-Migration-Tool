#include "Utils.h"
#include <string>

namespace Utils {

std::wstring FormatSize(uint64_t bytes) {
    const wchar_t* units[] = { L"bytes", L"KB", L"MB", L"GB", L"TB" };
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    wchar_t buf[64];
    if (unitIndex == 0) {
        swprintf_s(buf, L"%llu bytes", bytes);
    } else {
        swprintf_s(buf, L"%.1f %s", size, units[unitIndex]);
    }
    return buf;
}

std::wstring FormatSizeShort(uint64_t bytes) {
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    wchar_t buf[32];
    if (unitIndex == 0) {
        swprintf_s(buf, L"%llu B", bytes);
    } else if (size < 10.0) {
        swprintf_s(buf, L"%.2f %s", size, units[unitIndex]);
    } else if (size < 100.0) {
        swprintf_s(buf, L"%.1f %s", size, units[unitIndex]);
    } else {
        swprintf_s(buf, L"%.0f %s", size, units[unitIndex]);
    }
    return buf;
}

std::wstring CombinePaths(const std::wstring& base, const std::wstring& relative) {
    if (base.empty()) return relative;
    if (relative.empty()) return base;

    std::wstring result = base;
    if (result.back() != L'\\' && result.back() != L'/') {
        result += L'\\';
    }
    return result + relative;
}

bool EnsureDirectoryExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }

    // Find parent
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos && pos > 0) {
        std::wstring parent = path.substr(0, pos);
        // Don't try to create drive root
        if (parent.size() > 2 || (parent.size() == 2 && parent[1] != L':')) {
            if (!EnsureDirectoryExists(parent)) {
                return false;
            }
        }
    }

    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

void WriteLogLine(HANDLE hFile, const std::wstring& line) {
    if (hFile == INVALID_HANDLE_VALUE) return;
    int needed = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return;
    std::string utf8(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), &utf8[0], needed, nullptr, nullptr);
    utf8 += "\r\n";
    DWORD written;
    WriteFile(hFile, utf8.c_str(), (DWORD)utf8.size(), &written, nullptr);
}

std::wstring JsonEscape(const std::wstring& s) {
    std::wstring result;
    result.reserve(s.size() + 8);
    for (wchar_t ch : s) {
        switch (ch) {
        case L'"':  result += L"\\\""; break;
        case L'\\': result += L"\\\\"; break;
        case L'\n': result += L"\\n"; break;
        case L'\r': result += L"\\r"; break;
        case L'\t': result += L"\\t"; break;
        default:
            if (ch < 0x20) {
                wchar_t buf[8];
                swprintf_s(buf, L"\\u%04X", (unsigned)ch);
                result += buf;
            } else {
                result += ch;
            }
            break;
        }
    }
    return result;
}

std::wstring JsonUnescape(const std::wstring& s) {
    std::wstring result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            i++;
            switch (s[i]) {
            case L'"':  result += L'"'; break;
            case L'\\': result += L'\\'; break;
            case L'/':  result += L'/'; break;
            case L'n':  result += L'\n'; break;
            case L'r':  result += L'\r'; break;
            case L't':  result += L'\t'; break;
            case L'u':
                if (i + 4 < s.size()) {
                    wchar_t hex[5] = { s[i+1], s[i+2], s[i+3], s[i+4], 0 };
                    unsigned long val = wcstoul(hex, nullptr, 16);
                    result += static_cast<wchar_t>(val);
                    i += 4;
                }
                break;
            default: result += s[i]; break;
            }
        } else {
            result += s[i];
        }
    }
    return result;
}

} // namespace Utils
