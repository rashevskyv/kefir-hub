#include "yati_internal.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/ncz.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/keys.hpp"
#include "yati/nx/crypto.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "log.hpp"
#include "defines.hpp"
#include "utils/utils.hpp"

#include <switch.h>
#include <zstd.h>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace sphaira::yati::detail {
namespace {

auto isRightsIdValid(FsRightsId id) -> bool {
    FsRightsId empty_id{};
    return 0 != std::memcmp(std::addressof(id), std::addressof(empty_id), sizeof(id));
}

auto GetTicketCollection(const nca::Header& header, std::span<TikCollection> tik) -> TikCollection* {
    TikCollection* ticket{};

    if (isRightsIdValid(header.rights_id)) {
        auto it = std::ranges::find_if(tik, [&header](auto& e){
            return !std::memcmp(&header.rights_id, &e.rights_id, sizeof(e.rights_id));
        });

        if (it != tik.end()) {
            it->required = true;
            it->key_gen = header.key_gen;
            ticket = &(*it);
        }
    }

    return ticket;
}

Result HasRequiredTicket(const nca::Header& header, TikCollection* ticket) {
    if (isRightsIdValid(header.rights_id)) {
        log_write("looking for ticket %s\n", ::sphaira::utils::hexIdToStr(header.rights_id).str);
        R_UNLESS(ticket, Result_YatiTicketNotFound);
        log_write("ticket found\n");
    }
    R_SUCCEED();
}

} // namespace

Result HasRequiredTicket(const nca::Header& header, std::span<TikCollection> tik) {
    auto ticket = GetTicketCollection(header, tik);
    return HasRequiredTicket(header, ticket);
}

auto ThreadData::GetResults() volatile -> Result {
    R_TRY(yati->pbox->CheckCancelled());
    R_TRY(read_result.load());
    R_TRY(decompress_result.load());
    R_TRY(write_result.load());
    R_SUCCEED();
}

// Wakes every thread parked on one of the pipeline condvars so it re-checks its
// predicate (typically GetResults(), which surfaces a failure/cancel and lets
// the thread unwind). This must NOT lock or unlock read_mutex/write_mutex: it is
// called both by the worker threads (SetReadResult/SetDecompressResult/
// SetWriteResult) and by the orchestrator's cleanup loop, none of which own the
// mutexes at that point. libnx condvarWake* does not require the associated mutex
// to be held, so signalling alone is correct and race-free.
void ThreadData::WakeAllThreads() {
    condvarWakeAll(std::addressof(can_read));
    condvarWakeAll(std::addressof(can_decompress));
    condvarWakeAll(std::addressof(can_decompress_write));
    condvarWakeAll(std::addressof(can_write));
}

Result ThreadData::Read(void* buf, s64 size, u64* bytes_read) {
    size = std::min<s64>(size, nca->size - read_offset);
    const auto rc = yati->source->Read(buf, nca->offset + read_offset, size, bytes_read);
    if (R_FAILED(rc) || *bytes_read == 0) {
        log_write("[YATI] ThreadData::Read: off=%ld, size=%ld, read=%lu, rc=0x%X\n", nca->offset + read_offset, size, *bytes_read, rc);
    }
    R_TRY(rc);

    read_offset += *bytes_read;
    return rc;
}

