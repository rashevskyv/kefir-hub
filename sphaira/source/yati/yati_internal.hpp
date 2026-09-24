#pragma once

#include "yati/yati.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/ncz.hpp"
#include "yati/nx/keys.hpp"
#include "app.hpp"
#include "defines.hpp"

#include <switch.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace sphaira::yati::detail {

constexpr NcmStorageId NCM_STORAGE_IDS[]{
    NcmStorageId_BuiltInUser,
    NcmStorageId_SdCard,
};

constexpr u32 KEYGEN_LIMIT = 0x20;

struct NcaCollection : container::CollectionEntry {
    nca::Header header{};
    // NcmContentType
    u8 type{};
    NcmContentId content_id{};
    NcmPlaceHolderId placeholder_id{};
    // new hash of the nca..
    u8 hash[SHA256_HASH_SIZE]{};
    // set true if nca has been modified.
    bool modified{};
    // set if the nca was not installed.
    bool skipped{};
};

struct CnmtCollection : NcaCollection {
    // list of all nca's the cnmt depends on
    std::vector<NcaCollection> ncas{};
    // only set if any of the nca's depend on a ticket / cert.
    // if set, the ticket / cert will be installed once all nca's have installed.
    std::vector<FsRightsId> rights_id{};

    NcmContentMetaHeader meta_header{};
    NcmContentMetaKey key{};
    NcmContentInfo content_info{};
    std::vector<u8> extended_header{};
    std::vector<NcmPackagedContentInfo> infos{};
    u32 original_required_system_version{};
};

struct TikCollection {
    // raw data of the ticket / cert.
    std::vector<u8> ticket{};
    std::vector<u8> cert{};
    // set via the name of the ticket.
    FsRightsId rights_id{};
    // retrieved via the master key set in nca.
    u8 key_gen{};
    // set if ticket is required by an nca.
    bool required{};
    // set if ticket has already been patched.
    bool patched{};
};

struct Yati;

const u64 INFLATE_BUFFER_MAX = 1024*1024*4;

struct ThreadBuffer {
    ThreadBuffer() {
        buf.reserve(INFLATE_BUFFER_MAX);
    }

    std::vector<u8> buf;
    s64 off;
};

template<std::size_t Size>
struct RingBuf {
private:
    ThreadBuffer buf[Size]{};
    unsigned r_index{};
    unsigned w_index{};

    static_assert((sizeof(RingBuf::buf) & (sizeof(RingBuf::buf) - 1)) == 0, "Must be power of 2!");

public:
    void ringbuf_reset() {
        this->r_index = this->w_index;
    }

    unsigned ringbuf_capacity() const {
        return sizeof(this->buf) / sizeof(this->buf[0]);
    }

    unsigned ringbuf_size() const {
        return (this->w_index - this->r_index) % (ringbuf_capacity() * 2U);
    }

    unsigned ringbuf_free() const {
        return ringbuf_capacity() - ringbuf_size();
    }

    void ringbuf_push(std::vector<u8>& buf_in, s64 off_in) {
        auto& value = this->buf[this->w_index % ringbuf_capacity()];
        value.off = off_in;
        std::swap(value.buf, buf_in);

        this->w_index = (this->w_index + 1U) % (ringbuf_capacity() * 2U);
    }

    void ringbuf_pop(std::vector<u8>& buf_out, s64& off_out) {
        auto& value = this->buf[this->r_index % ringbuf_capacity()];
        off_out = value.off;
        std::swap(value.buf, buf_out);

        this->r_index = (this->r_index + 1U) % (ringbuf_capacity() * 2U);
    }
};

struct ThreadData {
    ThreadData(Yati* _yati, std::span<TikCollection> _tik, NcaCollection* _nca)
    : yati{_yati}, tik{_tik}, nca{_nca} {
        mutexInit(std::addressof(read_mutex));
        mutexInit(std::addressof(write_mutex));

        condvarInit(std::addressof(can_read));
        condvarInit(std::addressof(can_decompress));
        condvarInit(std::addressof(can_decompress_write));
        condvarInit(std::addressof(can_write));

        ueventCreate(&m_uevent_done, false);
        ueventCreate(&m_uevent_progres, true);

        sha256ContextCreate(&sha256);
        // this will be updated with the actual size from nca header.
        write_size = nca->size;

        // reduce buffer size to preve
        if (App::IsFileBaseEmummc()) {
            read_buffer_size = 1024 * 512;
        } else {
            read_buffer_size = 1024*1024*4;
        }

        max_buffer_size = std::max(read_buffer_size, INFLATE_BUFFER_MAX);
    }

