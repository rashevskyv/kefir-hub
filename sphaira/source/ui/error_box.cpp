#include "ui/error_box.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "i18n.hpp"

namespace sphaira::ui {
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
        case Module_Sphaira: return "Sphaira";
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

        case Result_TransferCancelled: return "SphairaError_TransferCancelled";
        case Result_StreamBadSeek: return "SphairaError_StreamBadSeek";
        case Result_FsTooManyEntries: return "SphairaError_FsTooManyEntries";
        case Result_FsNewPathTooLarge: return "SphairaError_FsNewPathTooLarge";
        case Result_FsInvalidType: return "SphairaError_FsInvalidType";
        case Result_FsEmpty: return "SphairaError_FsEmpty";
        case Result_FsAlreadyRoot: return "SphairaError_FsAlreadyRoot";
        case Result_FsNoCurrentPath: return "SphairaError_FsNoCurrentPath";
        case Result_FsBrokenCurrentPath: return "SphairaError_FsBrokenCurrentPath";
        case Result_FsIndexOutOfBounds: return "SphairaError_FsIndexOutOfBounds";
        case Result_FsFsNotActive: return "SphairaError_FsFsNotActive";
        case Result_FsNewPathEmpty: return "SphairaError_FsNewPathEmpty";
        case Result_FsLoadingCancelled: return "SphairaError_FsLoadingCancelled";
        case Result_FsBrokenRoot: return "SphairaError_FsBrokenRoot";
        case Result_FsUnknownStdioError: return "SphairaError_FsUnknownStdioError";
        case Result_FsReadOnly: return "SphairaError_FsReadOnly";
        case Result_FsNotActive: return "SphairaError_FsNotActive";
        case Result_FsFailedStdioStat: return "SphairaError_FsFailedStdioStat";
        case Result_FsFailedStdioOpendir: return "SphairaError_FsFailedStdioOpendir";
        case Result_NroBadMagic: return "SphairaError_NroBadMagic";
        case Result_NroBadSize: return "SphairaError_NroBadSize";
        case Result_AppFailedMusicDownload: return "SphairaError_AppFailedMusicDownload";
        case Result_CurlFailedEasyInit: return "SphairaError_CurlFailedEasyInit";
        case Result_DumpFailedNetworkUpload: return "SphairaError_DumpFailedNetworkUpload";
        case Result_UnzOpen2_64: return "SphairaError_UnzOpen2_64";
        case Result_UnzGetGlobalInfo64: return "SphairaError_UnzGetGlobalInfo64";
        case Result_UnzLocateFile: return "SphairaError_UnzLocateFile";
        case Result_UnzGoToFirstFile: return "SphairaError_UnzGoToFirstFile";
        case Result_UnzGoToNextFile: return "SphairaError_UnzGoToNextFile";
        case Result_UnzOpenCurrentFile: return "SphairaError_UnzOpenCurrentFile";
        case Result_UnzGetCurrentFileInfo64: return "SphairaError_UnzGetCurrentFileInfo64";
        case Result_UnzReadCurrentFile: return "SphairaError_UnzReadCurrentFile";
        case Result_ZipOpen2_64: return "SphairaError_ZipOpen2_64";
        case Result_ZipOpenNewFileInZip: return "SphairaError_ZipOpenNewFileInZip";
        case Result_ZipWriteInFileInZip: return "SphairaError_ZipWriteInFileInZip";
        case Result_MmzBadLocalHeaderSig: return "SphairaError_MmzBadLocalHeaderSig";
        case Result_MmzBadLocalHeaderRead: return "SphairaError_MmzBadLocalHeaderRead";
        case Result_FileBrowserFailedUpload: return "SphairaError_FileBrowserFailedUpload";
        case Result_FileBrowserDirNotDaybreak: return "SphairaError_FileBrowserDirNotDaybreak";
        case Result_AppstoreFailedZipDownload: return "SphairaError_AppstoreFailedZipDownload";
        case Result_AppstoreFailedMd5: return "SphairaError_AppstoreFailedMd5";
        case Result_AppstoreFailedParseManifest: return "SphairaError_AppstoreFailedParseManifest";
        case Result_GameBadReadForDump: return "SphairaError_GameBadReadForDump";
        case Result_GameEmptyMetaEntries: return "SphairaError_GameEmptyMetaEntries";
        case Result_GameMultipleKeysFound: return "SphairaError_GameMultipleKeysFound";
        case Result_GameNoNspEntriesBuilt: return "SphairaError_GameNoNspEntriesBuilt";
        case Result_KeyMissingNcaKeyArea: return "SphairaError_KeyMissingNcaKeyArea";
        case Result_KeyMissingTitleKek: return "SphairaError_KeyMissingTitleKek";
        case Result_KeyMissingMasterKey: return "SphairaError_KeyMissingMasterKey";
        case Result_KeyFailedDecyptETicketDeviceKey: return "SphairaError_KeyFailedDecyptETicketDeviceKey";
        case Result_NcaFailedNcaHeaderHashVerify: return "SphairaError_NcaFailedNcaHeaderHashVerify";
        case Result_NcaBadSigKeyGen: return "SphairaError_NcaBadSigKeyGen";
        case Result_GcBadReadForDump: return "SphairaError_GcBadReadForDump";
        case Result_GcEmptyGamecard: return "SphairaError_GcEmptyGamecard";
        case Result_GcBadXciMagic: return "SphairaError_GcBadXciMagic";
        case Result_GcBadXciRomSize: return "SphairaError_GcBadXciRomSize";
        case Result_GcFailedToGetSecurityInfo: return "SphairaError_GcFailedToGetSecurityInfo";
        case Result_GhdlEmptyAsset: return "SphairaError_GhdlEmptyAsset";
        case Result_GhdlFailedToDownloadAsset: return "SphairaError_GhdlFailedToDownloadAsset";
        case Result_GhdlFailedToDownloadAssetJson: return "SphairaError_GhdlFailedToDownloadAssetJson";
        case Result_GhdlFileTooLarge: return "SphairaError_GhdlFileTooLarge";
        case Result_ThemezerFailedToDownloadThemeMeta: return "SphairaError_ThemezerFailedToDownloadThemeMeta";
        case Result_ThemezerFailedToDownloadTheme: return "SphairaError_ThemezerFailedToDownloadTheme";
        case Result_MainFailedToDownloadUpdate: return "SphairaError_MainFailedToDownloadUpdate";
        case Result_UsbDsBadDeviceSpeed: return "SphairaError_UsbDsBadDeviceSpeed";
        case Result_NspBadMagic: return "SphairaError_NspBadMagic";
        case Result_XciBadMagic: return "SphairaError_XciBadMagic";
        case Result_XciSecurePartitionNotFound: return "SphairaError_XciSecurePartitionNotFound";
        case Result_EsBadTitleKeyType: return "SphairaError_EsBadTitleKeyType";
        case Result_EsPersonalisedTicketDeviceIdMissmatch: return "SphairaError_EsPersonalisedTicketDeviceIdMissmatch";
        case Result_EsFailedDecryptPersonalisedTicket: return "SphairaError_EsFailedDecryptPersonalisedTicket";
        case Result_EsBadDecryptedPersonalisedTicketSize: return "SphairaError_EsBadDecryptedPersonalisedTicketSize";
        case Result_EsInvalidTicketBadRightsId: return "SphairaError_EsInvalidTicketBadRightsId";
        case Result_EsInvalidTicketFromatVersion: return "SphairaError_EsInvalidTicketFromatVersion";
        case Result_EsInvalidTicketKeyType: return "SphairaError_EsInvalidTicketKeyType";
        case Result_EsInvalidTicketKeyRevision: return "SphairaError_EsInvalidTicketKeyRevision";
        case Result_OwoBadArgs: return "SphairaError_OwoBadArgs";
        case Result_UsbCancelled: return "SphairaError_UsbCancelled";
        case Result_UsbBadMagic: return "SphairaError_UsbBadMagic";
        case Result_UsbBadVersion: return "SphairaError_UsbBadVersion";
        case Result_UsbBadCount: return "SphairaError_UsbBadCount";
        case Result_UsbBadBufferAlign: return "SphairaError_UsbBadBufferAlign";
        case Result_UsbBadTransferSize: return "SphairaError_UsbBadTransferSize";
        case Result_UsbEmptyTransferSize: return "SphairaError_UsbEmptyTransferSize";
        case Result_UsbOverflowTransferSize: return "SphairaError_UsbOverflowTransferSize";
        case Result_UsbUploadBadMagic: return "SphairaError_UsbUploadBadMagic";
        case Result_UsbUploadExit: return "SphairaError_UsbUploadExit";
        case Result_UsbUploadBadCount: return "SphairaError_UsbUploadBadCount";
        case Result_UsbUploadBadTransferSize: return "SphairaError_UsbUploadBadTransferSize";
        case Result_UsbUploadBadTotalSize: return "SphairaError_UsbUploadBadTotalSize";
        case Result_UsbUploadBadCommand: return "SphairaError_UsbUploadBadCommand";
        case Result_YatiContainerNotFound: return "SphairaError_YatiContainerNotFound";
        case Result_YatiNcaNotFound: return "SphairaError_YatiNcaNotFound";
        case Result_YatiInvalidNcaReadSize: return "SphairaError_YatiInvalidNcaReadSize";
        case Result_YatiInvalidNcaSigKeyGen: return "SphairaError_YatiInvalidNcaSigKeyGen";
        case Result_YatiInvalidNcaMagic: return "SphairaError_YatiInvalidNcaMagic";
        case Result_YatiInvalidNcaSignature0: return "SphairaError_YatiInvalidNcaSignature0";
        case Result_YatiInvalidNcaSignature1: return "SphairaError_YatiInvalidNcaSignature1";
        case Result_YatiInvalidNcaSha256: return "SphairaError_YatiInvalidNcaSha256";
        case Result_YatiNczSectionNotFound: return "SphairaError_YatiNczSectionNotFound";
        case Result_YatiInvalidNczSectionCount: return "SphairaError_YatiInvalidNczSectionCount";
        case Result_YatiNczBlockNotFound: return "SphairaError_YatiNczBlockNotFound";
        case Result_YatiInvalidNczBlockVersion: return "SphairaError_YatiInvalidNczBlockVersion";
        case Result_YatiInvalidNczBlockType: return "SphairaError_YatiInvalidNczBlockType";
        case Result_YatiInvalidNczBlockTotal: return "SphairaError_YatiInvalidNczBlockTotal";
        case Result_YatiInvalidNczBlockSizeExponent: return "SphairaError_YatiInvalidNczBlockSizeExponent";
        case Result_YatiInvalidNczZstdError: return "SphairaError_YatiInvalidNczZstdError";
        case Result_YatiTicketNotFound: return "SphairaError_YatiTicketNotFound";
        case Result_YatiInvalidTicketBadRightsId: return "SphairaError_YatiInvalidTicketBadRightsId";
        case Result_YatiCertNotFound: return "SphairaError_YatiCertNotFound";
        case Result_YatiNcmDbCorruptHeader: return "SphairaError_YatiNcmDbCorruptHeader";
        case Result_YatiNcmDbCorruptInfos: return "SphairaError_YatiNcmDbCorruptInfos";
    }

    return "";
}

