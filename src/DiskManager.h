#pragma once

#include <string>
#include <vector>

struct DiskInfo {
    std::string letter;
    std::string name;
    std::string fs;
};

class DiskManager {
public:
    static std::vector<DiskInfo> GetRemovableDrives();
    static bool BackupDrive(const std::string& driveLetter, std::string& errorMsg);
    static bool RestoreDrive(const std::string& driveLetter, std::string& errorMsg);
    static bool FormatDriveFat(const std::string& driveLetter, std::string& errorMsg);
    static bool FormatDriveExt4(const std::string& driveLetter, std::string& errorMsg);

private:
    static std::string GetDownloadsFolder();
    static std::string GetDrivePath(const std::string& driveLetter);
};
