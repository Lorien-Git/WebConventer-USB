#include "DiskManager.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <filesystem>
#include <iostream>
#include <fstream>

// Miniz for ZIP
#include "../external/miniz.h"
#include <ShlObj.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include "Resource.h"

extern "C" {
    bool FormatDriveExt4Native(const char* volume_name, char* errorMsg, size_t errorMsgSize, void (*progress_cb)(int, const char*));
}

extern std::atomic<int> g_progress_percent;
extern std::mutex g_progress_mutex;
extern std::string g_progress_text;

void FormatProgressCallback(int percent, const char* text) {
    g_progress_percent = percent;
    {
        std::lock_guard<std::mutex> lock(g_progress_mutex);
        g_progress_text = text ? text : "";
    }
    std::cout << "[PROGRESS " << percent << "%] " << (text ? text : "") << std::endl;
}

namespace fs = std::filesystem;

static bool CheckIfExt4(const std::string& driveLetter, std::string& outVolName) {
    std::string devPath = "\\\\.\\" + driveLetter + ":";
    HANDLE hDev = CreateFileA(devPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    LARGE_INTEGER offset;
    offset.QuadPart = 1024;
    if (!SetFilePointerEx(hDev, offset, NULL, FILE_BEGIN)) {
        CloseHandle(hDev);
        return false;
    }
    
    uint8_t sb[1024] = { 0 };
    DWORD bytesRead = 0;
    if (ReadFile(hDev, sb, sizeof(sb), &bytesRead, NULL) && bytesRead >= 120) {
        uint16_t magic = *(uint16_t*)&sb[56];
        if (magic == 0xEF53) {
            char volName[17] = { 0 };
            memcpy(volName, &sb[120], 16);
            volName[16] = '\0';
            outVolName = volName;
            CloseHandle(hDev);
            return true;
        }
    }
    CloseHandle(hDev);
    return false;
}

std::vector<DiskInfo> DiskManager::GetRemovableDrives() {
    std::vector<DiskInfo> drives;
    DWORD drivesBitMask = GetLogicalDrives();
    
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        if (drivesBitMask & (1 << (letter - 'A'))) {
            std::string root = std::string(1, letter) + ":\\";
            if (GetDriveTypeA(root.c_str()) == DRIVE_REMOVABLE) {
                char volumeName[MAX_PATH + 1] = { 0 };
                char fileSystemName[MAX_PATH + 1] = { 0 };
                if (GetVolumeInformationA(root.c_str(), volumeName, MAX_PATH + 1, nullptr, nullptr, nullptr, fileSystemName, MAX_PATH + 1)) {
                    drives.push_back({ std::string(1, letter), volumeName, fileSystemName });
                } else {
                    std::string extVolName;
                    if (CheckIfExt4(std::string(1, letter), extVolName)) {
                        drives.push_back({ std::string(1, letter), extVolName.empty() ? "Linux USB" : extVolName, "EXT4" });
                    } else {
                        drives.push_back({ std::string(1, letter), "USB Drive", "RAW / Unknown" });
                    }
                }
            }
        }
    }
    return drives;
}

std::string DiskManager::GetDownloadsFolder() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path))) {
        return std::string(path) + "\\Downloads";
    }
    return "C:\\";
}

std::string DiskManager::GetDrivePath(const std::string& driveLetter) {
    return driveLetter + ":\\";
}