auto GetErrorDescription(Result rc) -> std::string {
    switch (rc) {
        case FsError_TargetLocked:
            return "The file is locked by the system or another app. If you just took a screenshot, wait a moment and try again."_i18n;
        case FsError_PathNotFound:
            return "The specified file or directory path was not found."_i18n;
        case FsError_PathAlreadyExists:
            return "A file or directory with this name already exists."_i18n;
        case FsError_TooLongPath:
            return "The path is too long. The Switch filesystem has a 768-character limit."_i18n;
        case FsError_InvalidCharacter:
            return "The name contains invalid characters. Do not use characters like *, ?, :, etc."_i18n;
        case Result_FsReadOnly:
            return "This directory or file is write-protected."_i18n;
        default:
            return "";
    }
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

    App::PlaySoundEffect(SoundEffect::SoundEffect_Error);
}

ErrorBox::ErrorBox(Result code, const std::string& message) : ErrorBox{message} {
    m_code = code;
    m_code_message = GetCodeMessage(code);
    m_code_module = std::to_string(R_MODULE(code));
    if (auto str = GetModule(code)) {
        m_code_module += " (" + std::string(str) + ")";
    }

    const auto desc = GetErrorDescription(code);
    if (!desc.empty()) {
        m_message += "\n\n" + desc;
    }

    log_write("[ERROR] Code: 0x%X Module: %s Description: %u\n", R_VALUE(code), m_code_module.c_str(), R_DESCRIPTION(code));
}

