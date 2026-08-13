import os
with open('source/app/update_service.cpp', 'r', encoding='utf-8') as f:
    code = f.read()

code = code.replace('''    // Staged update successfully downloaded to .tmp
    return true;
}''', '''    // Write the checksum marker file for finalizeStaged
    std::ofstream markerFileStream(marker, std::ios::binary | std::ios::trunc);
    if (!markerFileStream) {
        error = "Failed to write update marker file.";
        unlink(temporary.c_str());
        return false;
    }
    markerFileStream << expectedChecksum;
    markerFileStream.close();

    const std::string backup = targetPath_ + ".previous";
    unlink(backup.c_str());
    bool haveBackup = false;
    if (access(targetPath_.c_str(), F_OK) == 0) {
        std::string backupError;
        if (!copyFileContents(targetPath_, backup, backupError)) {
            error = "Unable to back up current application: " + backupError;
            return false;
        }
        haveBackup = true;
    }

    std::string copyError;
    if (!copyFileContents(targetPath_, helper, copyError)) {
        error = "Unable to create update helper: " + copyError;
        if (haveBackup) {
            rename(backup.c_str(), targetPath_.c_str());
        }
        return false;
    }

    // Staged update successfully downloaded to .tmp
    return true;
}''')

with open('source/app/update_service.cpp', 'w', encoding='utf-8') as f:
    f.write(code)