bool DiskManager::BackupDrive(const std::string& driveLetter, std::string& errorMsg) {
    std::string rootPath = GetDrivePath(driveLetter);
    std::error_code ec;
    if (!fs::exists(rootPath, ec)) {
        if (ec) {
            // If there's an OS error (like unrecognized volume/RAW/EXT4), skip backup safely
            return true;
        }
        errorMsg = "Drive not found.";
        return false;
    }

    // Prepare zip filename
    SYSTEMTIME st;
    GetLocalTime(&st);
    char filename[256];
    sprintf_s(filename, "\\backup_%s_%04d%02d%02d_%02d%02d%02d.webcnv", 
              driveLetter.c_str(), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    
    std::string outPath = GetDownloadsFolder() + filename;

    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));
    if (!mz_zip_writer_init_file(&zip_archive, outPath.c_str(), 0)) {
        errorMsg = "Failed to create archive file in Downloads.";
        return false;
    }

    try {
        std::vector<fs::path> allFiles;
        for (const auto& entry : fs::recursive_directory_iterator(rootPath, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            if (entry.is_regular_file(ec) && !ec) {
                std::string relPath = entry.path().lexically_relative(rootPath).string();
                // Skip Windows system directories
                if (relPath.rfind("System Volume Information", 0) == 0 || 
                    relPath.rfind("$RECYCLE.BIN", 0) == 0 ||
                    relPath.rfind("$Recycle.Bin", 0) == 0) {
                    continue;
                }
                allFiles.push_back(entry.path());
            }
        }
        
        if (allFiles.empty()) {
            std::cout << "[BACKUP] No user files found to backup." << std::endl;
            mz_zip_writer_end(&zip_archive);
            return true;
        }

        for (size_t i = 0; i < allFiles.size(); ++i) {
            std::string pathStr = allFiles[i].string();
            std::string relPath = allFiles[i].lexically_relative(rootPath).string();
            
            // Standardize ZIP path separators to forward slash
            std::string zipEntryName = relPath;
            for (char& c : zipEntryName) {
                if (c == '\\') c = '/';
            }

            {
                std::lock_guard<std::mutex> lock(g_progress_mutex);
                g_progress_text = "Backing up (" + std::to_string(i + 1) + "/" + std::to_string(allFiles.size()) + "): " + relPath;
            }
            g_progress_percent = (int)(((i + 1) * 100) / allFiles.size());
            std::cout << "[BACKUP] (" << (i + 1) << "/" << allFiles.size() << ") " << relPath << std::endl;
            
            // Stream file directly into archive (safe for large files, zero huge memory allocations)
            if (!mz_zip_writer_add_file(&zip_archive, zipEntryName.c_str(), pathStr.c_str(), NULL, 0, MZ_DEFAULT_COMPRESSION)) {
                std::cerr << "[BACKUP WARNING] Failed to add " << relPath << " to archive (file might be in use)." << std::endl;
            }
        }
    } catch (const std::exception& e) {
        mz_zip_writer_end(&zip_archive);
        errorMsg = e.what();
        return false;
    }

    mz_zip_writer_finalize_archive(&zip_archive);
    mz_zip_writer_end(&zip_archive);
    std::cout << "[BACKUP] Backup completed successfully: " << outPath << std::endl;
    return true;
}

bool DiskManager::RestoreDrive(const std::string& driveLetter, std::string& errorMsg) {
    std::string rootPath = GetDrivePath(driveLetter);
    std::error_code ec;
    if (!fs::exists(rootPath, ec) && !ec) {
        errorMsg = "Drive not found.";
        return false;
    }

    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Webconventer Backup (*.webcnv)\0*.webcnv\0Zip Archives (*.zip)\0*.zip\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    
    std::string downloadsDir = GetDownloadsFolder();
    ofn.lpstrInitialDir = downloadsDir.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) != TRUE) {
        errorMsg = "Restore cancelled by user.";
        return false;
    }

    std::string backupFile = ofn.lpstrFile;

    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));
    if (!mz_zip_reader_init_file(&zip_archive, backupFile.c_str(), 0)) {
        errorMsg = "Failed to open backup archive.";
        return false;
    }

    int num_files = mz_zip_reader_get_num_files(&zip_archive);
    std::cout << "[RESTORE] Extracting " << num_files << " items to " << rootPath << std::endl;

    for (int i = 0; i < num_files; ++i) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) continue;
        
        std::string fileName = file_stat.m_filename;
        // Convert forward slashes to Windows backslashes for path creation
        std::string nativeRelPath = fileName;
        for (char& c : nativeRelPath) {
            if (c == '/') c = '\\';
        }

        std::string outPath = rootPath + nativeRelPath;

        {
            std::lock_guard<std::mutex> lock(g_progress_mutex);
            g_progress_text = "Restoring (" + std::to_string(i + 1) + "/" + std::to_string(num_files) + "): " + fileName;
        }
        g_progress_percent = (int)(((i + 1) * 100) / num_files);
        std::cout << "[RESTORE] (" << (i + 1) << "/" << num_files << ") " << fileName << std::endl;

        if (mz_zip_reader_is_file_a_directory(&zip_archive, i)) {
            fs::create_directories(outPath, ec);
        } else {
            fs::create_directories(fs::path(outPath).parent_path(), ec);
            mz_zip_reader_extract_to_file(&zip_archive, i, outPath.c_str(), 0);
        }
    }

    mz_zip_reader_end(&zip_archive);
    std::cout << "[RESTORE] Restore completed successfully." << std::endl;
    return true;
}

