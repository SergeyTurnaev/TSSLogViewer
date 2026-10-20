// SPDX-License-Identifier: MIT
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

namespace tss::update {
namespace fs = std::filesystem;
constexpr size_t MaxManifestSize = 4 * 1024 * 1024;
constexpr uint64_t MaxFileSize = 1024ull * 1024 * 1024;
constexpr uint64_t MaxPackageSize = 4ull * 1024 * 1024 * 1024;
constexpr wchar_t InstalledManifest[] = L"TSSUpdate.manifest";
constexpr wchar_t UseLock[] = L"TSSUpdate.lock";

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE v = INVALID_HANDLE_VALUE) : value(v) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool valid() const { return value != INVALID_HANDLE_VALUE && value != nullptr; }
};
struct File {
    std::string path;
    std::string sha256;
    uint64_t size = 0;
};
struct Manifest {
    std::string product;
    std::string version;
    std::string viewerVersion;
    std::string runtime;
    std::string notes;
    uint64_t sequence = 0;
    uint64_t expires = 0;
    std::vector<File> files;
};

std::string readFile(const fs::path& path, size_t limit = MaxManifestSize);
void writeAtomic(const fs::path& path, const std::string& bytes);
std::string hashFile(const fs::path& path);
std::string hashBytes(const std::string& bytes);
std::vector<unsigned char> unbase64(const std::string& value);
Manifest parseManifest(const std::string& payload);
Manifest verifyManifest(const std::string& envelope, bool checkExpiry = true);
bool validPath(const std::string& path);
bool validVersion(const std::string& value);
int compareVersion(const std::string& a, const std::string& b);
void rejectReparsePoints(const fs::path& path);
bool matches(const fs::path& path, const File& file);
fs::path executablePath();
std::wstring quote(const std::wstring& value);
std::string installedRuntime();
}
