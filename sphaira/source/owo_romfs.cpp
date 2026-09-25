#include "owo_internal.hpp"
#include "yati/nx/crypto.hpp"
#include <switch.h>
#include <cstring>
#include <vector>
#include <span>

namespace sphaira {

auto write_padding(BufHelper& buf, u64 off, u64 block) -> u64 {
    const u64 size = block - (off % block);
    if (size) {
        std::vector<u8> padding(size);
        buf.write(padding.data(), padding.size());
    }
    return size;
}

namespace {

typedef struct romfs_dirent_ctx {
    u32 entry_offset;
    struct romfs_dirent_ctx *parent; /* Parent node */
    struct romfs_dirent_ctx *child; /* Child node */
    struct romfs_dirent_ctx *sibling; /* Sibling node */
    struct romfs_fent_ctx *file; /* File node */
    struct romfs_dirent_ctx *next; /* Next node */
} romfs_dirent_ctx_t;

typedef struct romfs_fent_ctx {
    u32 entry_offset;
    u64 offset;
    u64 size;
    romfs_dirent_ctx_t *parent; /* Parent dir */
    struct romfs_fent_ctx *sibling; /* Sibling file */
    struct romfs_fent_ctx *next; /* Logical next file */
} romfs_fent_ctx_t;

typedef struct {
    romfs_fent_ctx_t *files;
    u64 num_dirs;
    u64 num_files;
    u64 dir_table_size;
    u64 file_table_size;
    u64 dir_hash_table_size;
    u64 file_hash_table_size;
    u64 file_partition_size;
} romfs_ctx_t;

auto romfs_get_direntry(romfs_dir *directories, u32 offset) -> romfs_dir* {
    return (romfs_dir*)((u8*)directories + offset);
}

auto romfs_get_fentry(romfs_file *files, u32 offset) -> romfs_file* {
    return (romfs_file*)((u8*)files + offset);
}

auto calc_path_hash(u32 parent, const u8 *path, u32 start, u32 path_len) -> u32 {
    u32 hash = parent ^ 123456789;
    for (u32 i = 0; i < path_len; i++) {
        hash = (hash >> 5) | (hash << 27);
        hash ^= path[start + i];
    }

    return hash;
}

auto align(u32 offset, u32 alignment) -> u32 {
    const u32 mask = ~(alignment - 1);
    return (offset + (alignment - 1)) & mask;
}

auto align64(u64 offset, u64 alignment) -> u64 {
    const u64 mask = ~(u64)(alignment - 1);
    return (offset + (alignment - 1)) & mask;
}

auto romfs_get_hash_table_count(u32 num_entries) -> u32 {
    if (num_entries < 3) {
        return 3;
    } else if (num_entries < 19) {
        return num_entries | 1;
    }

    u32 count = num_entries;
    while (count % 2 == 0 || count % 3 == 0 || count % 5 == 0 || count % 7 == 0 || count % 11 == 0 || count % 13 == 0 || count % 17 == 0) {
        count++;
    }

    return count;
}

void romfs_visit_dir(const FileEntries& entries, romfs_dirent_ctx_t *parent, romfs_ctx_t *romfs_ctx) {
    romfs_dirent_ctx_t *child_dir_tree = NULL;
    romfs_fent_ctx_t *child_file_tree = NULL;
    romfs_fent_ctx_t *cur_file = NULL;

    for (auto& e : entries) {
        /* File */
        cur_file = (romfs_fent_ctx_t*)calloc(1, sizeof(romfs_fent_ctx_t));

        romfs_ctx->num_files++;

        cur_file->parent = parent;
        cur_file->size = e.data.size();

        romfs_ctx->file_table_size += sizeof(romfs_file) + align(e.name.length() - 1, 4);

        /* Ordered insertion on sibling */
        if (child_file_tree == NULL) {
            cur_file->sibling = child_file_tree;
            child_file_tree = cur_file;
        } else {
            romfs_fent_ctx_t *child, *prev;
            prev = child_file_tree;
            child = child_file_tree->sibling;
            prev->sibling = cur_file;
            cur_file->sibling = child;
        }

        /* Ordered insertion on next */
        if (romfs_ctx->files == NULL) {
            cur_file->next = romfs_ctx->files;
            romfs_ctx->files = cur_file;
        } else {
            romfs_fent_ctx_t *child, *prev;
            prev = romfs_ctx->files;
            child = romfs_ctx->files->next;
            prev->next = cur_file;
            cur_file->next = child;
        }

        cur_file = NULL;
    }

    parent->child = child_dir_tree;
    parent->file = child_file_tree;
}

} // namespace

void build_romfs_into_file(const FileEntries& entries, BufHelper& buf) {
    auto root_ctx = (romfs_dirent_ctx_t*)calloc(1, sizeof(romfs_dirent_ctx_t));
    root_ctx->parent = root_ctx;

    romfs_ctx_t romfs_ctx{};

    romfs_ctx.dir_table_size = sizeof(romfs_dir); /* Root directory. */
    romfs_ctx.num_dirs = 1;

    /* Visit all directories. */
    romfs_visit_dir(entries, root_ctx, &romfs_ctx);
    const u32 dir_hash_table_entry_count = romfs_get_hash_table_count(romfs_ctx.num_dirs);
    const u32 file_hash_table_entry_count = romfs_get_hash_table_count(romfs_ctx.num_files);
    romfs_ctx.dir_hash_table_size = 4 * dir_hash_table_entry_count;
    romfs_ctx.file_hash_table_size = 4 * file_hash_table_entry_count;

    romfs_header header{};
    romfs_fent_ctx_t *cur_file{};
    romfs_dirent_ctx_t *cur_dir{};
    u32 entry_offset{};

    std::vector<u32> dir_hash_table(dir_hash_table_entry_count, ROMFS_ENTRY_EMPTY);
    std::vector<u32> file_hash_table(file_hash_table_entry_count, ROMFS_ENTRY_EMPTY);

    auto dir_table = (romfs_dir*)calloc(1, romfs_ctx.dir_table_size);
    auto file_table = (romfs_file *)calloc(1, romfs_ctx.file_table_size);

    /* Determine file offsets. */
    cur_file = romfs_ctx.files;
    entry_offset = 0;
    for (auto& e : entries) {
        romfs_ctx.file_partition_size = align64(romfs_ctx.file_partition_size, 0x10);
        cur_file->offset = romfs_ctx.file_partition_size;
        romfs_ctx.file_partition_size += cur_file->size;
        cur_file->entry_offset = entry_offset;
        entry_offset += sizeof(romfs_file) + align(e.name.length() - 1, 4);
        cur_file = cur_file->next;
    }

    /* Determine dir offsets. */
    root_ctx->entry_offset = 0x0;

    /* Populate file tables. */
    cur_file = romfs_ctx.files;
    for (auto& e : entries) {
        auto cur_entry = romfs_get_fentry(file_table, cur_file->entry_offset);
        cur_entry->parent = (cur_file->parent->entry_offset);
        cur_entry->sibling = (cur_file->sibling == NULL ? ROMFS_ENTRY_EMPTY : cur_file->sibling->entry_offset);
        cur_entry->dataOff = (cur_file->offset);
        cur_entry->dataSize = (cur_file->size);

        const u32 name_size = e.name.length() - 1;
        const u32 hash = calc_path_hash(cur_file->parent->entry_offset, (const u8 *)e.name.c_str(), 1, name_size);
        cur_entry->nextHash = file_hash_table[hash % file_hash_table_entry_count];
        file_hash_table[hash % file_hash_table_entry_count] = (cur_file->entry_offset);

        cur_entry->nameLen = name_size;
        std::memcpy(cur_entry->name, e.name.c_str() + 1, name_size);

        cur_file = cur_file->next;
    }

    /* Populate dir tables. */
    cur_dir = root_ctx;

    while (cur_dir != NULL) {
        auto cur_entry = romfs_get_direntry(dir_table, cur_dir->entry_offset);
        cur_entry->parent = cur_dir->parent->entry_offset;
        cur_entry->sibling = cur_dir->sibling == NULL ? ROMFS_ENTRY_EMPTY : cur_dir->sibling->entry_offset;
        cur_entry->childDir = cur_dir->child == NULL ? ROMFS_ENTRY_EMPTY : cur_dir->child->entry_offset;
        cur_entry->childFile = cur_dir->file == NULL ? ROMFS_ENTRY_EMPTY : cur_dir->file->entry_offset;

        const auto hash = calc_path_hash(0, 0, 0, 0);
        cur_entry->nextHash = dir_hash_table[hash % dir_hash_table_entry_count];
        dir_hash_table[hash % dir_hash_table_entry_count] = (cur_dir->entry_offset);

        cur_entry->nameLen = 0;

        auto temp = cur_dir;
        cur_dir = cur_dir->next;
        free(temp);
    }

    header.headerSize = sizeof(header);
    header.fileHashTableSize = romfs_ctx.file_hash_table_size;
    header.fileTableSize = romfs_ctx.file_table_size;
    header.dirHashTableSize = romfs_ctx.dir_hash_table_size;
    header.dirTableSize = romfs_ctx.dir_table_size;
    header.fileDataOff = ROMFS_FILEPARTITION_OFS;

    header.dirHashTableOff = align64(romfs_ctx.file_partition_size + ROMFS_FILEPARTITION_OFS, 4);
    header.dirTableOff = header.dirHashTableOff + romfs_ctx.dir_hash_table_size;
    header.fileHashTableOff = header.dirTableOff + romfs_ctx.dir_table_size;
    header.fileTableOff = header.fileHashTableOff + romfs_ctx.file_hash_table_size;

    buf.write(&header, sizeof(header));

    /* Write files. */
    cur_file = romfs_ctx.files;
    for (auto&e : entries) {
        buf.seek(cur_file->offset + ROMFS_FILEPARTITION_OFS);
        buf.write(e.data.data(), e.data.size());

        auto temp = cur_file;
        cur_file = cur_file->next;
        free(temp);
    }

    buf.seek(header.dirHashTableOff);
    buf.write(dir_hash_table.data(), romfs_ctx.dir_hash_table_size);

    buf.write(dir_table, romfs_ctx.dir_table_size);
    free(dir_table);

    buf.write(file_hash_table.data(), romfs_ctx.file_hash_table_size);

    buf.write(file_table, romfs_ctx.file_table_size);
    free(file_table);
}

auto romfs_build(const FileEntries& entries, u64 *out_size) -> std::vector<u8> {
    BufHelper buf;

    build_romfs_into_file(entries, buf);

    // Write Padding
    buf.seek(buf.buf.size());
    *out_size = buf.tell();
    write_padding(buf, buf.tell(), IVFC_HASH_BLOCK_SIZE);

    return buf.buf;
}

} // namespace sphaira
