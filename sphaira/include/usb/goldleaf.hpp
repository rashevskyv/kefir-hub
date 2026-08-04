#pragma once

// Goldleaf / Quark usb protocol, as spoken by ns-usbloader in its
// "GoldLeaf v0.10+" mode.
//
// It is the mirror image of Awoo/Tinfoil: there the pc pushes a file list at
// the console and then answers range requests, here the console is the client
// driving a remote filesystem and the pc only ever answers. Nothing arrives
// until we ask for it, which is what makes the two protocols tellable apart --
// see Usb::WaitForConnection().
//
// Every request and every reply is one fixed BLOCK_SIZE transfer, zero padded.
// The payload is a flat run of little endian scalars and length prefixed utf-8
// strings. The only thing that does not travel inside a block is the file data
// answering a ReadFile, which follows its reply block as its own transfer.
//
// Nothing here includes switch.h, so the block codec is testable on the host
// -- see tests/test_goldleaf_block.cpp.

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace sphaira::usb::goldleaf {

enum Magic : std::uint32_t {
    Magic_In = 0x49434C47,  // GLCI, console -> pc.
    Magic_Out = 0x4F434C47, // GLCO, pc -> console.
};

enum class CmdId : std::uint32_t {
    GetDriveCount = 1,
    GetDriveInfo = 2,
    StatPath = 3,
    GetFileCount = 4,
    GetFile = 5,
    GetDirectoryCount = 6,
    GetDirectory = 7,
    StartFile = 8,
    ReadFile = 9,
    WriteFile = 10,
    EndFile = 11,
    Create = 12,
    Delete = 13,
    Rename = 14,
    GetSpecialPathCount = 15,
    GetSpecialPath = 16,
    SelectFile = 17,
};

enum PathType : std::uint32_t {
    PathType_File = 1,
    PathType_Directory = 2,
};

constexpr std::uint32_t BLOCK_SIZE = 0x1000;

// the drive ns-usbloader serves the files queued in its window under.
constexpr std::string_view VIRTUAL_DRIVE = "VIRT:/";

// appends fields to a request block. The first append that would not fit
// poisons the writer, so callers check Ok() once before sending rather than
// after every field.
struct BlockWriter {
    BlockWriter() = default;
    explicit BlockWriter(std::uint8_t* data) : m_data{data} {}

    void Raw(const void* src, std::uint32_t size) {
        // compared as a subtraction from the far end so an absurd size cannot
        // wrap the addition and land back inside the block.
        if (!m_data || !m_ok || size > BLOCK_SIZE - m_pos) {
            m_ok = false;
            return;
        }
        std::memcpy(m_data + m_pos, src, size);
        m_pos += size;
    }

    void U32(std::uint32_t v) { Raw(&v, sizeof(v)); }
    void U64(std::uint64_t v) { Raw(&v, sizeof(v)); }

    void Str(std::string_view s) {
        if (s.size() > BLOCK_SIZE) {
            m_ok = false;
            return;
        }
        U32(static_cast<std::uint32_t>(s.size()));
        Raw(s.data(), static_cast<std::uint32_t>(s.size()));
    }

    bool Ok() const { return m_ok; }
    std::uint32_t Pos() const { return m_pos; }

private:
    std::uint8_t* m_data{};
    std::uint32_t m_pos{};
    bool m_ok{true};
};

// reads fields back out of a reply block. Everything in here came off the
// wire, so each read is bounds checked against the block instead of trusted;
// a false return means the reply was malformed and the caller must stop.
struct BlockReader {
    BlockReader() = default;
    explicit BlockReader(const std::uint8_t* data) : m_data{data} {}

    [[nodiscard]] bool Raw(void* dst, std::uint32_t size) {
        if (!m_data || size > BLOCK_SIZE - m_pos) {
            return false;
        }
        std::memcpy(dst, m_data + m_pos, size);
        m_pos += size;
        return true;
    }

    [[nodiscard]] bool U32(std::uint32_t* out) { return Raw(out, sizeof(*out)); }
    [[nodiscard]] bool U64(std::uint64_t* out) { return Raw(out, sizeof(*out)); }

    [[nodiscard]] bool Str(std::string* out) {
        std::uint32_t len;
        if (!U32(&len) || len > BLOCK_SIZE - m_pos) {
            return false;
        }
        out->assign(reinterpret_cast<const char*>(m_data + m_pos), len);
        m_pos += len;
        return true;
    }

    std::uint32_t Pos() const { return m_pos; }

private:
    const std::uint8_t* m_data{};
    std::uint32_t m_pos{};
};

} // namespace sphaira::usb::goldleaf
