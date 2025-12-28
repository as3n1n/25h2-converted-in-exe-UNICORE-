#include <windows.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

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

    // ANSI color codes pour #FF0050
    const char* RESET = "\033[0m";
    const char* PINK = "\033[38;2;255;0;80m";      // #FF0050
    const char* CYAN = "\033[38;2;0;255;255m";     // Cyan
    const char* GREEN = "\033[38;2;0;255;100m";    // Vert
    const char* YELLOW = "\033[38;2;255;200;0m";   // Jaune
    const char* GRAY = "\033[38;2;150;150;150m";   // Gris
    const char* WHITE = "\033[38;2;255;255;255m";  // Blanc

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
     ██████╗██╗  ██╗██╗███╗   ██╗ ██████╗ 
    ██╔════╝██║  ██║██║████╗  ██║██╔═══██╗
    ██║     ███████║██║██╔██╗ ██║██║   ██║
    ██║     ██╔══██║██║██║╚██╗██║██║   ██║
    ╚██████╗██║  ██║██║██║ ╚████║╚██████╔╝
     ╚═════╝╚═╝  ╚═╝╚═╝╚═╝  ╚═══╝ ╚═════╝ 
)" << std::endl;
        std::cout << GRAY << "              25H2 Launcher" << RESET << "\n" << std::endl;
    }
};

class Launcher {
private:
    Logger log;
    fs::path exeDir;

    fs::path getExeDir() {
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        return fs::path(buffer).parent_path();
    }

    bool launch(const fs::path& path) {
        std::string cmd = "\"" + path.string() + "\" --unsafe";

        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;

        log.start("Launching: " + path.filename().string());

        if (CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, FALSE, 0, NULL, exeDir.string().c_str(), &si, &pi)) {
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
            std::cout << "\r\033[38;2;150;150;150m[" << "\033[38;2;255;0;80m" << "AUTO-CLOSE" << "\033[38;2;150;150;150m] " << "\033[38;2;255;255;255mClosing in " << i << "s...  " << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::cout << std::endl;
    }

public:
    Launcher() : exeDir(getExeDir()) {}

    int run() {
        log.banner();
        log.notice("You need to run this file before every loader start");
        std::cout << std::endl;
        log.info("Directory: " + exeDir.string());

        // Check build.exe
        fs::path buildExe = exeDir / "build.exe";
        if (fs::exists(buildExe)) {
            log.success("Found: build.exe");
            if (launch(buildExe)) {
                std::cout << std::endl;
                log.info("Open loader -> Inject as usual");
                std::cout << std::endl;
                countdown(5);
                return 0;
            }
            countdown(5);
            return 1;
        }

        log.warn("build.exe not found, searching build*.exe...");

        // Search build*.exe
        for (const auto& entry : fs::directory_iterator(exeDir)) {
            if (!entry.is_regular_file()) continue;

            std::string name = entry.path().filename().string();
            std::string ext = entry.path().extension().string();

            for (auto& c : name) c = (char)tolower(c);
            for (auto& c : ext) c = (char)tolower(c);

            if (name.rfind("build", 0) == 0 && ext == ".exe") {
                log.success("Found: " + entry.path().filename().string());
                if (launch(entry.path())) {
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

        log.error("No build.exe or build*.exe found!");
        countdown(5);
        return 1;
    }
};

int main() {
    SetConsoleTitleA("25H2");
    Launcher launcher;
    return launcher.run();
}