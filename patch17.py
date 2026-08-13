import os
with open('source/app/update_service.cpp', 'r', encoding='utf-8') as f:
    code = f.read()

import re

# We will completely replace the install and finalizeStaged functions.

pattern = r'bool UpdateService::install\(const ReleaseInfo& release, std::string& error\) const \{.*?\n\}\n\nbool UpdateService::finalizeStaged\(std::string& error\) const \{.*?\n\}\n\nbool UpdateService::stagedReady\(\) const \{'

replacement = '''bool UpdateService::install(const ReleaseInfo& release, std::string& error) const {
    if (!trustedAssetUrl(release.nroUrl) ||
        !trustedAssetUrl(release.checksumUrl)) {
        error = "Update asset URL is not trusted.";
        return false;
    }
    std::string checksumText;
    if (!fetchWithRetry([&] {
            return metadataFetcher_(release.checksumUrl, kChecksumLimit,
                                    checksumText, error);
        }, error))
        return false;
    std::string expectedChecksum;
    if (!parseChecksum(checksumText, expectedChecksum)) {
        error = "Update checksum is invalid.";
        return false;
    }
    const std::string temporary = stagedPath();
    const std::string marker = temporary + ".sha256";
    const std::string helper = helperPath();
    unlink(temporary.c_str());
    unlink(marker.c_str());
    unlink(helper.c_str());
    if (!fetchWithRetry([&] {
            return assetFetcher_(release.nroUrl, temporary, kNroLimit, error);
        }, error))
        return false;
    std::string actualChecksum;
    if (!checksumFile(temporary, actualChecksum, error)) {
        unlink(temporary.c_str());
        return false;
    }
    if (actualChecksum != expectedChecksum) {
        unlink(temporary.c_str());
        error = "Update checksum does not match GitHub release.";
        return false;
    }
    
    // Write the checksum marker file for finalizeStaged
    std::ofstream markerFileStream(marker, std::ios::binary | std::ios::trunc);
    if (!markerFileStream) {
        error = "Failed to write update marker file.";
        unlink(temporary.c_str());
        return false;
    }
    markerFileStream << expectedChecksum;
    markerFileStream.close();

    // Copy ourselves to create the updater helper
    std::string copyError;
    if (!copyFileContents(targetPath_, helper, copyError)) {
        error = "Unable to create update helper: " + copyError;
        unlink(temporary.c_str());
        unlink(marker.c_str());
        return false;
    }

    // Staged update successfully downloaded to .tmp
    return true;
}

bool UpdateService::finalizeStaged(std::string& error) const {
    const std::string temporary = stagedPath();
    const std::string marker = temporary + ".sha256";
    std::ifstream markerFile(marker, std::ios::binary);
    std::ostringstream markerText;
    markerText << markerFile.rdbuf();
    std::string expectedChecksum;
    if (!markerFile || !parseChecksum(markerText.str(), expectedChecksum)) {
        error = "Staged update checksum is missing or invalid.";
        return false;
    }
    std::string actualChecksum;
    if (!checksumFile(temporary, actualChecksum, error))
        return false;
    if (actualChecksum != expectedChecksum) {
        error = "Staged update checksum does not match.";
        return false;
    }

    const std::string backup = targetPath_ + ".previous";
    unlink(backup.c_str());
    bool haveBackup = false;
    if (access(targetPath_.c_str(), F_OK) == 0) {
        if (rename(targetPath_.c_str(), backup.c_str()) == 0) {
            haveBackup = true;
        }
    }

    if (rename(temporary.c_str(), targetPath_.c_str()) != 0) {
        error = "Failed to apply update.";
        if (haveBackup) {
            rename(backup.c_str(), targetPath_.c_str());
        }
        return false;
    }

    unlink(marker.c_str());
    unlink(backup.c_str());
    
    // Force the Switch OS FAT32 driver to commit changes to the SD card
    fsdevCommitDevice("sdmc");
    
    // Wait just in case the hardware needs a moment
    svcSleepThread(500000000ULL); // 0.5 seconds
    
    return true;
}

bool UpdateService::stagedReady() const {'''

code = re.sub(pattern, replacement, code, flags=re.DOTALL)

with open('source/app/update_service.cpp', 'w', encoding='utf-8') as f:
    f.write(code)

