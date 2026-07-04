#include "frontend/error_box.hpp"
#include "frontend/nvg_util.hpp"
#include "app.hpp"
#include "i18n.hpp"

namespace GooniesInstaller::frontend {
namespace {

auto GetModule(Result rc) -> const char* {
    switch (R_MODULE(rc)) {
        case Module_Svc: return "Svc";
        case Module_Fs: return "Fs";
        case Module_Os: return "Os";
        case Module_Ncm: return "Ncm";
        case Module_Ns: return "Ns";
        case Module_Spl: return "Spl";
        case Module_Applet: return "Applet";
        case Module_Usb: return "Usb";
        case Module_Irsensor: return "Irsensor";
        case Module_Libnx: return "Libnx";
        case Module_GooniesInstaller: return "GooniesInstaller";
    }

    return nullptr;
}
auto GetCodeMessage(Result rc) -> const char* {
    switch (rc) {
        case SvcError_TimedOut: return "SvcError_TimedOut";
        case SvcError_Cancelled: return "SvcError_Cancelled";

        case FsError_PathNotFound: return "FsError_PathNotFound";
        case FsError_PathAlreadyExists: return "FsError_PathAlreadyExists";
        case FsError_TargetLocked: return "FsError_TargetLocked";
        case FsError_TooLongPath: return "FsError_TooLongPath";
        case FsError_InvalidCharacter: return "FsError_InvalidCharacter";
        case FsError_InvalidOffset: return "FsError_InvalidOffset";
        case FsError_InvalidSize: return "FsError_InvalidSize";

        case Result_TransferCancelled: return "GooniesInstallerError_TransferCancelled";
        case Result_StreamBadSeek: return "GooniesInstallerError_StreamBadSeek";
        case Result_FsTooManyEntries: return "GooniesInstallerError_FsTooManyEntries";
        case Result_FsNewPathTooLarge: return "GooniesInstallerError_FsNewPathTooLarge";
        case Result_FsInvalidType: return "GooniesInstallerError_FsInvalidType";
        case Result_FsEmpty: return "GooniesInstallerError_FsEmpty";
        case Result_FsAlreadyRoot: return "GooniesInstallerError_FsAlreadyRoot";
        case Result_FsNoCurrentPath: return "GooniesInstallerError_FsNoCurrentPath";
        case Result_FsBrokenCurrentPath: return "GooniesInstallerError_FsBrokenCurrentPath";
        case Result_FsIndexOutOfBounds: return "GooniesInstallerError_FsIndexOutOfBounds";
        case Result_FsFsNotActive: return "GooniesInstallerError_FsFsNotActive";
        case Result_FsNewPathEmpty: return "GooniesInstallerError_FsNewPathEmpty";
        case Result_FsLoadingCancelled: return "GooniesInstallerError_FsLoadingCancelled";
        case Result_FsBrokenRoot: return "GooniesInstallerError_FsBrokenRoot";
        case Result_FsUnknownStdioError: return "GooniesInstallerError_FsUnknownStdioError";
        case Result_FsStdioFailedToSeek: return "GooniesInstallerError_FsStdioFailedToSeek";
        case Result_FsStdioFailedToRead: return "GooniesInstallerError_FsStdioFailedToRead";
        case Result_FsStdioFailedToWrite: return "GooniesInstallerError_FsStdioFailedToWrite";
        case Result_FsStdioFailedToOpenFile: return "GooniesInstallerError_FsStdioFailedToOpenFile";
        case Result_FsStdioFailedToCreate: return "GooniesInstallerError_FsStdioFailedToCreate";
        case Result_FsStdioFailedToTruncate: return "GooniesInstallerError_FsStdioFailedToTruncate";
        case Result_FsStdioFailedToFlush: return "GooniesInstallerError_FsStdioFailedToFlush";
        case Result_FsStdioFailedToCreateDirectory: return "GooniesInstallerError_FsStdioFailedToCreateDirectory";
        case Result_FsStdioFailedToDeleteFile: return "GooniesInstallerError_FsStdioFailedToDeleteFile";
        case Result_FsStdioFailedToDeleteDirectory: return "GooniesInstallerError_FsStdioFailedToDeleteDirectory";
        case Result_FsStdioFailedToOpenDirectory: return "GooniesInstallerError_FsStdioFailedToOpenDirectory";
        case Result_FsStdioFailedToRename: return "GooniesInstallerError_FsStdioFailedToRename";
        case Result_FsStdioFailedToStat: return "GooniesInstallerError_FsStdioFailedToStat";
        case Result_FsReadOnly: return "GooniesInstallerError_FsReadOnly";
        case Result_FsNotActive: return "GooniesInstallerError_FsNotActive";
        case Result_FsFailedStdioStat: return "GooniesInstallerError_FsFailedStdioStat";
        case Result_FsFailedStdioOpendir: return "GooniesInstallerError_FsFailedStdioOpendir";
        case Result_NroBadMagic: return "GooniesInstallerError_NroBadMagic";
        case Result_NroBadSize: return "GooniesInstallerError_NroBadSize";
        case Result_AppFailedMusicDownload: return "GooniesInstallerError_AppFailedMusicDownload";
        case Result_CurlFailedEasyInit: return "GooniesInstallerError_CurlFailedEasyInit";
        case Result_DumpFailedNetworkUpload: return "GooniesInstallerError_DumpFailedNetworkUpload";
        case Result_UnzOpen2_64: return "GooniesInstallerError_UnzOpen2_64";
        case Result_UnzGetGlobalInfo64: return "GooniesInstallerError_UnzGetGlobalInfo64";
        case Result_UnzLocateFile: return "GooniesInstallerError_UnzLocateFile";
        case Result_UnzGoToFirstFile: return "GooniesInstallerError_UnzGoToFirstFile";
        case Result_UnzGoToNextFile: return "GooniesInstallerError_UnzGoToNextFile";
        case Result_UnzOpenCurrentFile: return "GooniesInstallerError_UnzOpenCurrentFile";
        case Result_UnzGetCurrentFileInfo64: return "GooniesInstallerError_UnzGetCurrentFileInfo64";
        case Result_UnzReadCurrentFile: return "GooniesInstallerError_UnzReadCurrentFile";
        case Result_ZipOpen2_64: return "GooniesInstallerError_ZipOpen2_64";
        case Result_ZipOpenNewFileInZip: return "GooniesInstallerError_ZipOpenNewFileInZip";
        case Result_ZipWriteInFileInZip: return "GooniesInstallerError_ZipWriteInFileInZip";
        case Result_MmzBadLocalHeaderSig: return "GooniesInstallerError_MmzBadLocalHeaderSig";
        case Result_MmzBadLocalHeaderRead: return "GooniesInstallerError_MmzBadLocalHeaderRead";
        case Result_FileBrowserFailedUpload: return "GooniesInstallerError_FileBrowserFailedUpload";
        case Result_FileBrowserDirNotDaybreak: return "GooniesInstallerError_FileBrowserDirNotDaybreak";
        case Result_AppstoreFailedZipDownload: return "GooniesInstallerError_AppstoreFailedZipDownload";
        case Result_AppstoreFailedMd5: return "GooniesInstallerError_AppstoreFailedMd5";
        case Result_AppstoreFailedParseManifest: return "GooniesInstallerError_AppstoreFailedParseManifest";
        case Result_GameBadReadForDump: return "GooniesInstallerError_GameBadReadForDump";
        case Result_GameEmptyMetaEntries: return "GooniesInstallerError_GameEmptyMetaEntries";
        case Result_GameMultipleKeysFound: return "GooniesInstallerError_GameMultipleKeysFound";
        case Result_GameNoNspEntriesBuilt: return "GooniesInstallerError_GameNoNspEntriesBuilt";
        case Result_KeyMissingNcaKeyArea: return "GooniesInstallerError_KeyMissingNcaKeyArea";
        case Result_KeyMissingTitleKek: return "GooniesInstallerError_KeyMissingTitleKek";
        case Result_KeyMissingMasterKey: return "GooniesInstallerError_KeyMissingMasterKey";
        case Result_KeyFailedDecyptETicketDeviceKey: return "GooniesInstallerError_KeyFailedDecyptETicketDeviceKey";
        case Result_NcaFailedNcaHeaderHashVerify: return "GooniesInstallerError_NcaFailedNcaHeaderHashVerify";
        case Result_NcaBadSigKeyGen: return "GooniesInstallerError_NcaBadSigKeyGen";
        case Result_GcBadReadForDump: return "GooniesInstallerError_GcBadReadForDump";
        case Result_GcEmptyGamecard: return "GooniesInstallerError_GcEmptyGamecard";
        case Result_GcBadXciMagic: return "GooniesInstallerError_GcBadXciMagic";
        case Result_GcBadXciRomSize: return "GooniesInstallerError_GcBadXciRomSize";
        case Result_GcFailedToGetSecurityInfo: return "GooniesInstallerError_GcFailedToGetSecurityInfo";
        case Result_GhdlEmptyAsset: return "GooniesInstallerError_GhdlEmptyAsset";
        case Result_GhdlFailedToDownloadAsset: return "GooniesInstallerError_GhdlFailedToDownloadAsset";
        case Result_GhdlFailedToDownloadAssetJson: return "GooniesInstallerError_GhdlFailedToDownloadAssetJson";
        case Result_ThemezerFailedToDownloadThemeMeta: return "GooniesInstallerError_ThemezerFailedToDownloadThemeMeta";
        case Result_ThemezerFailedToDownloadTheme: return "GooniesInstallerError_ThemezerFailedToDownloadTheme";
        case Result_MainFailedToDownloadUpdate: return "GooniesInstallerError_MainFailedToDownloadUpdate";
        case Result_UsbDsBadDeviceSpeed: return "GooniesInstallerError_UsbDsBadDeviceSpeed";
        case Result_NcaBadMagic: return "GooniesInstallerError_NcaBadMagic";
        case Result_NspBadMagic: return "GooniesInstallerError_NspBadMagic";
        case Result_XciBadMagic: return "GooniesInstallerError_XciBadMagic";
        case Result_XciSecurePartitionNotFound: return "GooniesInstallerError_XciSecurePartitionNotFound";
        case Result_EsBadTitleKeyType: return "GooniesInstallerError_EsBadTitleKeyType";
        case Result_EsPersonalisedTicketDeviceIdMissmatch: return "GooniesInstallerError_EsPersonalisedTicketDeviceIdMissmatch";
        case Result_EsFailedDecryptPersonalisedTicket: return "GooniesInstallerError_EsFailedDecryptPersonalisedTicket";
        case Result_EsBadDecryptedPersonalisedTicketSize: return "GooniesInstallerError_EsBadDecryptedPersonalisedTicketSize";
        case Result_EsInvalidTicketBadRightsId: return "GooniesInstallerError_EsInvalidTicketBadRightsId";
        case Result_EsInvalidTicketFromatVersion: return "GooniesInstallerError_EsInvalidTicketFromatVersion";
        case Result_EsInvalidTicketKeyType: return "GooniesInstallerError_EsInvalidTicketKeyType";
        case Result_EsInvalidTicketKeyRevision: return "GooniesInstallerError_EsInvalidTicketKeyRevision";
        case Result_OwoBadArgs: return "GooniesInstallerError_OwoBadArgs";
        case Result_UsbCancelled: return "GooniesInstallerError_UsbCancelled";
        case Result_UsbBadMagic: return "GooniesInstallerError_UsbBadMagic";
        case Result_UsbBadVersion: return "GooniesInstallerError_UsbBadVersion";
        case Result_UsbBadCount: return "GooniesInstallerError_UsbBadCount";
        case Result_UsbBadBufferAlign: return "GooniesInstallerError_UsbBadBufferAlign";
        case Result_UsbBadTransferSize: return "GooniesInstallerError_UsbBadTransferSize";
        case Result_UsbEmptyTransferSize: return "GooniesInstallerError_UsbEmptyTransferSize";
        case Result_UsbOverflowTransferSize: return "GooniesInstallerError_UsbOverflowTransferSize";
        case Result_UsbUploadBadMagic: return "GooniesInstallerError_UsbUploadBadMagic";
        case Result_UsbUploadExit: return "GooniesInstallerError_UsbUploadExit";
        case Result_UsbUploadBadCount: return "GooniesInstallerError_UsbUploadBadCount";
        case Result_UsbUploadBadTransferSize: return "GooniesInstallerError_UsbUploadBadTransferSize";
        case Result_UsbUploadBadTotalSize: return "GooniesInstallerError_UsbUploadBadTotalSize";
        case Result_UsbUploadBadCommand: return "GooniesInstallerError_UsbUploadBadCommand";
        case Result_YatiContainerNotFound: return "GooniesInstallerError_YatiContainerNotFound";
        case Result_YatiNcaNotFound: return "GooniesInstallerError_YatiNcaNotFound";
        case Result_YatiInvalidNcaReadSize: return "GooniesInstallerError_YatiInvalidNcaReadSize";
        case Result_YatiInvalidNcaSigKeyGen: return "GooniesInstallerError_YatiInvalidNcaSigKeyGen";
        case Result_YatiInvalidNcaMagic: return "GooniesInstallerError_YatiInvalidNcaMagic";
        case Result_YatiInvalidNcaSignature0: return "GooniesInstallerError_YatiInvalidNcaSignature0";
        case Result_YatiInvalidNcaSignature1: return "GooniesInstallerError_YatiInvalidNcaSignature1";
        case Result_YatiInvalidNcaSha256: return "GooniesInstallerError_YatiInvalidNcaSha256";
        case Result_YatiNczSectionNotFound: return "GooniesInstallerError_YatiNczSectionNotFound";
        case Result_YatiInvalidNczSectionCount: return "GooniesInstallerError_YatiInvalidNczSectionCount";
        case Result_YatiNczBlockNotFound: return "GooniesInstallerError_YatiNczBlockNotFound";
        case Result_YatiInvalidNczBlockVersion: return "GooniesInstallerError_YatiInvalidNczBlockVersion";
        case Result_YatiInvalidNczBlockType: return "GooniesInstallerError_YatiInvalidNczBlockType";
        case Result_YatiInvalidNczBlockTotal: return "GooniesInstallerError_YatiInvalidNczBlockTotal";
        case Result_YatiInvalidNczBlockSizeExponent: return "GooniesInstallerError_YatiInvalidNczBlockSizeExponent";
        case Result_YatiInvalidNczZstdError: return "GooniesInstallerError_YatiInvalidNczZstdError";
        case Result_YatiTicketNotFound: return "GooniesInstallerError_YatiTicketNotFound";
        case Result_YatiInvalidTicketBadRightsId: return "GooniesInstallerError_YatiInvalidTicketBadRightsId";
        case Result_YatiCertNotFound: return "GooniesInstallerError_YatiCertNotFound";
        case Result_YatiNcmDbCorruptHeader: return "GooniesInstallerError_YatiNcmDbCorruptHeader";
        case Result_YatiNcmDbCorruptInfos: return "GooniesInstallerError_YatiNcmDbCorruptInfos";

        case Result_NszFailedCreateCctx: return "GooniesInstallerError_NszFailedCreateCctx";
        case Result_NszFailedSetCompressionLevel: return "GooniesInstallerError_NszFailedSetCompressionLevel";
        case Result_NszFailedSetThreadCount: return "GooniesInstallerError_NszFailedSetThreadCount";
        case Result_NszFailedSetLongDistanceMode: return "GooniesInstallerError_NszFailedSetLongDistanceMode";
        case Result_NszFailedResetCctx: return "GooniesInstallerError_NszFailedResetCctx";
        case Result_NszFailedCompress2: return "GooniesInstallerError_NszFailedCompress2";
        case Result_NszFailedCompressStream2: return "GooniesInstallerError_NszFailedCompressStream2";
        case Result_NszTooManyBlocks: return "GooniesInstallerError_NszTooManyBlocks";
        case Result_NszMissingBlocks: return "GooniesInstallerError_NszMissingBlocks";
    }

    return "";
}

} // namespace

ErrorBox::ErrorBox(const std::string& message) : m_message{message} {
    log_write("[ERROR] %s\n", m_message.c_str());

    m_pos.w = 770.f;
    m_pos.h = 430.f;
    m_pos.x = 255;
    m_pos.y = 145;

    SetAction(Button::A, Action{[this](){
        SetPop();
    }});

    App::PlaySoundEffect(SoundEffect::Error);
}

ErrorBox::ErrorBox(Result code, const std::string& message) : ErrorBox{message} {
    m_code = code;
    m_code_message = GetCodeMessage(code);
    m_code_module = std::to_string(R_MODULE(code));
    if (auto str = GetModule(code)) {
        m_code_module += " (" + std::string(str) + ")";
    }
    log_write("[ERROR] Code: 0x%X Module: %s Description: %u\n", R_VALUE(code), m_code_module.c_str(), R_DESCRIPTION(code));
}

auto ErrorBox::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);
}

