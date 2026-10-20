// SPDX-License-Identifier: MIT
#include "updatecore.h"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <shellapi.h>
using namespace tss::update;
void check(bool ok) { if (!ok) throw std::runtime_error("Test assertion failed"); }
void rejects(const std::function<void()>& fn) { bool failed = false; try { fn(); } catch (...) { failed = true; } check(failed); }
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--verify") { auto m = verifyManifest(readFile(fs::u8path(argv[2]))); std::cout << m.product << ' ' << m.sequence << '\n'; return 0; }
        for (const char* name : {"../escape", "/root", "a//b", "A/../b", "a\\b", "a:b", "CON.txt", "folder/NUL", "aux.dll", "COM1.txt", "TSSUpdate.lock", "TSSUpdate.manifest", "unins000.exe", ".tss-update/a", "a./b", "trailing."}) check(!validPath(name));
        for (const char* name : {"Qt6Core.dll", "platforms/qwindows.dll", "Source/TSSLogViewer-source.zip", "license-zh_TW.txt"}) check(validPath(name));
        check(compareVersion("26.10", "26.9") > 0); check(compareVersion("26.2.0", "26.2") == 0); check(compareVersion("26.1.99", "26.2") < 0);
        check(!validVersion("26.2beta")); check(!validVersion("1.65536"));
        std::string body = "TSSUPDATER/1\nproduct=tss-viewer-windows-x64\nsequence=100\nexpires=9999999999\nversion=26.2\nviewerVersion=26.2\nruntime=0.0\nnotes=dGVzdA==\n";
        const std::string hash(64, '0');
        for (const char* name : {"TSSLogViewer.exe", "TSSUpdater.exe", "VERSION.txt"}) body += "file\t1\t" + hash + "\t" + name + "\n";
        check(parseManifest(body).files.size() == 3);
        auto emptyNotes = body;
        emptyNotes.replace(emptyNotes.find("dGVzdA=="), 8, "");
        check(parseManifest(emptyNotes).notes.empty());
        rejects([&] { parseManifest(body + "file\t1\t" + hash + "\ttsslogviewer.exe\n"); });
        rejects([&] { parseManifest(body + "file\t1\t" + hash + "\tTSSUpdater.exe/child\n"); });
        rejects([&] { parseManifest(body + "file\t1\t" + hash + "\t../outside\n"); });
        rejects([&] { parseManifest(body + "file\t4294967296\t" + hash + "\tlarge.dll\n"); });
        rejects([&] { verifyManifest("invalid\n" + body); });
        check(hashBytes("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        for (const std::wstring value : {L"", L"D:\\Logs with spaces\\ride.tsslog", L"\"nested quoted path\"", L"C:\\trailing\\"}) {
            int count = 0;
            const auto command = L"test.exe " + quote(value);
            auto parsed = CommandLineToArgvW(command.c_str(), &count);
            check(parsed && count == 2 && parsed[1] == value);
            LocalFree(parsed);
        }
        std::cout << "Protocol, path validation, versions and SHA-256 checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
