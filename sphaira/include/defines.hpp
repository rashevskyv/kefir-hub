#pragma once

#include <switch.h>
#include <experimental/scope>

// NOTE: these are *subsets* of the atmosphere/libnx result tables --
// only the codes sphaira actually compares against are listed. Add the
// entry you need (value from ams) rather than re-pasting a whole table.
enum {
    Module_Svc = 1,
    Module_Fs = 2,
    Module_Os = 3,
    Module_Ncm = 5,
    Module_Ns = 16,
    Module_Spl = 26,
    Module_Nifm = 110,
    Module_Applet = 131,
    Module_Usb = 140,
    Module_Irsensor = 205,
    Module_Sphaira = 505,
};

enum SvcError {
    SvcError_TimedOut = 0xEA01,
    SvcError_Cancelled = 0xEC01,
};

enum FsError {
    FsError_PathNotFound = 0x202,
    FsError_PathNotFoundFsDev = 0x1002,
    FsError_PathAlreadyExists = 0x402,
    FsError_TargetLocked = 0xE02,
    FsError_NotImplemented = 0x177202,
    FsError_TooLongPath = 0x2EE602,
    FsError_InvalidCharacter = 0x2EE802,
    FsError_InvalidOffset = 0x2F5A02,
    FsError_InvalidSize = 0x2F5C02,
    FsError_UnsupportedOperateRangeForFileStorage = 0x314402,
    FsError_FileNotFound = 0x339402,
};

// libnx returns these bare usb-module (140) results out of
// usbDsParseReportData() when a URB completes with a status other than 0x3
// (finished). libnx writes them as plain hex constants, so there is no symbol
// to compare against -- hence the copies here.
enum UsbError {
    UsbError_UrbFailed = 0x828C,    // urb_status 0x4
    UsbError_UrbCancelled = 0x748C, // urb_status 0x5
    UsbError_UrbBadStatus = 0x108C, // any other urb_status
};

enum class SphairaResult : Result {
    TransferCancelled,
    StreamBadSeek,

    FsTooManyEntries,
    FsNewPathTooLarge,
    FsInvalidType,
    FsEmpty,
    FsAlreadyRoot,
    FsNoCurrentPath,
    FsBrokenCurrentPath,
    FsIndexOutOfBounds,
    FsFsNotActive,
    FsNewPathEmpty,
    FsLoadingCancelled,
    FsBrokenRoot,
    FsUnknownStdioError,
    FsReadOnly,
    FsNotActive,
    FsFailedStdioStat,
    FsFailedStdioOpendir,

    NroBadMagic,
    NroBadSize,

    AppFailedMusicDownload,
    CurlFailedEasyInit,
    DumpFailedNetworkUpload,

    UnzOpen2_64,
    UnzGetGlobalInfo64,
    UnzLocateFile,
    UnzGoToFirstFile,
    UnzGoToNextFile,
    UnzOpenCurrentFile,
    UnzGetCurrentFileInfo64,
    UnzReadCurrentFile,

    ZipOpen2_64,
    ZipOpenNewFileInZip,
    ZipWriteInFileInZip,

    MmzBadLocalHeaderSig,
    MmzBadLocalHeaderRead,

    FileBrowserFailedUpload,
    FileBrowserDirNotDaybreak,

    AppstoreFailedZipDownload,
    AppstoreFailedMd5,
    AppstoreFailedParseManifest,

    GameBadReadForDump,
    GameEmptyMetaEntries,
    GameMultipleKeysFound,
    GameNoNspEntriesBuilt,
    GameMoveNoAppManager,
    GameMoveNotEnoughSpace,

    KeyMissingNcaKeyArea,
    KeyMissingTitleKek,
    KeyMissingMasterKey,
    KeyFailedDecyptETicketDeviceKey,

    NcaFailedNcaHeaderHashVerify,
    NcaBadSigKeyGen,
    NcaBadMagic,

    GcBadReadForDump,
    GcEmptyGamecard,
    GcBadXciMagic,
    GcBadXciRomSize,
    GcFailedToGetSecurityInfo,