    auto GetResults() volatile -> Result;
    void WakeAllThreads();

    auto IsAnyRunning() volatile const -> bool {
        // liveness is the per-thread *_running flags; decompress_result is a
        // Result and was being read here by mistake (a failed-but-exited
        // decompress thread would keep this true, a still-running one with a
        // clean result could read false).
        return read_running || decompress_running || write_running;
    }

    auto GetWriteOffset() volatile const -> s64 {
        return write_offset;
    }

    auto GetReadOffset() volatile const -> s64 {
        return read_offset;
    }

    auto GetWriteSize() volatile const -> s64 {
        return write_size;
    }

    auto GetDoneEvent() {
        return &m_uevent_done;
    }

    auto GetProgressEvent() {
        return &m_uevent_progres;
    }

    // NOTE: WakeAllThreads() only signals the condvars (it does not touch the
    // mutexes), so it is safe to call here without owning read_mutex/write_mutex.
    // A blocked peer is either parked in condvarWait (released its mutex) and is
    // woken directly, or it is mid-critical-section and will release the mutex on
    // its own. Grabbing the mutexes here (as an earlier revision did) races with
    // the orchestrator thread, which also calls WakeAllThreads() without owning
    // them, and corrupts HOS mutex ownership -> install deadlock / crash.
    void SetReadResult(Result result) {
        read_result = result;
        if (R_FAILED(result)) {
            ueventSignal(GetDoneEvent());
        }
        WakeAllThreads();
    }

    void SetDecompressResult(Result result) {
        decompress_result = result;
        if (R_FAILED(result)) {
            ueventSignal(GetDoneEvent());
        }
        WakeAllThreads();
    }

    void SetWriteResult(Result result) {
        write_result = result;
        ueventSignal(GetDoneEvent());
        WakeAllThreads();
    }

    Result Read(void* buf, s64 size, u64* bytes_read);

    Result SetDecompressBuf(std::vector<u8>& buf, s64 off, s64 size) {
        buf.resize(size);

        // the unlock must be registered before the wait loop: the early
        // returns inside it (peer thread died, condvarWait failed) used to
        // leave the mutex held, and the caller's next trip into one of these
        // functions self-deadlocked on it -- a hang no condvar wake can fix.
        mutexLock(std::addressof(read_mutex));
        ON_SCOPE_EXIT(mutexUnlock(std::addressof(read_mutex)));
        // every wait loop below checks GetResults() *inside* the loop: a peer
        // that dies while this ring is full never drains it, so a parked
        // thread only ever leaves via a wake + failed result check. checking
        // after the loop only (as before) parked the decompress thread
        // forever once the write thread exited on a failed read -- the B
        // button hang. the running-flag escape names the thread that drains
        // (or fills) *this* ring; two of them named the wrong thread.
        while (!read_buffers.ringbuf_free()) {
            R_TRY(GetResults());
            if (!decompress_running) {
                R_SUCCEED();
            }
            R_TRY(condvarWait(std::addressof(can_read), std::addressof(read_mutex)));
        }

        R_TRY(GetResults());
        read_buffers.ringbuf_push(buf, off);
        return condvarWakeOne(std::addressof(can_decompress));
    }

    Result GetDecompressBuf(std::vector<u8>& buf_out, s64& off_out) {
        mutexLock(std::addressof(read_mutex));
        ON_SCOPE_EXIT(mutexUnlock(std::addressof(read_mutex)));
        while (!read_buffers.ringbuf_size()) {
            R_TRY(GetResults());
            if (!read_running) {
                buf_out.resize(0);
                R_SUCCEED();
            }
            R_TRY(condvarWait(std::addressof(can_decompress), std::addressof(read_mutex)));
        }

        R_TRY(GetResults());
        read_buffers.ringbuf_pop(buf_out, off_out);
        return condvarWakeOne(std::addressof(can_read));
    }

    Result SetWriteBuf(std::vector<u8>& buf, s64 size, bool skip_verify) {
        buf.resize(size);
        if (!skip_verify) {
            sha256ContextUpdate(std::addressof(sha256), buf.data(), buf.size());
        }

        mutexLock(std::addressof(write_mutex));
        ON_SCOPE_EXIT(mutexUnlock(std::addressof(write_mutex)));
        while (!write_buffers.ringbuf_free()) {
            R_TRY(GetResults());
            if (!write_running) {
                R_SUCCEED();
            }
            R_TRY(condvarWait(std::addressof(can_decompress_write), std::addressof(write_mutex)));
        }

        R_TRY(GetResults());
        write_buffers.ringbuf_push(buf, 0);
        return condvarWakeOne(std::addressof(can_write));
    }

