#include <iostream>
#include <string>
#include <vector>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <shellapi.h>
#include <urlmon.h> // For URL encoding

// define httplib as cpp httplib
#include <vector>
#include <mutex>
#include <atomic>
#include "../external/httplib.h"
#include "DiskManager.h"
#include "Resource.h"

// Globals for progress tracking
std::atomic<int> g_progress_percent(0);
std::mutex g_progress_mutex;
std::string g_progress_text = "";

#pragma comment(lib, "urlmon.lib")

std::string UrlEncode(const std::string& value) {
    // A simple URL encoder
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
        std::string::value_type c = (*i);
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase << '%' << std::setw(2) << int((unsigned char)c) << std::nouppercase;
        }
    }
    return escaped.str();
}

void HandleUnhandledException(const std::exception& e) {
#ifndef _DEBUG
    std::string errorMsg = e.what();
    MessageBoxA(NULL, ("A critical error occurred:\n" + errorMsg).c_str(), "Webconventer USB Error", MB_ICONERROR | MB_OK);

    std::string issueUrl = "https://github.com/Lorien-Git/WebConventer-USB/issues/new";
    std::string title = UrlEncode("Crash Report: " + errorMsg.substr(0, 50));
    std::string body = UrlEncode("The application crashed with the following error:\n\n```\n" + errorMsg + "\n```");
    
    std::string fullUrl = issueUrl + "?title=" + title + "&body=" + body;
    ShellExecuteA(NULL, "open", fullUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
    
    ExitProcess(1);
#else
    std::cerr << "Unhandled Exception: " << e.what() << std::endl;
#endif
}

std::string GetRequestJsonField(const std::string& body, const std::string& field) {
    // Very naive JSON parsing for {"disk": "E"}
    std::string search = "\"" + field + "\":";
    size_t pos = body.find(search);
    if (pos == std::string::npos) return "";
    pos += search.length();
    while (pos < body.length() && (body[pos] == ' ' || body[pos] == '"')) pos++;
    size_t endPos = body.find("\"", pos);
    if (endPos == std::string::npos) return "";
    return body.substr(pos, endPos - pos);
}

int main() {
#ifndef _DEBUG
    // In Release mode we hide the console (if it was somehow allocated)
    ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif

    try {
        httplib::Server svr;

        // Serve embedded static files
        auto serveStatic = [](const httplib::Request& req, httplib::Response& res) {
            std::string path = req.path;
            if (path == "/") path = "/index.html";

            auto it = g_Resources.find(path);
            if (it != g_Resources.end()) {
                res.set_content(reinterpret_cast<const char*>(it->second.data), it->second.size, it->second.mime_type);
            } else {
                res.status = 404;
                res.set_content("Not Found", "text/plain");
            }
        };

        svr.Get("/", serveStatic);
        svr.Get("/index.html", serveStatic);
        svr.Get("/style.css", serveStatic);
        svr.Get("/script.js", serveStatic);

        svr.set_exception_handler([](const auto& req, auto& res, std::exception_ptr ep) {
            auto fmt = "<h1>Error 500</h1><p>%s</p>";
            char buf[BUFSIZ];
            try {
                std::rethrow_exception(ep);
            } catch (std::exception &e) {
                snprintf(buf, sizeof(buf), fmt, e.what());
            } catch (...) {
                snprintf(buf, sizeof(buf), fmt, "Unknown Exception");
            }
            res.set_content(buf, "text/html");
            res.status = 500;
        });

        svr.Get("/api/disks", [](const httplib::Request& /*req*/, httplib::Response& res) {
            auto drives = DiskManager::GetRemovableDrives();
            std::string json = "[";
            for (size_t i = 0; i < drives.size(); ++i) {
                json += "{\"letter\":\"" + drives[i].letter + "\", \"name\":\"" + drives[i].name + "\", \"fs\":\"" + drives[i].fs + "\"}";
                if (i < drives.size() - 1) json += ",";
            }
            json += "]";
            res.set_content(json, "application/json");
        });

        svr.Get("/api/progress", [](const httplib::Request&, httplib::Response& res) {
            int percent = g_progress_percent.load();
            std::string text;
            {
                std::lock_guard<std::mutex> lock(g_progress_mutex);
                text = g_progress_text;
            }
            // Escape quotes in text
            std::string escText;
            for (char c : text) {
                if (c == '"' || c == '\\') escText += '\\';
                escText += c;
            }
            std::string json = "{\"percent\":" + std::to_string(percent) + ", \"text\":\"" + escText + "\"}";
            res.set_content(json, "application/json");
        });

        svr.Get("/api/version", [](const httplib::Request&, httplib::Response& res) {
            res.set_content("{\"version\":\"1.0.0\", \"github\":\"https://github.com/Lorien-Git/WebConventer-USB\"}", "application/json");
        });

        auto handleAction = [](const httplib::Request& req, httplib::Response& res, auto actionFunc) {
            std::string disk = GetRequestJsonField(req.body, "disk");
            if (disk.empty()) {
                res.set_content("{\"success\":false, \"error\":\"No disk specified\"}", "application/json");
                return;
            }
            
            // Reset progress
            g_progress_percent = 0;
            {
                std::lock_guard<std::mutex> lock(g_progress_mutex);
                g_progress_text = "Starting...";
            }

            std::string errorMsg;
            bool success = actionFunc(disk, errorMsg);
            if (success) {
                res.set_content("{\"success\":true}", "application/json");
            } else {
                std::string escError;
                for (char c : errorMsg) {
                    if (c == '"' || c == '\\') escError += '\\';
                    escError += c;
                }
                res.set_content("{\"success\":false, \"error\":\"" + escError + "\"}", "application/json");
            }
        };

        svr.Post("/api/backup", [&](const httplib::Request& req, httplib::Response& res) {
            handleAction(req, res, DiskManager::BackupDrive);
        });

        svr.Post("/api/restore", [&](const httplib::Request& req, httplib::Response& res) {
            handleAction(req, res, DiskManager::RestoreDrive);
        });

        svr.Post("/api/format/exfat", [&](const httplib::Request& req, httplib::Response& res) {
            handleAction(req, res, DiskManager::FormatDriveFat);
        });

        svr.Post("/api/format/ext4", [&](const httplib::Request& req, httplib::Response& res) {
            handleAction(req, res, DiskManager::FormatDriveExt4);
        });

        svr.Post("/api/crash", [&](const httplib::Request&, httplib::Response&) {
            // For testing the release crash handler
            throw std::runtime_error("Simulated crash for testing GitHub issue creation.");
        });

        int port = 48080;
        
        std::cout << "========================================" << std::endl;
        std::cout << " Webconventer USB is running!" << std::endl;
        std::cout << " Web interface hosted at: http://localhost:" << port << std::endl;
        std::cout << "========================================" << std::endl;

        // Open browser automatically
        ShellExecuteA(NULL, "open", ("http://localhost:" + std::to_string(port)).c_str(), NULL, NULL, SW_SHOWNORMAL);

        if (!svr.listen("0.0.0.0", port)) {
            throw std::runtime_error("Failed to start server on port " + std::to_string(port));
        }

    } catch (const std::exception& e) {
        HandleUnhandledException(e);
    } catch (...) {
        HandleUnhandledException(std::runtime_error("Unknown exception occurred."));
    }

    return 0;
}

#ifndef _DEBUG
// Windows subsystem entry point for Release mode to hide console
int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow) {
    return main();
}
#endif