    GhdlEmptyAsset,
    GhdlFailedToDownloadAsset,
    GhdlFailedToDownloadAssetJson,
    GhdlFileTooLarge,

    ThemezerFailedToDownloadThemeMeta,
    ThemezerFailedToDownloadTheme,

    MainFailedToDownloadUpdate,

    UsbDsBadDeviceSpeed,

    NspBadMagic,
    XciBadMagic,
    XciSecurePartitionNotFound,

    EsBadTitleKeyType,
    EsPersonalisedTicketDeviceIdMissmatch,
    EsFailedDecryptPersonalisedTicket,
    EsBadDecryptedPersonalisedTicketSize,
    EsBadTicketSize,
    // found ticket has missmatching rights_id from it's name.
    EsInvalidTicketBadRightsId,
    EsInvalidTicketFromatVersion,
    EsInvalidTicketKeyType,
    EsInvalidTicketKeyRevision,

    OwoBadArgs,

    UsbCancelled,
    UsbBadMagic,
    UsbBadVersion,
    UsbBadCount,
    UsbBadBufferAlign,
    UsbBadTransferSize,
    UsbEmptyTransferSize,
    UsbOverflowTransferSize,
    UsbBadTotalSize,
    // the goldleaf host rejected a command; its own code is in the log.
    UsbGoldleafFailed,

    UsbUploadBadMagic,
    UsbUploadExit,
    UsbUploadBadCount,
    UsbUploadBadTransferSize,
    UsbUploadBadTotalSize,
    UsbUploadBadCommand,

    // unkown container for the source provided.
    YatiContainerNotFound,
    // nca required by the cnmt but not found in collection.
    YatiNcaNotFound,
    YatiInvalidNcaReadSize,
    YatiInvalidNcaSigKeyGen,
    YatiInvalidNcaMagic,
    YatiInvalidNcaSignature0,
    YatiInvalidNcaSignature1,
    // invalid sha256 over the entire nca.
    YatiInvalidNcaSha256,
    // section could not be found.
    YatiNczSectionNotFound,
    // section count == 0.
    YatiInvalidNczSectionCount,
    // block could not be found.
    YatiNczBlockNotFound,
    // block version != 2.
    YatiInvalidNczBlockVersion,
    // block type != 1.
    YatiInvalidNczBlockType,
    // block count == 0.
    YatiInvalidNczBlockTotal,
    // block size exponent < 14 || > 32.
    YatiInvalidNczBlockSizeExponent,
    // zstd error while decompressing ncz.
    YatiInvalidNczZstdError,
    // nca has rights_id but matching ticket wasn't found.
    YatiTicketNotFound,
    // found ticket has missmatching rights_id from it's name.
    YatiInvalidTicketBadRightsId,
    // cert not found for the ticket.
    YatiCertNotFound,
    // unable to fetch header from ncm database.
    YatiNcmDbCorruptHeader,
    // unable to total infos from ncm database.
    YatiNcmDbCorruptInfos,
    SmbConnectionFailed,
    SmbNotSupported,
    SaveSyncFailed,
    // stream returned zero bytes without an error while more data was expected.
    // NOTE: new codes must be appended here, inserting mid-enum renumbers
    // every code below the insertion point.
    StreamUnexpectedEof,
    // failed to delete the currently installed interface translation while
    // installing a new one; the ui offers remove + reboot instead.
    TranslationRemoveExistingFailed,
    // the mtp/stream host stopped sending data mid-transfer without closing the
    // file (copy cancelled on the pc, cable pulled, host-side timeout). distinct
    // from TransferCancelled, which is the user cancelling on the console.
    TransferInterrupted,

    // background clock sync.
    NtpNoConnection,
    NtpResolveFailed,
    NtpSocketFailed,
    NtpSendFailed,
    NtpRecvFailed,
    // reply was not a well formed server response (wrong mode, kiss-o'-death,
    // or a timestamp outside any plausible range).
    NtpBadReply,
    // neither writable time service accepted the user-clock update.
    NtpSetTimeFailed,