auto ErrorBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    gfx::dimBackground(vg);
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP));

    const Vec4 box = { 455, 470, 365, 65 };
    const auto center_x = m_pos.x + m_pos.w/2;

    gfx::drawTextArgs(vg, center_x, 180, 63, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_ERROR), "\uE140");
    if (m_code.has_value()) {
        const auto code = m_code.value();
        if (m_code_message.empty()) {
            gfx::drawTextArgs(vg, center_x, 270, 25, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "Code: 0x%X Module: %s", R_VALUE(code), m_code_module.c_str());
        } else {
            gfx::drawTextArgs(vg, center_x, 270, 25, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", m_code_message.c_str());
        }
    } else {
        gfx::drawTextArgs(vg, center_x, 270, 25, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "An error occurred"_i18n.c_str());
    }
    gfx::drawTextArgs(vg, center_x, 325, 23, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", m_message.c_str());
    gfx::drawTextArgs(vg, center_x, 380, 20, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "If this message appears repeatedly, please open an issue."_i18n.c_str());
    gfx::drawTextArgs(vg, center_x, 415, 20, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "https://github.com/ITotalJustice/GooniesInstaller/issues");
    gfx::drawRectOutline(vg, theme, 4.f, box);
    gfx::drawTextArgs(vg, center_x, box.y + box.h/2, 23, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED), "OK"_i18n.c_str());
}

} // namespace GooniesInstaller::frontend
