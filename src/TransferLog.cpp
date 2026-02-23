#include "TransferLog.h"
#include "Utils.h"
#include <string>
#include <vector>

TransferLog::TransferLog() {}

TransferLog::~TransferLog() {
    EndLogging();
}

std::wstring TransferLog::FormatSerial(DWORD serial) {
    wchar_t buf[16];
    swprintf_s(buf, L"%08X", serial);
    return buf;
}

bool TransferLog::Load(const std::wstring& exeDir, const std::wstring& serialHex) {
    Clear();

    std::wstring logsDir = Utils::CombinePaths(exeDir, L"logs");
    logFilePath_ = Utils::CombinePaths(logsDir, L"DSplit_" + serialHex + L".log");

    // Read entire file as bytes
    HANDLE hFile = CreateFileW(logFilePath_.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return fileSize == 0; // empty file counts as "exists"
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
    if (wideLen <= 0) return true; // file exists but empty/unreadable

    std::wstring content(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, buf.data() + start,
        (int)(bytesRead - start), &content[0], wideLen);

    // Parse lines
    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find_first_of(L"\r\n", pos);
        if (eol == std::wstring::npos) eol = content.size();

        std::wstring line = content.substr(pos, eol - pos);

        // Skip past \r\n or \n
        pos = eol;
        if (pos < content.size() && content[pos] == L'\r') pos++;
        if (pos < content.size() && content[pos] == L'\n') pos++;

        // Skip comment and empty lines
        if (line.empty() || line[0] == L'#') continue;

        paths_.insert(line);
    }

    return true;
}

bool TransferLog::Contains(const std::wstring& relativePath) const {
    return paths_.count(relativePath) > 0;
}

bool TransferLog::BeginLogging(const std::wstring& exeDir, const std::wstring& serialHex,
                                const std::wstring& volumeName, const std::wstring& driveLetter) {
    // Just store the path; Migration::Run() handles all file I/O
    std::wstring logsDir = Utils::CombinePaths(exeDir, L"logs");
    logFilePath_ = Utils::CombinePaths(logsDir, L"DSplit_" + serialHex + L".log");
    return true;
}

void TransferLog::AppendEntry(const std::wstring& relativePath, bool isDirectory) {
    if (!logFile_) return;

    if (isDirectory) {
        fwprintf(logFile_, L"%s\\\n", relativePath.c_str());
    } else {
        fwprintf(logFile_, L"%s\n", relativePath.c_str());
    }
    fflush(logFile_);

    // Also add to in-memory set
    if (isDirectory) {
        paths_.insert(relativePath + L"\\");
    } else {
        paths_.insert(relativePath);
    }
}

void TransferLog::EndLogging() {
    if (logFile_) {
        fclose(logFile_);
        logFile_ = nullptr;
    }
}

void TransferLog::AddExclusion(const std::wstring& relativePath) {
    paths_.insert(relativePath);
}

const std::unordered_set<std::wstring>& TransferLog::GetLoggedPaths() const {
    return paths_;
}

void TransferLog::Clear() {
    EndLogging();
    paths_.clear();
    logFilePath_.clear();
}