    // the console has no network connection and one could not be brought up.
    // the ui turns this into a plain "your internet is off" message instead of
    // showing a raw nifm code.
    NetNoConnection,
};

#define MAKE_SPHAIRA_RESULT_ENUM(x) Result_##x =  MAKERESULT(Module_Sphaira, (Result)SphairaResult::x)

enum : Result {
    MAKE_SPHAIRA_RESULT_ENUM(TransferCancelled),
    MAKE_SPHAIRA_RESULT_ENUM(StreamBadSeek),
    MAKE_SPHAIRA_RESULT_ENUM(FsTooManyEntries),
    MAKE_SPHAIRA_RESULT_ENUM(FsNewPathTooLarge),
    MAKE_SPHAIRA_RESULT_ENUM(FsInvalidType),
    MAKE_SPHAIRA_RESULT_ENUM(FsEmpty),
    MAKE_SPHAIRA_RESULT_ENUM(FsAlreadyRoot),
    MAKE_SPHAIRA_RESULT_ENUM(FsNoCurrentPath),
    MAKE_SPHAIRA_RESULT_ENUM(FsBrokenCurrentPath),
    MAKE_SPHAIRA_RESULT_ENUM(FsIndexOutOfBounds),
    MAKE_SPHAIRA_RESULT_ENUM(FsFsNotActive),
    MAKE_SPHAIRA_RESULT_ENUM(FsNewPathEmpty),
    MAKE_SPHAIRA_RESULT_ENUM(FsLoadingCancelled),
    MAKE_SPHAIRA_RESULT_ENUM(FsBrokenRoot),
    MAKE_SPHAIRA_RESULT_ENUM(FsUnknownStdioError),
    MAKE_SPHAIRA_RESULT_ENUM(FsReadOnly),
    MAKE_SPHAIRA_RESULT_ENUM(FsNotActive),
    MAKE_SPHAIRA_RESULT_ENUM(FsFailedStdioStat),
    MAKE_SPHAIRA_RESULT_ENUM(FsFailedStdioOpendir),
    MAKE_SPHAIRA_RESULT_ENUM(NroBadMagic),
    MAKE_SPHAIRA_RESULT_ENUM(NroBadSize),
    MAKE_SPHAIRA_RESULT_ENUM(AppFailedMusicDownload),
    MAKE_SPHAIRA_RESULT_ENUM(CurlFailedEasyInit),
    MAKE_SPHAIRA_RESULT_ENUM(DumpFailedNetworkUpload),
    MAKE_SPHAIRA_RESULT_ENUM(UnzOpen2_64),
    MAKE_SPHAIRA_RESULT_ENUM(UnzGetGlobalInfo64),
    MAKE_SPHAIRA_RESULT_ENUM(UnzLocateFile),
    MAKE_SPHAIRA_RESULT_ENUM(UnzGoToFirstFile),
    MAKE_SPHAIRA_RESULT_ENUM(UnzGoToNextFile),
    MAKE_SPHAIRA_RESULT_ENUM(UnzOpenCurrentFile),
    MAKE_SPHAIRA_RESULT_ENUM(UnzGetCurrentFileInfo64),
    MAKE_SPHAIRA_RESULT_ENUM(UnzReadCurrentFile),
    MAKE_SPHAIRA_RESULT_ENUM(ZipOpen2_64),
    MAKE_SPHAIRA_RESULT_ENUM(ZipOpenNewFileInZip),
    MAKE_SPHAIRA_RESULT_ENUM(ZipWriteInFileInZip),
    MAKE_SPHAIRA_RESULT_ENUM(MmzBadLocalHeaderSig),
    MAKE_SPHAIRA_RESULT_ENUM(MmzBadLocalHeaderRead),
    MAKE_SPHAIRA_RESULT_ENUM(FileBrowserFailedUpload),
    MAKE_SPHAIRA_RESULT_ENUM(FileBrowserDirNotDaybreak),
    MAKE_SPHAIRA_RESULT_ENUM(AppstoreFailedZipDownload),
    MAKE_SPHAIRA_RESULT_ENUM(AppstoreFailedMd5),
    MAKE_SPHAIRA_RESULT_ENUM(AppstoreFailedParseManifest),
    MAKE_SPHAIRA_RESULT_ENUM(GameBadReadForDump),
    MAKE_SPHAIRA_RESULT_ENUM(GameEmptyMetaEntries),
    MAKE_SPHAIRA_RESULT_ENUM(GameMultipleKeysFound),
    MAKE_SPHAIRA_RESULT_ENUM(GameNoNspEntriesBuilt),
    MAKE_SPHAIRA_RESULT_ENUM(GameMoveNoAppManager),
    MAKE_SPHAIRA_RESULT_ENUM(GameMoveNotEnoughSpace),
    MAKE_SPHAIRA_RESULT_ENUM(KeyMissingNcaKeyArea),
    MAKE_SPHAIRA_RESULT_ENUM(KeyMissingTitleKek),
    MAKE_SPHAIRA_RESULT_ENUM(KeyMissingMasterKey),
    MAKE_SPHAIRA_RESULT_ENUM(KeyFailedDecyptETicketDeviceKey),
    MAKE_SPHAIRA_RESULT_ENUM(NcaFailedNcaHeaderHashVerify),
    MAKE_SPHAIRA_RESULT_ENUM(NcaBadSigKeyGen),
    MAKE_SPHAIRA_RESULT_ENUM(NcaBadMagic),
    MAKE_SPHAIRA_RESULT_ENUM(GcBadReadForDump),
    MAKE_SPHAIRA_RESULT_ENUM(GcEmptyGamecard),
    MAKE_SPHAIRA_RESULT_ENUM(GcBadXciMagic),
    MAKE_SPHAIRA_RESULT_ENUM(GcBadXciRomSize),
    MAKE_SPHAIRA_RESULT_ENUM(GcFailedToGetSecurityInfo),
    MAKE_SPHAIRA_RESULT_ENUM(GhdlEmptyAsset),
    MAKE_SPHAIRA_RESULT_ENUM(GhdlFailedToDownloadAsset),
    MAKE_SPHAIRA_RESULT_ENUM(GhdlFailedToDownloadAssetJson),
    MAKE_SPHAIRA_RESULT_ENUM(GhdlFileTooLarge),
    MAKE_SPHAIRA_RESULT_ENUM(ThemezerFailedToDownloadThemeMeta),
    MAKE_SPHAIRA_RESULT_ENUM(ThemezerFailedToDownloadTheme),
    MAKE_SPHAIRA_RESULT_ENUM(MainFailedToDownloadUpdate),
    MAKE_SPHAIRA_RESULT_ENUM(UsbDsBadDeviceSpeed),
    MAKE_SPHAIRA_RESULT_ENUM(NspBadMagic),
    MAKE_SPHAIRA_RESULT_ENUM(XciBadMagic),
    MAKE_SPHAIRA_RESULT_ENUM(XciSecurePartitionNotFound),
    MAKE_SPHAIRA_RESULT_ENUM(EsBadTitleKeyType),
    MAKE_SPHAIRA_RESULT_ENUM(EsPersonalisedTicketDeviceIdMissmatch),
    MAKE_SPHAIRA_RESULT_ENUM(EsFailedDecryptPersonalisedTicket),
    MAKE_SPHAIRA_RESULT_ENUM(EsBadDecryptedPersonalisedTicketSize),
    MAKE_SPHAIRA_RESULT_ENUM(EsBadTicketSize),
    MAKE_SPHAIRA_RESULT_ENUM(EsInvalidTicketBadRightsId),
    MAKE_SPHAIRA_RESULT_ENUM(EsInvalidTicketFromatVersion),
    MAKE_SPHAIRA_RESULT_ENUM(EsInvalidTicketKeyType),
    MAKE_SPHAIRA_RESULT_ENUM(EsInvalidTicketKeyRevision),
    MAKE_SPHAIRA_RESULT_ENUM(OwoBadArgs),
    MAKE_SPHAIRA_RESULT_ENUM(UsbCancelled),
    MAKE_SPHAIRA_RESULT_ENUM(UsbBadMagic),
    MAKE_SPHAIRA_RESULT_ENUM(UsbBadVersion),
    MAKE_SPHAIRA_RESULT_ENUM(UsbBadCount),
    MAKE_SPHAIRA_RESULT_ENUM(UsbBadBufferAlign),
    MAKE_SPHAIRA_RESULT_ENUM(UsbBadTransferSize),
    MAKE_SPHAIRA_RESULT_ENUM(UsbEmptyTransferSize),
    MAKE_SPHAIRA_RESULT_ENUM(UsbOverflowTransferSize),
    MAKE_SPHAIRA_RESULT_ENUM(UsbGoldleafFailed),
    MAKE_SPHAIRA_RESULT_ENUM(UsbUploadBadMagic),
    MAKE_SPHAIRA_RESULT_ENUM(UsbUploadExit),
    MAKE_SPHAIRA_RESULT_ENUM(UsbUploadBadCount),
    MAKE_SPHAIRA_RESULT_ENUM(UsbUploadBadTransferSize),
    MAKE_SPHAIRA_RESULT_ENUM(UsbUploadBadTotalSize),
    MAKE_SPHAIRA_RESULT_ENUM(UsbUploadBadCommand),
    MAKE_SPHAIRA_RESULT_ENUM(YatiContainerNotFound),
    MAKE_SPHAIRA_RESULT_ENUM(YatiNcaNotFound),
    MAKE_SPHAIRA_RESULT_ENUM(YatiInvalidNcaReadSize),
    MAKE_SPHAIRA_RESULT_ENUM(YatiInvalidNcaSigKeyGen),
    MAKE_SPHAIRA_RESULT_ENUM(YatiInvalidNcaMagic),
    MAKE_SPHAIRA_RESULT_ENUM(YatiInvalidNcaSignature0),
    MAKE_SPHAIRA_RESULT_ENUM(YatiInvalidNcaSignature1),
    MAKE_SPHAIRA_RESULT_ENUM(YatiInvalidNcaSha256),
    MAKE_SPHAIRA_RESULT_ENUM(YatiNczSectionNotFound),
    MAKE_SPHAIRA_RESULT_ENUM(YatiInvalidNczSectionCount),
    MAKE_SPHAIRA_RESULT_ENUM(YatiNczBlockNotFound),
    MAKE_SPHAIRA_RESULT_ENUM(YatiInvalidNczBlockVersion),
    MAKE_SPHAIRA_RESULT_ENUM(YatiInvalidNczBlockType),
    MAKE_SPHAIRA_RESULT_ENUM(YatiInvalidNczBlockTotal),
    MAKE_SPHAIRA_RESULT_ENUM(YatiInvalidNczBlockSizeExponent),
    MAKE_SPHAIRA_RESULT_ENUM(YatiInvalidNczZstdError),
    MAKE_SPHAIRA_RESULT_ENUM(YatiTicketNotFound),
    MAKE_SPHAIRA_RESULT_ENUM(YatiInvalidTicketBadRightsId),
    MAKE_SPHAIRA_RESULT_ENUM(YatiCertNotFound),
    MAKE_SPHAIRA_RESULT_ENUM(YatiNcmDbCorruptHeader),
    MAKE_SPHAIRA_RESULT_ENUM(YatiNcmDbCorruptInfos),
    MAKE_SPHAIRA_RESULT_ENUM(SmbConnectionFailed),
    MAKE_SPHAIRA_RESULT_ENUM(SmbNotSupported),
    MAKE_SPHAIRA_RESULT_ENUM(SaveSyncFailed),
    MAKE_SPHAIRA_RESULT_ENUM(StreamUnexpectedEof),
    MAKE_SPHAIRA_RESULT_ENUM(TranslationRemoveExistingFailed),
    MAKE_SPHAIRA_RESULT_ENUM(TransferInterrupted),
    MAKE_SPHAIRA_RESULT_ENUM(NtpNoConnection),
    MAKE_SPHAIRA_RESULT_ENUM(NtpResolveFailed),
    MAKE_SPHAIRA_RESULT_ENUM(NtpSocketFailed),
    MAKE_SPHAIRA_RESULT_ENUM(NtpSendFailed),
    MAKE_SPHAIRA_RESULT_ENUM(NtpRecvFailed),
    MAKE_SPHAIRA_RESULT_ENUM(NtpBadReply),
    MAKE_SPHAIRA_RESULT_ENUM(NtpSetTimeFailed),
    MAKE_SPHAIRA_RESULT_ENUM(NetNoConnection),
};