// read thread reads all data from the source, it also handles
// parsing ncz headers, sections and reading ncz blocks
Result Yati::readFuncInternal(ThreadData* t) {
    ON_SCOPE_EXIT( t->read_running = false; );

    // the main buffer which data is read into.
    std::vector<u8> buf;
    // workaround ncz block reading ahead. if block isn't found, we usually
    // would seek back to the offset, however this is not possible in stream
    // mode, so we instead store the data to the temp buffer and pre-pend it.
    std::vector<u8> temp_buf;
    buf.reserve(t->max_buffer_size);
    temp_buf.reserve(t->max_buffer_size);

    while (t->read_offset < t->nca->size && R_SUCCEEDED(t->GetResults())) {
        const auto buffer_offset = t->read_offset.load();

        // read more data
        s64 read_size = t->read_buffer_size;
        if (!t->read_offset) {
            read_size = NCZ_SECTION_OFFSET;
        }

        s64 buf_offset = 0;
        if (!temp_buf.empty()) {
            buf = temp_buf;
            read_size -= temp_buf.size();
            buf_offset = temp_buf.size();
            temp_buf.clear();
        }

        u64 bytes_read{};
        buf.resize(buf_offset + read_size);
        R_TRY(t->Read(buf.data() + buf_offset, read_size, std::addressof(bytes_read)));
        auto buf_size = buf_offset + bytes_read;
        if (!bytes_read) {
            break;
        }

        // read enough bytes for ncz, check magic
        if (t->read_offset == NCZ_SECTION_OFFSET) {
            // check for ncz section header.
            ncz::Header header{};
            std::memcpy(std::addressof(header), buf.data() + 0x4000, sizeof(header));
            if (header.magic == NCZ_SECTION_MAGIC) {
                // validate section header.
                R_UNLESS(header.total_sections, Result_YatiInvalidNczSectionCount);

                buf_size = 0x4000;
                log_write("found ncz, total number of sections: %zu\n", header.total_sections);
                t->ncz_sections.resize(header.total_sections);
                R_TRY(t->Read(t->ncz_sections.data(), t->ncz_sections.size() * sizeof(ncz::Section), std::addressof(bytes_read)));

                // check for ncz block header.
                R_TRY(t->Read(std::addressof(t->ncz_block_header), sizeof(t->ncz_block_header), std::addressof(bytes_read)));
                if (t->ncz_block_header.magic != NCZ_BLOCK_MAGIC) {
                    // didn't find block, keep the data we just read in the temp buffer.
                    temp_buf.resize(sizeof(t->ncz_block_header));
                    std::memcpy(temp_buf.data(), std::addressof(t->ncz_block_header), temp_buf.size());
                    log_write("storing temp data of size: %zu\n", temp_buf.size());
                } else {
                    // validate block header.
                    R_UNLESS(t->ncz_block_header.version == 0x2, Result_YatiInvalidNczBlockVersion);
                    R_UNLESS(t->ncz_block_header.type == 0x1, Result_YatiInvalidNczBlockType);
                    R_UNLESS(t->ncz_block_header.total_blocks, Result_YatiInvalidNczBlockTotal);
                    R_UNLESS(t->ncz_block_header.block_size_exponent >= 14 && t->ncz_block_header.block_size_exponent <= 32, Result_YatiInvalidNczBlockSizeExponent);

                    // read blocks (array of block sizes).
                    std::vector<ncz::Block> blocks(t->ncz_block_header.total_blocks);
                    R_TRY(t->Read(blocks.data(), blocks.size() * sizeof(ncz::Block), std::addressof(bytes_read)));

                    // calculate offsets for each block.
                    auto block_offset = t->read_offset.load();
                    for (const auto& block : blocks) {
                        t->ncz_blocks.emplace_back(block_offset, block.size);
                        block_offset += block.size;
                    }
                }
            }
        }

        R_TRY(t->SetDecompressBuf(buf, buffer_offset, buf_size));
    }

    log_write("read success\n");
    R_SUCCEED();
}