auto ErrorBox::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);
}

auto ErrorBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    gfx::dimBackground(vg);

    // Measure the error description text height dynamically
    const float padding = 45.f;
    const float text_w = m_pos.w - padding * 2.f;
    float message_bounds[4]{};
    nvgSave(vg);
    nvgFontSize(vg, 22.f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, 1.25f);
    nvgTextBoxBounds(vg, 0, 0, text_w, m_message.c_str(), nullptr, message_bounds);
    const float message_h = message_bounds[3] - message_bounds[1];

    // Measure the help text height dynamically
    const std::string help_text = "If this message appears repeatedly, please open an issue."_i18n;
    float help_bounds[4]{};
    nvgFontSize(vg, 18.f);
    nvgTextLineHeight(vg, 1.25f);
    nvgTextBoxBounds(vg, 0, 0, text_w, help_text.c_str(), nullptr, help_bounds);
    const float help_h = help_bounds[3] - help_bounds[1];
    nvgRestore(vg);

    // Dynamically calculate the popup window height
    const float content_h = 183.f + message_h + help_h + 15.f + 18.f + 30.f + 65.f + 35.f;
    m_pos.h = std::max(430.f, content_h);
    m_pos.y = (SCREEN_HEIGHT - m_pos.h) / 2.f;

    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5.f);

    const auto center_x = m_pos.x + m_pos.w / 2.f;
    const float text_x = m_pos.x + padding;

    // Draw Error Icon
    gfx::drawText(vg, center_x, m_pos.y + 35.f, 63.f, theme->GetColour(ThemeEntryID_ERROR), "\uE140", NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

    // Draw Code/Title
    float next_y = m_pos.y + 125.f;
    if (m_code.has_value()) {
        const auto code = m_code.value();
        if (m_code_message.empty()) {
            gfx::drawTextArgs(vg, center_x, next_y, 25.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "Code: 0x%X Module: %s", R_VALUE(code), m_code_module.c_str());
        } else {
            gfx::drawTextArgs(vg, center_x, next_y, 25.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", m_code_message.c_str());
        }
    } else {
        gfx::drawText(vg, center_x, next_y, 25.f, theme->GetColour(ThemeEntryID_TEXT), "An error occurred"_i18n.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    }

    // Draw Error Description Message
    next_y += 50.f;
    gfx::drawTextBox(vg, text_x, next_y, 22.f, text_w, theme->GetColour(ThemeEntryID_TEXT), m_message.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

    // Draw Help/Issue details (wrapped to prevent overflow)
    next_y += message_h + 25.f;
    gfx::drawTextBox(vg, text_x, next_y, 18.f, text_w, theme->GetColour(ThemeEntryID_TEXT_INFO), help_text.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP, nullptr, 1.25f);

    // Draw GitHub URL
    next_y += help_h + 15.f;
    gfx::drawText(vg, center_x, next_y, 18.f, theme->GetColour(ThemeEntryID_TEXT_INFO), "https://github.com/ITotalJustice/sphaira/issues", NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

    // Draw OK button box relative to dynamically calculated height
    const Vec4 box = {
        m_pos.x + (m_pos.w - 365.f) / 2.f,
        m_pos.y + m_pos.h - 65.f - 35.f,
        365.f,
        65.f
    };
    gfx::drawRectOutline(vg, theme, 4.f, box);
    gfx::drawText(vg, center_x, box.y + box.h / 2.f, 23.f, theme->GetColour(ThemeEntryID_TEXT_SELECTED), "OK"_i18n.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
}

} // namespace sphaira::ui