#undef MAKE_SPHAIRA_RESULT_ENUM

#define R_SUCCEED() return (Result)0

#define R_THROW(_rc) return _rc

#define R_TRY_RESULT(r, _result) { \
    if (const auto _rc = (r); R_FAILED(_rc)) { \
        R_THROW(_result); \
    } \
}

#define R_TRY(r) { \
    if (const auto _rc = (r); R_FAILED(_rc)) { \
        R_THROW(_rc); \
    } \
}

#define R_UNLESS(expr, res) { \
    if (!(expr)) { \
        R_THROW(res); \
    } \
}

#define CONCATENATE_IMPL(s1, s2) s1##s2
#define CONCATENATE(s1, s2) CONCATENATE_IMPL(s1, s2)
#define ANONYMOUS_VARIABLE(pref) CONCATENATE(pref, __COUNTER__)

#define ON_SCOPE_EXIT(_f) std::experimental::scope_exit ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE_){[&] { _f; }};
// #define ON_SCOPE_FAIL(_f) std::experimental::scope_exit ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE_){[&] { if (R_FAILED(rc)) { _f; } }};
// #define ON_SCOPE_SUCCESS(_f) std::experimental::scope_exit ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE_){[&] { if (R_SUCCEEDED(rc)) { _f; } }};