// decompress thread handles decrypting / modifying the nca header, decompressing ncz
// and calculating the running sha256.
Result Yati::decompressFuncInternal(ThreadData* t) {
    ON_SCOPE_EXIT( t->decompress_running = false; );

    // only used for ncz files.
    auto dctx = ZSTD_createDCtx();
    ON_SCOPE_EXIT(ZSTD_freeDCtx(dctx));
    const auto chunk_size = ZSTD_DStreamOutSize();
    const ncz::Section* ncz_section{};
    const ncz::BlockInfo* ncz_block{};
    bool is_ncz{};

    s64 inflate_offset{};
    Aes128CtrContext ctx{};
    std::vector<u8> inflate_buf{};
    inflate_buf.reserve(t->max_buffer_size);

    s64 written{};
    s64 block_offset{};
    std::vector<u8> buf{};
    buf.reserve(t->max_buffer_size);

    // encrypts the nca and passes the buffer to the write thread.
    const auto ncz_flush = [&](s64 size) -> Result {
        if (!inflate_offset) {
            R_SUCCEED();
        }

        // if we are not moving the whole vector, then we need to keep
        // the remaining data.
        // rather that copying the entire vector to the write thread,
        // only copy (store) the remaining amount.
        std::vector<u8> temp_vector{};
        if (size < inflate_offset) {
            temp_vector.resize(inflate_offset - size);
            std::memcpy(temp_vector.data(), inflate_buf.data() + size, temp_vector.size());
        }

        for (s64 off = 0; off < size;) {
            if (!ncz_section || !ncz_section->InRange(written)) {
                auto it = std::ranges::find_if(t->ncz_sections, [written](auto& e){
                    return e.InRange(written);
                });

                R_UNLESS(it != t->ncz_sections.cend(), Result_YatiNczSectionNotFound);
                ncz_section = &(*it);

                if (ncz_section->crypto_type >= nca::EncryptionType_AesCtr) {
                    const auto swp = std::byteswap(u64(written) >> 4);
                    u8 counter[0x16];
                    std::memcpy(counter + 0x0, ncz_section->counter, 0x8);
                    std::memcpy(counter + 0x8, &swp, 0x8);
                    aes128CtrContextCreate(&ctx, ncz_section->key, counter);
                }
            }

            const auto total_size = ncz_section->offset + ncz_section->size;
            const auto chunk_size = std::min<u64>(total_size - written, size - off);

            if (ncz_section->crypto_type >= nca::EncryptionType_AesCtr) {
                aes128CtrCrypt(&ctx, inflate_buf.data() + off, inflate_buf.data() + off, chunk_size);
            }

            written += chunk_size;
            off += chunk_size;
        }

        R_TRY(t->SetWriteBuf(inflate_buf, size, config.skip_nca_hash_verify));
        inflate_offset -= size;

        // restore remaining data to the swapped buffer.
        if (!temp_vector.empty()) {
            inflate_buf = temp_vector;
        }

        R_SUCCEED();
    };

    while (t->decompress_offset < t->write_size && R_SUCCEEDED(t->GetResults())) {
        s64 decompress_buf_off{};
        R_TRY(t->GetDecompressBuf(buf, decompress_buf_off));
        if (buf.empty()) {
            break;
        }

        // do we have an nsz? if so, setup buffers.
        if (!is_ncz && !t->ncz_sections.empty()) {
            log_write("YES IT FOUND NCZ\n");
            is_ncz = true;
        }

        // if we don't have a ncz or it's before the ncz header, pass buffer directly to write
        if (!is_ncz || !decompress_buf_off) {
            // check nca header
            if (!decompress_buf_off) {
                log_write("reading nca header\n");

                nca::Header header{};
                crypto::cryptoAes128Xts(buf.data(), std::addressof(header), keys.header_key, 0, 0x200, sizeof(header), false);
                log_write("verifying nca header magic\n");
                R_UNLESS(header.magic == 0x3341434E, Result_YatiInvalidNcaMagic);
                log_write("nca magic is ok! type: %u\n", header.content_type);

                // store the unmodified header.
                t->nca->header = header;

                if (!config.skip_rsa_header_fixed_key_verify) {
                    log_write("verifying nca fixed key\n");
                    R_TRY(nca::VerifyFixedKey(header));
                    log_write("nca fixed key is ok! type: %u\n", header.content_type);
                } else {
                    log_write("skipping nca verification\n");
                }

                t->write_size = header.size;
                log_write("setting placeholder size: %zu\n", t->write_size.load());
                // same stall concern as CreatePlaceHolder above: this grows the
                // file to the decompressed size and blocks the pipeline while
                // it does, so record how long it takes.
                const auto resize_start = armTicksToNs(armGetSystemTick());
                R_TRY(ncmContentStorageSetPlaceHolderSize(std::addressof(cs), std::addressof(t->nca->placeholder_id), t->write_size));
                log_write("placeholder resize took %llu ms\n", (armTicksToNs(armGetSystemTick()) - resize_start) / 1000000ULL);

                if (!config.ignore_distribution_bit && header.distribution_type == nca::DistributionType_GameCard) {
                    header.distribution_type = nca::DistributionType_System;
                    t->nca->modified = true;
                }

                // try and get the ticket, if the nca requires it.
                auto ticket = GetTicketCollection(header, t->tik);
                R_TRY(HasRequiredTicket(header, ticket));

                if ((config.convert_to_standard_crypto && ticket) || config.lower_master_key) {
                    t->nca->modified = true;
                    u8 keak_generation;

                    if (ticket) {
                        const auto key_gen = header.key_gen;
                        log_write("converting to standard crypto: 0x%X 0x%X\n", key_gen, header.key_gen);

                        // fetch ticket data block.
                        es::TicketData ticket_data;
                        R_TRY(es::GetTicketData(ticket->ticket, std::addressof(ticket_data)));

                        // validate that this indeed the correct ticket.
                        R_UNLESS(!std::memcmp(std::addressof(header.rights_id), std::addressof(ticket_data.rights_id), sizeof(header.rights_id)), Result_YatiInvalidTicketBadRightsId);

                        // decrypt title key.
                        keys::KeyEntry title_key;
                        R_TRY(es::GetTitleKey(title_key, ticket_data, keys));
                        R_TRY(es::DecryptTitleKey(title_key, key_gen, keys));

                        std::memset(header.key_area, 0, sizeof(header.key_area));
                        std::memcpy(&header.key_area[0x2], &title_key, sizeof(title_key));

                        keak_generation = key_gen;
                        ticket->required = false;
                    } else if (config.lower_master_key) {
                        R_TRY(nca::DecryptKeak(keys, header));
                    }

                    if (config.lower_master_key) {
                        keak_generation = 0;
                    }

                    R_TRY(nca::EncryptKeak(keys, header, keak_generation));
                    std::memset(&header.rights_id, 0, sizeof(header.rights_id));
                }

                if (t->nca->modified) {
                    crypto::cryptoAes128Xts(std::addressof(header), buf.data(), keys.header_key, 0, 0x200, sizeof(header), true);
                }
            }

            written += buf.size();
            t->decompress_offset += buf.size();
            R_TRY(t->SetWriteBuf(buf, buf.size(), config.skip_nca_hash_verify));
        } else if (is_ncz) {
            u64 buf_off{};
            while (buf_off < buf.size()) {
                std::span<const u8> buffer{buf.data() + buf_off, buf.size() - buf_off};
                bool compressed = true;

                // todo: blocks need to use read offset, as the offset + size is compressed range.
                if (t->ncz_blocks.size()) {
                    if (!ncz_block || !ncz_block->InRange(decompress_buf_off)) {
                        block_offset = 0;
                        auto it = std::ranges::find_if(t->ncz_blocks, [decompress_buf_off](auto& e){
                            return e.InRange(decompress_buf_off);
                        });

                        R_UNLESS(it != t->ncz_blocks.cend(), Result_YatiNczBlockNotFound);
                        ncz_block = &(*it);
                    }

                    // https://github.com/nicoboss/nsz/issues/79
                    // exponent goes up to 32, so this must not be a 32bit shift.
                    u64 decompressedBlockSize = 1ULL << t->ncz_block_header.block_size_exponent;
                    // special handling for the last block to check it's actually compressed
                    if (ncz_block->offset == t->ncz_blocks.back().offset) {
                        // https://github.com/nicoboss/nsz/issues/210
                        // a zero remainder means the last block is a full block,
                        // not an empty one (nsz PR #211).
                        const auto remainder = t->ncz_block_header.decompressed_size % decompressedBlockSize;
                        if (remainder) {
                            decompressedBlockSize = remainder;
                        }
                    }

                    // check if this block is compressed.
                    compressed = ncz_block->size < decompressedBlockSize;

                    // clip read size as blocks can be up to 32GB in size!
                    const auto size = std::min<u64>(buffer.size(), ncz_block->size - block_offset);
                    buffer = buffer.subspan(0, size);
                }

                if (compressed) {
                    ZSTD_inBuffer input = { buffer.data(), buffer.size(), 0 };
                    while (input.pos < input.size) {
                        R_TRY(t->GetResults());

                        inflate_buf.resize(inflate_offset + chunk_size);
                        ZSTD_outBuffer output = { inflate_buf.data() + inflate_offset, chunk_size, 0 };
                        const auto res = ZSTD_decompressStream(dctx, std::addressof(output), std::addressof(input));
                        if (ZSTD_isError(res)) {
                            log_write("[NCZ] ZSTD_decompressStream() pos: %zu size: %zu res: %zd msg: %s\n", input.pos, input.size, res, ZSTD_getErrorName(res));
                        }
                        R_UNLESS(!ZSTD_isError(res), Result_YatiInvalidNczZstdError);

                        t->decompress_offset += output.pos;
                        inflate_offset += output.pos;
                        if (inflate_offset >= INFLATE_BUFFER_MAX) {
                            R_TRY(ncz_flush(INFLATE_BUFFER_MAX));
                        }
                    }
                } else {
                    inflate_buf.resize(inflate_offset + buffer.size());
                    std::memcpy(inflate_buf.data() + inflate_offset, buffer.data(), buffer.size());

                    t->decompress_offset += buffer.size();
                    inflate_offset += buffer.size();
                    if (inflate_offset >= INFLATE_BUFFER_MAX) {
                        R_TRY(ncz_flush(INFLATE_BUFFER_MAX));
                    }
                }

                buf_off += buffer.size();
                decompress_buf_off += buffer.size();
                block_offset += buffer.size();
            }
        }
    }

    // flush remaining data.
    if (is_ncz && inflate_offset) {
        log_write("flushing remaining\n");
        R_TRY(ncz_flush(inflate_offset));
    }

    log_write("decompress thread done!\n");

    // get final hash output.
    sha256ContextGetHash(std::addressof(t->sha256), t->nca->hash);

    R_SUCCEED();
}

