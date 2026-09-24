#include "yati/yati.hpp"
#include "path_util.hpp"
#include "yati/container/nsp.hpp"
#include "yati/container/xci.hpp"
#include "yati/nx/ncz.hpp"
#include "ui/menus/install_plan.hpp"
#include "app.hpp"
#include "log.hpp"
#include "defines.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace sphaira::yati {
namespace {

// exact decompressed size of an ncz entry: the section table at 0x4000
// stores the decompressed offset+size of every region of the original nca.
// Returns -1 when the entry is not a valid ncz (or the reads fail), in which
// case the caller falls back to the x1.6 estimate.
s64 GetNczDecompressedSize(source::Base* source, s64 offset, s64 size) {
    if (size < static_cast<s64>(NCZ_SECTION_OFFSET)) {
        return -1;
    }

    ncz::Header header{};
    u64 bytes_read{};
    if (R_FAILED(source->Read(std::addressof(header), offset + NCZ_NORMAL_SIZE, sizeof(header), std::addressof(bytes_read))) || bytes_read != sizeof(header)) {
        return -1;
    }
    if (header.magic != NCZ_SECTION_MAGIC || !header.total_sections || header.total_sections > 0x100) {
        return -1;
    }

    std::vector<ncz::Section> sections(header.total_sections);
    const auto sections_size = sections.size() * sizeof(ncz::Section);
    if (R_FAILED(source->Read(sections.data(), offset + NCZ_SECTION_OFFSET, sections_size, std::addressof(bytes_read))) || bytes_read != sections_size) {
        return -1;
    }

    s64 total = NCZ_NORMAL_SIZE;
    for (const auto& section : sections) {
        const auto section_offset = static_cast<s64>(section.offset);
        const auto section_size = static_cast<s64>(section.size);
        if (section_offset < 0 || section_size < 0 || section_offset > INT64_MAX - section_size) {
            return -1;
        }
        total = std::max(total, section_offset + section_size);
    }
    return total;
}

} // namespace

Result AnalyzeSource(source::Base* source, const fs::FsPath& path, InstallAnalysis& out) {
    out = {};
    R_TRY(source->GetOpenResult());

    const auto ext = std::strrchr(path.s, '.');
    R_UNLESS(ext, Result_YatiContainerNotFound);

    std::unique_ptr<container::Base> container;
    if (!strcasecmp(ext, ".nsp") || !strcasecmp(ext, ".nsz")) {
        container = std::make_unique<container::Nsp>(source);
    } else if (!strcasecmp(ext, ".xci") || !strcasecmp(ext, ".xcz")) {
        container = std::make_unique<container::Xci>(source);
    }
    R_UNLESS(container, Result_YatiContainerNotFound);
    R_TRY(container->GetCollections(out.collections));
    R_UNLESS(!out.collections.empty(), Result_YatiContainerNotFound);

    // ncz entries whose section table could not be read: estimated below.
    s64 estimated_compressed{};
    for (const auto& entry : out.collections) {
        R_UNLESS(entry.offset >= 0 && entry.size >= 0, Result_YatiContainerNotFound);
        R_UNLESS(entry.offset <= INT64_MAX - entry.size, Result_YatiContainerNotFound);
        out.source_size = std::max(out.source_size, entry.offset + entry.size);

        if (path::EndsWithIC(entry.name, ".nca")) {
            R_UNLESS(out.install_size <= INT64_MAX - entry.size, Result_YatiContainerNotFound);
            out.install_size += entry.size;
        } else if (path::EndsWithIC(entry.name, ".ncz")) {
            out.compressed = true;
            const auto decompressed = GetNczDecompressedSize(source, entry.offset, entry.size);
            if (decompressed >= 0) {
                // exact decompressed size from the ncz section table.
                R_UNLESS(out.install_size <= INT64_MAX - decompressed, Result_YatiContainerNotFound);
                out.install_size += decompressed;
            } else {
                R_UNLESS(estimated_compressed <= INT64_MAX - entry.size, Result_YatiContainerNotFound);
                estimated_compressed += entry.size;
            }
        }
    }

    if (estimated_compressed > 0) {
        // Existing Yati policy is x1.6. Use checked integer arithmetic so a
        // malformed container cannot overflow into a small/negative plan.
        constexpr s64 FACTOR_NUMERATOR = 8;
        constexpr s64 FACTOR_DENOMINATOR = 5;
        const auto quotient = estimated_compressed / FACTOR_DENOMINATOR;
        const auto remainder = estimated_compressed % FACTOR_DENOMINATOR;
        const auto rounded_remainder = (remainder * FACTOR_NUMERATOR + FACTOR_DENOMINATOR - 1) / FACTOR_DENOMINATOR;
        R_UNLESS(quotient <= (INT64_MAX - rounded_remainder) / FACTOR_NUMERATOR, Result_YatiContainerNotFound);
        const auto estimate = quotient * FACTOR_NUMERATOR + rounded_remainder;
        R_UNLESS(out.install_size <= INT64_MAX - estimate, Result_YatiContainerNotFound);
        out.install_size += estimate;
        out.size_kind = AnalysisSizeKind::Estimate;
        out.size_reason = "Compressed content size is estimated (x1.6)";
    }
    out.suggested_sd = ChooseInstallTarget(out.install_size, false);
    R_SUCCEED();
}

bool ChooseInstallTarget(s64 total_size, bool is_compressed) {
    s64 free_nand = 0;
    s64 free_sd = 0;
    fs::GetStorageSpaces(&free_nand, nullptr, &free_sd, nullptr);

    constexpr double COMPRESSED_SIZE_FACTOR = 1.6;
    s64 estimated = total_size;
    if (is_compressed) {
        estimated = static_cast<s64>(total_size * COMPRESSED_SIZE_FACTOR);
    }

    const s64 reserve_nand = App::GetInstallReserveMb() * 1024LL * 1024LL;
    const s64 reserve_sd = App::GetInstallReserveSdMb() * 1024LL * 1024LL;
    const s64 usable_nand = std::max<s64>(0, free_nand - reserve_nand);
    const s64 usable_sd = std::max<s64>(0, free_sd - reserve_sd);

    const long loc = App::GetInstallLocation();
    const bool pick_sd = ui::menu::dbi::PlanPickSd(loc, estimated, usable_sd, usable_nand);
    if (pick_sd) {
        if (usable_sd < estimated) {
            log_write("[Install] WARNING: Target SD space is below reserve!\n");
        }
    } else if (usable_nand < estimated) {
        log_write("[Install] WARNING: Target NAND space is below reserve!\n");
    }
    return pick_sd;
}

} // namespace sphaira::yati
