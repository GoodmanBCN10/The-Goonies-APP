import re

with open("source/app/update_service.cpp", "r") as f:
    content = f.read()

old_install_end = """    if (actualChecksum != expectedChecksum) {
        unlink(temporary.c_str());
        error = "Update checksum does not match GitHub release.";
        return false;
    }
    std::ofstream markerFile(marker, std::ios::binary | std::ios::trunc);
    markerFile << expectedChecksum << '\\n';
    markerFile.flush();
    if (!markerFile) {
        unlink(marker.c_str());
        unlink(temporary.c_str());
        error = "Unable to save staged update checksum.";
        return false;
    }
    markerFile.close();
    std::string helperError;
    if (!copyFileContents(targetPath_, helper, helperError)) {
        unlink(helper.c_str());
        unlink(marker.c_str());
        unlink(temporary.c_str());
        error = "Unable to create update helper: " + helperError;
        return false;
    }
    return true;
}"""

new_install_end = """    if (actualChecksum != expectedChecksum) {
        unlink(temporary.c_str());
        error = "Update checksum does not match GitHub release.";
        return false;
    }

    const std::string backup = targetPath_ + ".previous";
    unlink(backup.c_str());
    bool haveBackup = false;
    if (access(targetPath_.c_str(), F_OK) == 0) {
        if (rename(targetPath_.c_str(), backup.c_str()) == 0) {
            haveBackup = true;
        } else {
            std::string backupError;
            if (!copyFileContents(targetPath_, backup, backupError)) {
                error = "Unable to back up current application: " + backupError;
                return false;
            }
            haveBackup = true;
        }
    }
    if (rename(temporary.c_str(), targetPath_.c_str()) != 0) {
        int renameErrno = errno;
        std::string restoreError;
        if (haveBackup && !copyFileContents(backup, targetPath_, restoreError)) {
            error = "Unable to replace application with update, and unable to restore from backup: " + restoreError;
            return false;
        } else if (haveBackup) {
            rename(backup.c_str(), targetPath_.c_str());
        }
        error = "Unable to replace application with update: " + std::string(std::strerror(renameErrno));
        return false;
    }
    
    return true;
}"""

content = content.replace(old_install_end, new_install_end)

content = re.sub(r'bool UpdateService::finalizeStaged\(std::string& error\) const \{.*?\n\}', 
    'bool UpdateService::finalizeStaged(std::string& error) const {\n    return false;\n}', 
    content, flags=re.DOTALL)

with open("source/app/update_service.cpp", "w") as f:
    f.write(content)

print("Patched!")
