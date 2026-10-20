// SPDX-License-Identifier: MIT
#include "updatecore.h"
#include "trustedkey.h"
#include <bcrypt.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace tss::update {
namespace {
void require(bool condition, const char* error) { if (!condition) throw std::runtime_error(error); }
struct Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE handle = nullptr;
    Hash() {
        require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "SHA-256 provider unavailable");
        if (BCryptCreateHash(algorithm, &handle, nullptr, 0, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("Cannot create SHA-256 hash");
        }
    }
    ~Hash() { BCryptDestroyHash(handle); BCryptCloseAlgorithmProvider(algorithm, 0); }
    void add(const void* data, size_t size) {
        require(size <= MAXDWORD && BCryptHashData(handle, (PUCHAR)data, static_cast<ULONG>(size), 0) >= 0, "Hash failed");
    }
    std::array<unsigned char, 32> finish() {
        std::array<unsigned char, 32> bytes{};
        require(BCryptFinishHash(handle, bytes.data(), static_cast<ULONG>(bytes.size()), 0) >= 0, "Hash failed");
        return bytes;
    }
};
std::string hex(const std::array<unsigned char, 32>& bytes) {
    std::string out;
    for (unsigned char b : bytes) { out += "0123456789abcdef"[b >> 4]; out += "0123456789abcdef"[b & 15]; }
    return out;
}
uint64_t number(const std::string& value) {
    require(!value.empty() && value.size() <= 16 && std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }), "Invalid integer");
    return std::stoull(value);
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
}

void rejectReparsePoints(const fs::path& path) {
    auto absolute = fs::absolute(path).lexically_normal();
    fs::path current;
    for (const auto& component : absolute) {
        current /= component;
        const DWORD attrs = GetFileAttributesW(current.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) require(!(attrs & FILE_ATTRIBUTE_REPARSE_POINT), "Reparse points are not allowed");
        else require(GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND, "Cannot inspect update path");
    }
}

std::string readFile(const fs::path& path, size_t limit) {
    rejectReparsePoints(path);
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    require(file.valid(), "Cannot read file");
    LARGE_INTEGER length{};
    require(GetFileSizeEx(file.value, &length) && length.QuadPart >= 0 && static_cast<uint64_t>(length.QuadPart) <= limit, "File exceeds size limit");
    std::string bytes(static_cast<size_t>(length.QuadPart), '\0');
    DWORD count = 0;
    require(ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) && count == bytes.size(), "Incomplete file read");
    return bytes;
}

void writeAtomic(const fs::path& path, const std::string& bytes) {
    rejectReparsePoints(path);
    const fs::path temporary = path.wstring() + L".new";
    rejectReparsePoints(temporary);
    {
        Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        require(file.valid(), "Cannot write update state");
        DWORD count = 0;
        require(WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) && count == bytes.size() && FlushFileBuffers(file.value), "Cannot flush update state");
    }
    require(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "Cannot commit update state");
}

std::string hashFile(const fs::path& path) {
    rejectReparsePoints(path);
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    require(file.valid(), "Cannot hash file");
    Hash hash;
    std::array<unsigned char, 128 * 1024> buffer{};
    DWORD count = 0;
    for (;;) {
        require(ReadFile(file.value, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr), "File hash read failed");
        if (!count) break;
        hash.add(buffer.data(), count);
    }
    return hex(hash.finish());
}
std::string hashBytes(const std::string& bytes) { Hash hash; hash.add(bytes.data(), bytes.size()); return hex(hash.finish()); }