// write thread writes data to the nca placeholder.
Result Yati::writeFuncInternal(ThreadData* t) {
    ON_SCOPE_EXIT( t->write_running = false; );

    std::vector<u8> buf;
    buf.reserve(t->max_buffer_size);
    const auto is_file_based_emummc = App::IsFileBaseEmummc();

    while (t->write_offset < t->write_size && R_SUCCEEDED(t->GetResults())) {
        s64 dummy_off;
        R_TRY(t->GetWriteBuf(buf, dummy_off));
        if (buf.empty()) {
            break;
        }

        s64 off{};
        while (off < buf.size() && t->write_offset < t->write_size && R_SUCCEEDED(t->GetResults())) {
            const auto wsize = std::min<s64>(t->read_buffer_size, buf.size() - off);
            R_TRY(ncmContentStorageWritePlaceHolder(std::addressof(cs), std::addressof(t->nca->placeholder_id), t->write_offset, buf.data() + off, wsize));

            off += wsize;
            t->write_offset += wsize;
            ueventSignal(t->GetProgressEvent());

            // todo: check how much time elapsed and sleep the diff
            // rather than always sleeping a fixed amount.
            // ie, writing a small buffer (nca header) should not sleep the full 2 ms.
            if (is_file_based_emummc) {
                svcSleepThread(2e+6); // 2ms
            }
        }
    }

    log_write("finished write thread!\n");
    R_SUCCEED();
}

void readFunc(void* d) {
    auto t = static_cast<ThreadData*>(d);
    t->SetReadResult(t->yati->readFuncInternal(t));
    log_write("read thread returned now\n");
}

void decompressFunc(void* d) {
    log_write("hello decomp thread func\n");
    auto t = static_cast<ThreadData*>(d);
    t->SetDecompressResult(t->yati->decompressFuncInternal(t));
    log_write("decompress thread returned now\n");
}

void writeFunc(void* d) {
    auto t = static_cast<ThreadData*>(d);
    t->SetWriteResult(t->yati->writeFuncInternal(t));
    log_write("write thread returned now\n");
}

} // namespace sphaira::yati::detail