    Result GetWriteBuf(std::vector<u8>& buf_out, s64& off_out) {
        mutexLock(std::addressof(write_mutex));
        ON_SCOPE_EXIT(mutexUnlock(std::addressof(write_mutex)));
        while (!write_buffers.ringbuf_size()) {
            R_TRY(GetResults());
            if (!decompress_running) {
                buf_out.resize(0);
                R_SUCCEED();
            }
            R_TRY(condvarWait(std::addressof(can_write), std::addressof(write_mutex)));
        }

        R_TRY(GetResults());
        write_buffers.ringbuf_pop(buf_out, off_out);
        return condvarWakeOne(std::addressof(can_decompress_write));
    }

    // these need to be copied
    Yati* yati{};
    std::span<TikCollection> tik{};
    NcaCollection* nca{};

    // these need to be created
    Mutex read_mutex{};
    Mutex write_mutex{};

    CondVar can_read{};
    CondVar can_decompress{};
    CondVar can_decompress_write{};
    CondVar can_write{};

    UEvent m_uevent_done{};
    UEvent m_uevent_progres{};

    RingBuf<4> read_buffers{};
    RingBuf<4> write_buffers{};

    ncz::BlockHeader ncz_block_header{};
    std::vector<ncz::Section> ncz_sections{};
    std::vector<ncz::BlockInfo> ncz_blocks{};

    Sha256Context sha256{};

    u64 read_buffer_size{};
    u64 max_buffer_size{};

    // these are shared between threads
    std::atomic<s64> read_offset{};
    std::atomic<s64> decompress_offset{};
    std::atomic<s64> write_offset{};
    std::atomic<s64> write_size{};

    std::atomic<Result> read_result{};
    std::atomic<Result> decompress_result{};
    std::atomic<Result> write_result{};

    std::atomic_bool read_running{true};
    std::atomic_bool decompress_running{true};
    std::atomic_bool write_running{true};
};

struct Yati {
    Yati(ui::InstallProgress*, source::Base*);
    ~Yati();

    Result Setup(const ConfigOverride& override);
    Result InstallNca(std::span<TikCollection> tickets, NcaCollection& nca);
    Result InstallNcaInternal(std::span<TikCollection> tickets, NcaCollection& nca);
    Result InstallCnmtNca(std::span<TikCollection> tickets, CnmtCollection& cnmt, const container::Collections& collections);

    Result readFuncInternal(ThreadData* t);
    Result decompressFuncInternal(ThreadData* t);
    Result writeFuncInternal(ThreadData* t);

    Result ParseTicketsIntoCollection(std::vector<TikCollection>& tickets, const container::Collections& collections, bool read_data);
    Result GetLatestVersion(const CnmtCollection& cnmt, u32& version_out, bool& skip);
    Result ShouldSkip(const CnmtCollection& cnmt, bool& skip);
    Result ImportTickets(std::span<TikCollection> tickets);
    Result RemoveInstalledNcas(const CnmtCollection& cnmt);
    Result RegisterNcasAndPushRecord(const CnmtCollection& cnmt, u32 latest_version_num);


// private:
    ui::InstallProgress* pbox{};
    source::Base* source{};

    // for all content storages
    NcmContentStorage ncm_cs[2]{};
    NcmContentMetaDatabase ncm_db[2]{};
    // these point to the above struct
    NcmContentStorage cs{};
    NcmContentMetaDatabase db{};
    NcmStorageId storage_id{};

    Service ns_app{};
    std::unique_ptr<container::Base> container{};
    Config config{};
    keys::Keys keys{};
};

Result InstallInternal(ui::InstallProgress* pbox, source::Base* source, const container::Collections& collections, const ConfigOverride& override);
Result InstallInternalStream(ui::InstallProgress* pbox, source::Base* source, container::Collections collections, const ConfigOverride& override);

void readFunc(void* d);
void decompressFunc(void* d);
void writeFunc(void* d);

Result HasRequiredTicket(const nca::Header& header, std::span<TikCollection> tik);

} // namespace sphaira::yati::detail
