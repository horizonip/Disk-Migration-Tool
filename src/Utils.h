#pragma once
#include <windows.h>
#include <string>
#include <cstdint>

namespace Utils {

// Format a byte count as a human-readable string (e.g. "1.23 GB")
std::wstring FormatSize(uint64_t bytes);

// Format a byte count as a short string without decimals for small values
std::wstring FormatSizeShort(uint64_t bytes);

// Combine a base path and a relative path with backslash separator
std::wstring CombinePaths(const std::wstring& base, const std::wstring& relative);

// Ensure a directory exists, creating intermediate directories as needed
bool EnsureDirectoryExists(const std::wstring& path);

// Write a wide string as UTF-8 + CRLF to a file handle
void WriteLogLine(HANDLE hFile, const std::wstring& line);

// Escape a wide string for JSON (backslash, quote, control chars)
std::wstring JsonEscape(const std::wstring& s);

// Unescape a JSON string (reverse of JsonEscape)
std::wstring JsonUnescape(const std::wstring& s);

} // namespace Utils