static int RunSilentCommand(const std::string& cmdLine) {
    UINT oldErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    int exitCode = -1;
    if (CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        if (GetExitCodeProcess(pi.hProcess, &code)) {
            exitCode = (int)code;
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    SetErrorMode(oldErrorMode);
    return exitCode;
}

// Helper to format via native format command for FAT32 / exFAT
bool DiskManager::FormatDriveFat(const std::string& driveLetter, std::string& errorMsg) {
    std::string cleanLetter = std::string(1, driveLetter[0]);

    {
        std::lock_guard<std::mutex> lock(g_progress_mutex);
        g_progress_text = "Formatting drive " + cleanLetter + ": to FAT32 / exFAT...";
    }
    g_progress_percent = 30;
    std::cout << "[FAT FORMAT] Starting format for " << cleanLetter << ":..." << std::endl;

    // Try native Windows fast format with FAT32 first
    std::string cmd = "cmd.exe /c format " + cleanLetter + ": /FS:FAT32 /Q /Y /V:USB";
    int result = RunSilentCommand(cmd);
    
    // If FAT32 failed (e.g. drive > 32GB), format as exFAT
    if (result != 0) {
        g_progress_percent = 60;
        std::cout << "[FAT FORMAT] FAT32 format returned " << result << ", attempting exFAT format..." << std::endl;
        cmd = "cmd.exe /c format " + cleanLetter + ": /FS:exFAT /Q /Y /V:USB";
        result = RunSilentCommand(cmd);
    }

    if (result == 0) {
        std::string rootPath = cleanLetter + ":\\";
        SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHA, rootPath.c_str(), NULL);
        SHChangeNotify(SHCNE_DRIVEADDGUI, SHCNF_PATHA, rootPath.c_str(), NULL);
        
        g_progress_percent = 100;
        std::cout << "[FAT FORMAT] Drive " << cleanLetter << ": formatted successfully." << std::endl;
        return true;
    }

    errorMsg = "Failed to format volume. Exit code: " + std::to_string(result);
    return false;
}

bool DiskManager::FormatDriveExt4(const std::string& driveLetter, std::string& errorMsg) {
    std::string rootPath = GetDrivePath(driveLetter);
    std::error_code ec;
    // We don't necessarily need the root path to 'exist' as a valid filesystem to format it,
    // but the drive must be present. We'll proceed even if fs::exists throws an error (RAW drive).
    if (!fs::exists(rootPath, ec) && !ec) {
        errorMsg = "Drive not found.";
        return false;
    }

    std::string drive = std::string(1, driveLetter[0]) + ":";
    std::string ntPath = "\\\\.\\" + drive;

    char errBuf[256] = { 0 };
    if (!FormatDriveExt4Native(ntPath.c_str(), errBuf, sizeof(errBuf), FormatProgressCallback)) {
        errorMsg = errBuf;
        return false;
    }

    return true;
}
