// SPDX-License-Identifier: MIT
#include "updatecore.h"
#include <shellapi.h>
#include <shlobj.h>
#include <aclapi.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace tss::update;
namespace {
constexpr wchar_t StateDir[] = L".tss-update";
constexpr wchar_t RuntimePath[] = L"Prerequisites/vc_redist.x64.exe";
struct Child { DWORD id; HANDLE process; };
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool isApp(const std::wstring& name) { return name == L"TSSLimiterX.exe" || name == L"TSSLogViewer.exe"; }
std::wstring wide(const std::string& value) { return std::wstring(value.begin(), value.end()); }
bool elevated() {
    Handle token; TOKEN_ELEVATION value{}; DWORD size = 0;
    require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value), "Cannot inspect process token");
    require(GetTokenInformation(token.value, TokenElevation, &value, sizeof(value), &size), "Cannot inspect elevation");
    return value.TokenIsElevated != 0;
}
void safeElevatedRoot(const fs::path& root) {
    if (!elevated()) return;
    PWSTR raw = nullptr;
    require(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &raw)), "Cannot locate Program Files");
    fs::path programFiles(raw); CoTaskMemFree(raw);
    const auto rel = root.lexically_relative(programFiles);
    require(!rel.empty() && rel.native().rfind(L"..", 0) != 0 && !rel.is_absolute(), "Elevated updates require an installation inside Program Files");
    rejectReparsePoints(root);
    // Пакеты из пользовательского каталога не должны управлять повышенным процессом.
    PACL dacl = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
    require(GetNamedSecurityInfoW(const_cast<wchar_t*>(root.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &descriptor) == ERROR_SUCCESS && dacl, "Cannot inspect installation permissions");
    bool safe = true;
    for (auto kind : {WinWorldSid, WinBuiltinUsersSid, WinAuthenticatedUserSid}) {
        BYTE sid[SECURITY_MAX_SID_SIZE]{}; DWORD size = sizeof(sid);
        if (!CreateWellKnownSid(kind, nullptr, sid, &size)) { safe = false; break; }
        TRUSTEE_W trustee{}; BuildTrusteeWithSidW(&trustee, sid); ACCESS_MASK access = 0;
        if (GetEffectiveRightsFromAclW(dacl, &trustee, &access) != ERROR_SUCCESS || (access & (FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_DELETE_CHILD | WRITE_DAC | WRITE_OWNER | DELETE))) safe = false;
    }
    LocalFree(descriptor);
    require(safe, "Installation directory is writable by unprivileged users");
}
Child start(const fs::path& exe, const std::wstring& arguments, const fs::path& directory, bool inherit = false) {
    std::wstring command = quote(exe.wstring()) + L" " + arguments;
    STARTUPINFOW info{}; info.cb = sizeof(info); PROCESS_INFORMATION process{};
    require(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, inherit, CREATE_NO_WINDOW, nullptr, directory.c_str(), &info, &process), "Cannot start update process");
    CloseHandle(process.hThread); return {process.dwProcessId, process.hProcess};
}
void waitChild(Child child) { Handle process(child.process); require(WaitForSingleObject(process.value, 300000) == WAIT_OBJECT_0, "Child process timeout"); DWORD code = 0; GetExitCodeProcess(process.value, &code); require(code == 0, "Recovery failed"); }
std::wstring newToken() {
    GUID guid{}; require(SUCCEEDED(CoCreateGuid(&guid)), "Cannot create update identifier");
    wchar_t buffer[40]{}; StringFromGUID2(guid, buffer, 40); std::wstring result;
    for (wchar_t c : buffer) if (iswxdigit(c)) result += static_cast<wchar_t>(towlower(c));
    return result;
}
void runRecovery(const fs::path& root) {
    const auto exe = root / L"TSSUpdater.exe";
    const auto token = newToken(); const auto arguments = L"--recover " + token;
    SHELLEXECUTEINFOW info{}; info.cbSize = sizeof(info); info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpFile = exe.c_str(); info.lpParameters = arguments.c_str(); info.lpDirectory = root.c_str(); info.nShow = SW_HIDE;
    // В portable-каталоге повышение не нужно.
    {
        Handle probe(CreateFileW((root / L"TSSUpdate.lock").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!probe.valid() && GetLastError() == ERROR_ACCESS_DENIED) info.lpVerb = L"runas";
    }
    require(ShellExecuteExW(&info), "Recovery was cancelled or could not start");
    waitChild({0, info.hProcess});
    if (!fs::exists(root / StateDir / (L"pid-" + token + L".txt")) && !fs::exists(root / StateDir / L"journal")) return;
    const auto pid = static_cast<DWORD>(std::stoul(readFile(root / StateDir / (L"pid-" + token + L".txt"), 32)));
    Handle worker(OpenProcess(SYNCHRONIZE, FALSE, pid));
    if (worker.valid()) require(WaitForSingleObject(worker.value, 300000) == WAIT_OBJECT_0, "Recovery timed out");
    require(readFile(root / StateDir / (L"result-" + token + L".txt")) == "OK", "Recovery failed; preserve the installation and its backup");
}
void launch(const fs::path& root, const std::wstring& app, const std::wstring& extra = L"") {
    require(isApp(app), "Unknown application");
    if (fs::exists(root / StateDir / L"journal") && executablePath().parent_path() == root) {
        // Восстановление может заменить сам TSSUpdater.exe: ожидание ведётся из копии без повышения.
        wchar_t temp[32768]{}; require(GetTempPathW(32768, temp), "Cannot locate temporary directory");
        const auto copy = fs::path(temp) / (L"TSSUpdater-" + newToken() + L".exe");
        require(CopyFileW(executablePath().c_str(), copy.c_str(), TRUE), "Cannot prepare recovery launcher");
        auto child = start(copy, L"--resume-launch " + quote(root.wstring()) + L" " + quote(app) + L" " + quote(extra), root);
        CloseHandle(child.process); return;
    }
    // До загрузки Qt ждём завершения замены DLL. Приложение затем удерживает эту же блокировку.
    for (int attempt = 0; attempt < 1200; ++attempt) {
        Handle lock(CreateFileW((root / UseLock).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (lock.valid()) {
            if (fs::exists(root / StateDir / L"journal")) {
                CloseHandle(lock.value); lock.value = INVALID_HANDLE_VALUE;
                runRecovery(root); continue;
            }
            // Унаследованный handle защищает процесс уже во время загрузки Qt, ещё до main().
            require(SetHandleInformation(lock.value, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT), "Cannot inherit installation lock");
            auto child = start(root / app, extra, root, true); CloseHandle(child.process); return;
        }
        if (GetLastError() != ERROR_SHARING_VIOLATION) break;
        Sleep(100);
    }
    throw std::runtime_error("Another update is still running");
}
bool tokenValid(const std::wstring& token) { return token.size() == 32 && token.find_first_not_of(L"0123456789abcdef") == std::wstring::npos; }
void copyVerified(const fs::path& from, const fs::path& to, const File& expected) {
    rejectReparsePoints(from); rejectReparsePoints(to);
    fs::create_directories(to.parent_path());
    require(CopyFileW(from.c_str(), to.c_str(), FALSE), "Cannot prepare update file");
    require(matches(to, expected), "Downloaded file failed SHA-256 verification");
    Handle copied(CreateFileW(to.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    require(copied.valid() && FlushFileBuffers(copied.value), "Cannot flush prepared file");
}
void replaceFile(const fs::path& source, const fs::path& target) {
    rejectReparsePoints(source); rejectReparsePoints(target);
    fs::create_directories(target.parent_path());
    const fs::path temporary = target.wstring() + L".tss-new";
    rejectReparsePoints(temporary);
    require(CopyFileW(source.c_str(), temporary.c_str(), FALSE), "Cannot copy application file");
    { Handle handle(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); require(handle.valid() && FlushFileBuffers(handle.value), "Cannot flush application file"); }
    require(MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "Cannot replace application file");
}
struct Entry { bool existed; File file; };
std::vector<Entry> readJournal(const fs::path& state) {
    std::vector<Entry> entries;
    std::istringstream stream(readFile(state / L"journal")); std::string line;
    while (std::getline(stream, line)) {
        std::istringstream fields(line); std::string mode, size, hash, path;
        std::getline(fields, mode, '\t'); std::getline(fields, size, '\t'); std::getline(fields, hash, '\t'); std::getline(fields, path);
        require((mode == "R" || mode == "D") && (validPath(path) || path == "TSSUpdate.manifest"), "Invalid recovery journal");
        require(size.find_first_not_of("0123456789") == std::string::npos && !size.empty(), "Invalid journal size");
        entries.push_back({mode == "R", {path, hash, std::stoull(size)}});
        require(entries.size() < 25000, "Recovery journal too large");
    }
    require(!entries.empty(), "Empty recovery journal"); return entries;
}
void restore(const fs::path& root) {
    const auto state = root / StateDir;
    if (!fs::exists(state / L"journal")) return;
    const auto entries = readJournal(state);
    // Проверяем все резервные копии до первой операции восстановления.
    for (const auto& entry : entries) if (entry.existed) require(matches(state / L"backup" / entry.file.path, entry.file), "Recovery backup is incomplete");
    for (bool applications : {false, true}) for (const auto& entry : entries) {
        if (isApp(wide(entry.file.path)) != applications) continue;
        const auto target = root / entry.file.path;
        rejectReparsePoints(target);
        if (entry.existed) { if (!matches(target, entry.file)) replaceFile(state / L"backup" / entry.file.path, target); }
        else if (fs::exists(target)) require(DeleteFileW(target.c_str()), "Cannot remove incomplete new file");
    }
    require(DeleteFileW((state / L"journal").c_str()), "Cannot finish recovery");
}
void setInstalledVersion(const fs::path& root, const Manifest& manifest) {
    if (manifest.product != "tss-suite-windows-x64") return;
    HKEY raw = nullptr;
    const wchar_t* key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{F8610F16-0D0A-4F3C-B3D5-A5E7D9452C97}_is1";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, &raw) != ERROR_SUCCESS) return;
    wchar_t location[32768]{}; DWORD size = sizeof(location);
    if (RegQueryValueExW(raw, L"InstallLocation", nullptr, nullptr, reinterpret_cast<BYTE*>(location), &size) == ERROR_SUCCESS && fs::equivalent(fs::path(location), root)) {
        const auto version = wide(manifest.version);
        RegSetValueExW(raw, L"DisplayVersion", 0, REG_SZ, reinterpret_cast<const BYTE*>(version.c_str()), static_cast<DWORD>((version.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(raw);
}
void apply(const fs::path& root, const fs::path& cache, const std::wstring& expected) {
    const auto state = root / StateDir;
    if (fs::exists(state / L"journal")) restore(root);
    const auto envelope = readFile(cache / L"release.manifest");
    require(expected.empty() || wide(hashBytes(envelope)) == expected, "The selected release changed before installation");
    const auto next = verifyManifest(envelope);
    const auto old = verifyManifest(readFile(root / InstalledManifest), false);
    require(next.product == old.product && next.sequence > old.sequence && compareVersion(next.version, old.version) >= 0 && compareVersion(next.viewerVersion, old.viewerVersion) >= 0, "Update would downgrade or change the installed product");
    std::map<std::string, File> previous;
    for (const auto& file : old.files) previous[file.path] = file;
    std::set<std::string> names;
    std::vector<File> changed;
    std::vector<std::string> removed;
    for (const auto& file : next.files) {
        names.insert(file.path);
        if (fs::path(file.path) == RuntimePath) continue;
        const auto installed = root / file.path;
        rejectReparsePoints(installed);
        if (matches(installed, file)) continue;
        require(!fs::exists(installed) || previous.count(file.path), "Update conflicts with an unmanaged file");
        copyVerified(cache / L"blobs" / (file.sha256 + ".bin"), state / L"prepared" / file.path, file);
        changed.push_back(file);
    }
    for (const auto& file : old.files) if (!names.count(file.path) && fs::path(file.path) != RuntimePath && fs::exists(root / file.path)) removed.push_back(file.path);
    if (compareVersion(installedRuntime(), next.runtime) < 0) {
        const auto runtime = std::find_if(next.files.begin(), next.files.end(), [](const File& f) { return fs::path(f.path) == RuntimePath; });
        require(runtime != next.files.end(), "Required Visual C++ Runtime is unavailable in this update");
        const auto installer = state / L"prepared" / RuntimePath;
        copyVerified(cache / L"blobs" / (runtime->sha256 + ".bin"), installer, *runtime);
        auto child = start(installer, L"/install /quiet /norestart", installer.parent_path()); Handle process(child.process);
        require(WaitForSingleObject(process.value, 300000) == WAIT_OBJECT_0, "Visual C++ Runtime installation timed out");
        DWORD code = 0; GetExitCodeProcess(process.value, &code);
        require((code == 0 || code == 3010) && compareVersion(installedRuntime(), next.runtime) >= 0, "Visual C++ Runtime installation failed");
    }
    // Полные EXE временно убираются до замены Qt; ярлыки запускают стабильный native launcher.
    for (const auto& file : next.files) if (isApp(wide(file.path)) && std::none_of(changed.begin(), changed.end(), [&](const File& other) { return other.path == file.path; })) {
        copyVerified(root / file.path, state / L"prepared" / file.path, file); changed.push_back(file);
    }
    std::set<std::string> affected(removed.begin(), removed.end());
    for (const auto& file : changed) affected.insert(file.path);
    affected.insert("TSSUpdate.manifest");
    std::string journal;
    for (const auto& path : affected) {
        const auto installed = root / path;
        rejectReparsePoints(installed);
        if (fs::exists(installed)) {
            File backup{path, hashFile(installed), fs::file_size(installed)};
            copyVerified(installed, state / L"backup" / path, backup);
            journal += "R\t" + std::to_string(backup.size) + "\t" + backup.sha256 + "\t" + path + "\n";
        } else journal += "D\t0\t-\t" + path + "\n";
    }
    writeAtomic(state / L"journal", journal);
    try {
        for (const auto& file : changed) if (isApp(wide(file.path)) && fs::exists(root / file.path)) require(DeleteFileW((root / file.path).c_str()), "An application is still running");
        for (bool applications : {false, true}) {
            for (const auto& file : changed) if (isApp(wide(file.path)) == applications) replaceFile(state / L"prepared" / file.path, root / file.path);
            for (const auto& path : removed) if (isApp(wide(path)) == applications) require(DeleteFileW((root / path).c_str()), "Cannot remove obsolete application file");
        }
        writeAtomic(root / InstalledManifest, envelope);
        // Удаление журнала — точка фиксации. До неё любой сбой означает откат.
        require(DeleteFileW((state / L"journal").c_str()), "Cannot commit update transaction");
        setInstalledVersion(root, next);
    } catch (...) { restore(root); throw; }
}
void work(const fs::path& root, const fs::path& cache, const std::wstring& token, bool recovery, const std::wstring& expected = L"") {
    const auto state = root / StateDir;
    try {
        Handle lock;
        for (int attempt = 0; attempt < 1200; ++attempt) {
            lock.value = CreateFileW((root / UseLock).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (lock.valid()) break;
            require(GetLastError() == ERROR_SHARING_VIOLATION, "Cannot lock installation");
            Sleep(100);
        }
        require(lock.valid(), "Close both applications before updating; no processes were terminated");
        if (recovery) { restore(root); setInstalledVersion(root, verifyManifest(readFile(root / InstalledManifest), false)); }
        else apply(root, cache, expected);
        if (!token.empty()) writeAtomic(state / (L"result-" + token + L".txt"), "OK");
    } catch (const std::exception& error) {
        if (!token.empty()) writeAtomic(state / (L"result-" + token + L".txt"), error.what());
        throw;
    }
}
void startWorker(const fs::path& root, const fs::path& cache, const std::wstring& token, bool recovery, const std::wstring& expected = L"") {
    safeElevatedRoot(root);
    Handle transaction(CreateMutexW(nullptr, FALSE, L"Global\\TSSUpdaterInstallTransaction"));
    const auto mutexError = GetLastError();
    require(transaction.valid() && mutexError != ERROR_ALREADY_EXISTS, "Another installer or updater is running");
    require(SetHandleInformation(transaction.value, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT), "Cannot inherit update mutex");
    const auto state = root / StateDir;
    rejectReparsePoints(state / L"runner" / L"TSSUpdater.exe");
    fs::create_directories(state / L"runner");
    const auto worker = state / L"runner" / L"TSSUpdater.exe";
    require(CopyFileW(executablePath().c_str(), worker.c_str(), FALSE), "Another updater is already running");
    const auto args = (recovery ? L"--worker-recover " : L"--worker ") + quote(cache.wstring()) + L" " + quote(token) + L" " + std::to_wstring(GetCurrentProcessId()) + L" " + quote(expected);
    const auto child = start(worker, args, root, true);
    writeAtomic(state / (L"pid-" + token + L".txt"), std::to_string(child.id)); CloseHandle(child.process);
}
void uninstallOwned(const fs::path& root) {
    if (!fs::exists(root / InstalledManifest)) return;
    // В неподдерживаемом для обновлений каталоге достаточно штатного удаления Inno.
    try { safeElevatedRoot(root); } catch (...) { return; }
    const auto manifest = verifyManifest(readFile(root / InstalledManifest), false);
    Handle lock(CreateFileW((root / UseLock).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    require(lock.valid(), "Close applications before uninstalling");
    for (const auto& file : manifest.files) {
        if (file.path == "TSSUpdater.exe" || fs::path(file.path) == RuntimePath) continue;
        const auto target = root / file.path; rejectReparsePoints(target);
        if (fs::exists(target)) require(DeleteFileW(target.c_str()), "Cannot remove managed application file");
    }
}
void watch(const fs::path& root, const std::wstring& token, const std::wstring& app) {
    require(!elevated(), "The restart watcher must run as the original user");
    const auto state = root / StateDir;
    for (int i = 0; i < 6000; ++i) {
        if (fs::exists(state / (L"result-" + token + L".txt"))) { launch(root, app, L"--tss-update-result " + token); return; }
        const auto pidPath = state / (L"pid-" + token + L".txt");
        if (fs::exists(pidPath)) {
            const auto pid = static_cast<DWORD>(std::stoul(readFile(pidPath, 32)));
            Handle process(OpenProcess(SYNCHRONIZE, FALSE, pid));
            if (!process.valid() || WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) {
                if (fs::exists(state / L"journal")) runRecovery(root);
                launch(root, app, L"--tss-update-result " + token); return;
            }
        }
        Sleep(100);
    }
    throw std::runtime_error("Update watcher timed out; run TSSUpdater to recover the installation");
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0; wchar_t** raw = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!raw) return 1;
    std::vector<std::wstring> args(raw, raw + argc); LocalFree(raw);
    try {
        SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
        const auto executable = executablePath(); auto root = executable.parent_path();
        if (argc >= 2 && (args[1] == L"--worker" || args[1] == L"--worker-recover")) {
            require(argc == 6 && root.filename() == L"runner" && root.parent_path().filename() == StateDir, "Invalid worker location");
            root = root.parent_path().parent_path(); safeElevatedRoot(root);
            require(tokenValid(args[3]), "Invalid update token");
            Handle parent(OpenProcess(SYNCHRONIZE, FALSE, std::stoul(args[4])));
            if (parent.valid()) require(WaitForSingleObject(parent.value, 10000) == WAIT_OBJECT_0, "Update launcher did not exit");
            work(root, args[2], args[3], args[1] == L"--worker-recover", args[5]); return 0;
        }
        if (argc == 5 && args[1] == L"--watch") { require(tokenValid(args[3]), "Invalid update token"); watch(fs::path(args[2]), args[3], args[4]); return 0; }
        if (argc == 5 && args[1] == L"--resume-launch") { launch(fs::path(args[2]), args[3], args[4]); return 0; }
        rejectReparsePoints(root);
        if (argc == 2 && args[1] == L"--uninstall-clean") { uninstallOwned(root); return 0; }
        if (argc >= 3 && args[1] == L"--launch") {
            require(argc <= 4, "Invalid launch arguments");
            launch(root, args[2], argc == 4 ? quote(args[3]) : L""); return 0;
        }
        if ((argc == 4 || argc == 5) && args[1] == L"--apply") { require(tokenValid(args[3]), "Invalid update token"); startWorker(root, args[2], args[3], false, argc == 5 ? args[4] : L""); return 0; }
        if (argc == 1) { launch(root, fs::exists(root / L"TSSLimiterX.exe") ? L"TSSLimiterX.exe" : L"TSSLogViewer.exe"); return 0; }
        if ((argc == 2 || argc == 3) && args[1] == L"--recover") {
            const auto token = argc == 3 ? args[2] : newToken(); require(tokenValid(token), "Invalid recovery token");
            if (fs::exists(root / StateDir / L"journal")) startWorker(root, root, token, true);
            return 0;
        }
        throw std::runtime_error("Invalid TSSUpdater arguments");
    } catch (const std::exception& error) {
        // Диагностика аварийного native-запуска доступна и при повреждённых библиотеках Qt.
        if (argc < 2 || (args[1] != L"--worker" && args[1] != L"--worker-recover")) MessageBoxA(nullptr, error.what(), "TSSUpdater", MB_OK | MB_ICONERROR);
        return 1;
    }
}