// threading helpers.
#define PRIO_PREEMPTIVE 0x3B

// threading affinity, use with svcSetThreadCoreMask().
#define THREAD_AFFINITY_CORE0 BIT(0)
#define THREAD_AFFINITY_CORE1 BIT(1)
#define THREAD_AFFINITY_CORE2 BIT(2)
#define THREAD_AFFINITY_DEFAULT(core) (BIT(core)|THREAD_AFFINITY_CORE1|THREAD_AFFINITY_CORE2)
#define THREAD_AFFINITY_ALL (THREAD_AFFINITY_CORE0|THREAD_AFFINITY_CORE1|THREAD_AFFINITY_CORE2)

// mutex helpers.
struct ScopedRwLock {
    ScopedRwLock(RwLock* lock, bool write) : m_lock{lock}, m_write{write} {
        if (m_write) {
            rwlockWriteLock(m_lock);
        } else {
            rwlockReadLock(m_lock);
        }
    }

    ~ScopedRwLock() {
        if (m_write) {
            rwlockWriteUnlock(m_lock);
        } else {
            rwlockReadUnlock(m_lock);
        }
    }

    ScopedRwLock(const ScopedRwLock&) = delete;
    void operator=(const ScopedRwLock&) = delete;

private:
    RwLock* const m_lock;
    bool const m_write;
};

struct ScopedMutex {
    ScopedMutex(Mutex* mutex) : m_mutex{mutex} {
        mutexLock(m_mutex);
    }

    ~ScopedMutex() {
        mutexUnlock(m_mutex);
    }

    ScopedMutex(const ScopedMutex&) = delete;
    void operator=(const ScopedMutex&) = delete;

private:
    Mutex* const m_mutex;
};

#define SCOPED_MUTEX(mutex) ScopedMutex ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE_){mutex}

#define SCOPED_RWLOCK(lock, write) ScopedRwLock ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE_){lock, write}
