#include <windows.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

bool IsRunAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

void RelaunchAsAdmin() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);

    SHELLEXECUTEINFOA sei = { sizeof(sei) };
    sei.lpVerb = "runas";
    sei.lpFile = path;
    sei.nShow = SW_SHOWNORMAL;

    ShellExecuteExA(&sei);
}

class Logger {
private:
    HANDLE hConsole;

    void enableVirtualTerminal() {
        hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode;
        GetConsoleMode(hConsole, &mode);
        SetConsoleMode(hConsole, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        SetConsoleOutputCP(CP_UTF8);
    }

    std::string getTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::tm tm_buf;
        localtime_s(&tm_buf, &time);
        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%H:%M:%S");
        ss << "." << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    const char* RESET = "\033[0m";
    const char* PINK = "\033[38;2;255;0;80m";
    const char* CYAN = "\033[38;2;0;255;255m";
    const char* GREEN = "\033[38;2;0;255;100m";
    const char* YELLOW = "\033[38;2;255;200;0m";
    const char* GRAY = "\033[38;2;150;150;150m";
    const char* WHITE = "\033[38;2;255;255;255m";

public:
    Logger() {
        enableVirtualTerminal();
    }

    void info(const std::string& msg) {
        std::cout << GRAY << "[" << getTimestamp() << "] " << PINK << "[INFO] " << WHITE << msg << RESET << std::endl;
    }

    void success(const std::string& msg) {
        std::cout << GRAY << "[" << getTimestamp() << "] " << GREEN << "[OK] " << WHITE << msg << RESET << std::endl;
    }

    void warn(const std::string& msg) {
        std::cout << GRAY << "[" << getTimestamp() << "] " << YELLOW << "[WARN] " << WHITE << msg << RESET << std::endl;
    }

    void error(const std::string& msg) {
        std::cout << GRAY << "[" << getTimestamp() << "] " << PINK << "[ERROR] " << WHITE << msg << RESET << std::endl;
    }

    void notice(const std::string& msg) {
        std::cout << GRAY << "[" << getTimestamp() << "] " << CYAN << "[NOTICE] " << WHITE << msg << RESET << std::endl;
    }

    void start(const std::string& msg) {
        std::cout << GRAY << "[" << getTimestamp() << "] " << PINK << "[START] " << WHITE << msg << RESET << std::endl;
    }

    void banner() {
        std::cout << PINK;
        std::cout << R"(
       ██████ ██   ██ ██ ███    ██  ██████  
      ██      ██   ██ ██ ████   ██ ██    ██ 
      ██      ███████ ██ ██ ██  ██ ██    ██ 
      ██      ██   ██ ██ ██  ██ ██ ██    ██ 
       ██████ ██   ██ ██ ██   ████  ██████  
)";
        std::cout << GRAY << "\n         just a free stuff, dont ask for support" << RESET << "\n" << std::endl;
    }
};

class Launcher {
private:
    Logger log;
    fs::path foundBuild;
    bool searchCancelled = false;

    std::vector<std::string> getAllDrives() {
        std::vector<std::string> drives;
        DWORD driveMask = GetLogicalDrives();

        for (char letter = 'A'; letter <= 'Z'; letter++) {
            if (driveMask & (1 << (letter - 'A'))) {
                std::string drive = std::string(1, letter) + ":\\";
                UINT type = GetDriveTypeA(drive.c_str());
                if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
                    drives.push_back(drive);
                }
            }
        }
        return drives;
    }

    bool searchDirectory(const fs::path& dir, int depth = 0) {
        if (depth > 15) return false; // Max depth

        try {
            for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied)) {
                try {
                    if (entry.is_regular_file()) {
                        std::string name = entry.path().filename().string();
                        std::string nameLower = name;
                        for (auto& c : nameLower) c = (char)tolower(c);

                        if (nameLower == "build.exe" || (nameLower.rfind("build", 0) == 0 && nameLower.size() > 5 && nameLower.substr(nameLower.size() - 4) == ".exe")) {
                            foundBuild = entry.path();
                            return true;
                        }
                    }
                    else if (entry.is_directory()) {
                        std::string dirName = entry.path().filename().string();
                        // Skip system/hidden folders
                        if (dirName[0] == '.' || dirName == "$Recycle.Bin" || dirName == "Windows" ||
                            dirName == "Program Files" || dirName == "Program Files (x86)" ||
                            dirName == "ProgramData" || dirName == "System Volume Information") {
                            continue;
                        }

                        if (searchDirectory(entry.path(), depth + 1)) {
                            return true;
                        }
                    }
                }
                catch (...) { continue; }
            }
        }
        catch (...) {}

        return false;
    }

    bool launch(const fs::path& path) {
        std::string cmd = "\"" + path.string() + "\" --unsafe";

        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;

        log.start("Launching: " + path.filename().string());

        if (CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, FALSE, 0, NULL, path.parent_path().string().c_str(), &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            log.success("Process started (PID: " + std::to_string(pi.dwProcessId) + ")");
            return true;
        }

        log.error("Failed to start (Error: " + std::to_string(GetLastError()) + ")");
        return false;
    }

    void countdown(int seconds) {
        for (int i = seconds; i > 0; i--) {
            std::cout << "\r\033[38;2;150;150;150m[\033[38;2;255;0;80mAUTO-CLOSE\033[38;2;150;150;150m] \033[38;2;255;255;255mClosing in " << i << "s...  " << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::cout << std::endl;
    }

public:
    int run() {
        log.banner();

        if (!IsRunAsAdmin()) {
            log.error("This program requires administrator privileges!");
            log.info("Requesting elevation...");
            std::this_thread::sleep_for(std::chrono::seconds(1));
            RelaunchAsAdmin();
            return 1;
        }

        log.success("Running as Administrator");
        log.notice("You need to run this file before every loader start");
        std::cout << std::endl;

        auto drives = getAllDrives();
        log.info("Scanning " + std::to_string(drives.size()) + " drive(s) for build.exe...");
        std::cout << std::endl;

        for (const auto& drive : drives) {
            log.info("Scanning " + drive + " ...");

            if (searchDirectory(drive)) {
                log.success("Found: " + foundBuild.string());

                if (launch(foundBuild)) {
                    std::cout << std::endl;
                    log.info("Open loader -> Inject as usual");
                    std::cout << std::endl;
                    countdown(5);
                    return 0;
                }
                countdown(5);
                return 1;
            }
        }

        log.error("No build.exe found on any drive!");
        countdown(5);
        return 1;
    }
};

int main() {
    SetConsoleTitleA("25H2");
    Launcher launcher;
    return launcher.run();
}