std::vector<unsigned char> unbase64(const std::string& value) {
    if (value.empty()) return {};
    require(value.size() <= MaxManifestSize, "Base64 exceeds size limit");
    DWORD length = 0;
    require(CryptStringToBinaryA(value.data(), static_cast<DWORD>(value.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, nullptr, &length, nullptr, nullptr), "Invalid base64");
    std::vector<unsigned char> result(length);
    require(CryptStringToBinaryA(value.data(), static_cast<DWORD>(value.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, result.data(), &length, nullptr, nullptr), "Invalid base64");
    result.resize(length);
    return result;
}

bool validPath(const std::string& path) {
    if (path.empty() || path.size() > 180 || path.front() == '/' || path.back() == '/' || path.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-/.") != std::string::npos) return false;
    std::istringstream parts(path);
    std::string part;
    while (std::getline(parts, part, '/')) {
        if (part.empty() || part.front() == '.' || part.back() == '.') return false;
        const auto stem = lower(part.substr(0, part.find('.')));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || std::regex_match(stem, std::regex("(com|lpt)[0-9]"))) return false;
    }
    const auto name = lower(path);
    return name != "tssupdate.manifest" && name != "tssupdate.lock" && name.rfind("unins", 0) != 0;
}
bool validVersion(const std::string& value) {
    if (!std::regex_match(value, std::regex("[0-9]{1,5}(\\.[0-9]{1,5}){1,3}"))) return false;
    std::istringstream stream(value); std::string part;
    while (std::getline(stream, part, '.')) if (std::stoi(part) > 65535) return false;
    return true;
}
int compareVersion(const std::string& a, const std::string& b) {
    require(validVersion(a) && validVersion(b), "Invalid version");
    std::istringstream left(a), right(b);
    for (int i = 0; i != 4; ++i) {
        std::string x, y; std::getline(left, x, '.'); std::getline(right, y, '.');
        const int xv = x.empty() ? 0 : std::stoi(x), yv = y.empty() ? 0 : std::stoi(y);
        if (xv != yv) return xv > yv ? 1 : -1;
    }
    return 0;
}

Manifest parseManifest(const std::string& payload) {
    require(payload.size() <= MaxManifestSize && payload.find('\r') == std::string::npos && payload.find('\0') == std::string::npos, "Invalid manifest encoding");
    std::istringstream stream(payload); std::string line;
    std::getline(stream, line); require(line == "TSSUPDATER/1", "Unsupported update protocol");
    Manifest result;
    auto field = [&](const char* key) {
        require(static_cast<bool>(std::getline(stream, line)), "Missing manifest field");
        std::string prefix = std::string(key) + '=';
        require(line.rfind(prefix, 0) == 0, "Unexpected manifest field");
        return line.substr(prefix.size());
    };
    result.product = field("product");
    require(result.product == "tss-suite-windows-x64" || result.product == "tss-viewer-windows-x64", "Unknown update product");
    result.sequence = number(field("sequence"));
    require(result.sequence > 0, "Invalid release sequence");
    result.expires = number(field("expires"));
    result.version = field("version"); result.viewerVersion = field("viewerVersion"); result.runtime = field("runtime");
    require(validVersion(result.version) && validVersion(result.viewerVersion) && validVersion(result.runtime), "Invalid manifest version");
    const auto notes = unbase64(field("notes"));
    require(notes.size() <= 64 * 1024, "Release notes too large");
    result.notes.assign(notes.begin(), notes.end());
    std::set<std::string> names;
    uint64_t total = 0;
    while (std::getline(stream, line)) {
        require(line.rfind("file\t", 0) == 0, "Unexpected file entry");
        std::istringstream entry(line.substr(5));
        std::string size, hash, path;
        std::getline(entry, size, '\t'); std::getline(entry, hash, '\t'); std::getline(entry, path);
        require(validPath(path) && names.insert(lower(path)).second, "Unsafe or duplicate file path");
        require(hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == std::string::npos, "Invalid SHA-256");
        const auto length = number(size);
        require(length <= MaxFileSize && (total += length) <= MaxPackageSize && result.files.size() < 20000, "Package exceeds size limit");
        result.files.push_back({path, hash, length});
    }
    require(names.count("tsslogviewer.exe") && names.count("tssupdater.exe") && names.count("version.txt"), "Incomplete application package");
    if (result.product == "tss-suite-windows-x64") require(names.count("tsslimiterx.exe") && names.count("tsslogviewer-version.txt"), "Incomplete suite package");
    else require(!names.count("tsslimiterx.exe"), "Viewer package contains configurator");
    // Файл не может одновременно быть каталогом другого файла.
    for (const auto& name : names) for (size_t p = name.find('/'); p != std::string::npos; p = name.find('/', p + 1)) require(!names.count(name.substr(0, p)), "File/directory collision");
    return result;
}

Manifest verifyManifest(const std::string& envelope, bool checkExpiry) {
    require(envelope.size() <= MaxManifestSize, "Manifest exceeds size limit");
    const auto newline = envelope.find('\n');
    require(newline != std::string::npos && newline <= 1024, "Missing release signature");
    auto signature = unbase64(envelope.substr(0, newline));
    const auto payload = envelope.substr(newline + 1);
    auto publicKey = unbase64(TssUpdatePublicKey);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0) >= 0, "RSA provider unavailable");
    BCRYPT_KEY_HANDLE key = nullptr;
    const auto imported = BCryptImportKeyPair(algorithm, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key, publicKey.data(), static_cast<ULONG>(publicKey.size()), 0);
    if (imported < 0) { BCryptCloseAlgorithmProvider(algorithm, 0); throw std::runtime_error("Update signing key is not configured"); }
    Hash hash; hash.add(payload.data(), payload.size()); auto digest = hash.finish();
    BCRYPT_PSS_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM, 32};
    const auto verified = BCryptVerifySignature(key, &padding, digest.data(), static_cast<ULONG>(digest.size()), signature.data(), static_cast<ULONG>(signature.size()), BCRYPT_PAD_PSS);
    BCryptDestroyKey(key); BCryptCloseAlgorithmProvider(algorithm, 0);
    require(verified >= 0, "Release signature is invalid");
    auto result = parseManifest(payload);
    if (checkExpiry) {
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        require(result.expires > static_cast<uint64_t>(now), "Release metadata has expired");
    }
    return result;
}
bool matches(const fs::path& path, const File& file) {
    if (!fs::exists(path)) return false;
    rejectReparsePoints(path);
    return fs::is_regular_file(path) && fs::file_size(path) == file.size && hashFile(path) == file.sha256;
}
fs::path executablePath() {
    std::wstring path(32768, L'\0');
    DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    require(count && count < path.size(), "Cannot locate updater"); path.resize(count); return path;
}
std::wstring quote(const std::wstring& value) {
    require(value.find(L'\n') == std::wstring::npos && value.find(L'\0') == std::wstring::npos, "Invalid command argument");
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (const auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\'); slashes = 0;
        if (c == L'"') out += L'\\';
        out += c;
    }
    // Экранирование совместимо с CommandLineToArgvW, в том числе для вложенного аргумента.
    out.append(slashes * 2, L'\\');
    return out + L'"';
}
std::string installedRuntime() {
    wchar_t version[128]{}; DWORD size = sizeof(version), installed = 0, valueSize = sizeof(installed);
    const wchar_t* key = L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64";
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"Installed", RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &installed, &valueSize) != ERROR_SUCCESS || installed != 1 ||
        RegGetValueW(HKEY_LOCAL_MACHINE, key, L"Version", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, version, &size) != ERROR_SUCCESS) return "0.0";
    std::wstring value(version); if (!value.empty() && value[0] == L'v') value.erase(0, 1);
    std::string result;
    for (wchar_t c : value) { if (c > 127) return "0.0"; result += static_cast<char>(c); }
    return validVersion(result) ? result : "0.0";
}
}
