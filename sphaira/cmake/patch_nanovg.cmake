# Patches the fetched nanovg-deko3d library. Run as a FetchContent PATCH_COMMAND
# and again at configure time (working directory = nanovg source root).
#
# DkRenderer records every nanovg draw call of a frame into one command buffer
# of DynamicCmdSize bytes (128 KiB, ~350 draw calls). A busy frame (a full grid
# of selected save backups: tick, border, badges and owner strip per tile) runs
# out, and deko3d's default handler aborts the app in requestCmdMem.
# The patch gives the command buffer an add-memory callback: when a frame needs
# more, it takes another chunk from the data pool. The chunks live until the
# next frame's fence wait, then go back to the pool. No fixed upper limit.

set(hpp "include/nanovg/dk_renderer.hpp")
set(cpp "source/dk_renderer.cpp")
if(NOT EXISTS "${hpp}" OR NOT EXISTS "${cpp}")
    message(STATUS "[nanovg-patch] sources not found (cwd unexpected), skipping")
    return()
endif()

file(READ "${hpp}" hpp_src)
if(hpp_src MATCHES "AddDynCmdMem")
    return()
endif()
file(READ "${cpp}" cpp_src)

# replace(<var> <old> <new>): exact one-shot replace, fatal when <old> is missing
macro(nanovg_replace var old new)
    string(FIND "${${var}}" "${old}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "[nanovg-patch] anchor not found: ${old}")
    endif()
    string(REPLACE "${old}" "${new}" ${var} "${${var}}")
endmacro()

nanovg_replace(hpp_src
    "            CCmdMemRing<1> m_dyn_cmd_mem;"
    "            CCmdMemRing<1> m_dyn_cmd_mem;
            std::vector<CMemPool::Handle> m_dyn_cmd_extra;
            static void AddDynCmdMem(void* user, DkCmdBuf cmdbuf, size_t min_size);")

nanovg_replace(cpp_src
    "        m_dyn_cmd_buf = dk::CmdBufMaker{m_device}.create();"
    "        m_dyn_cmd_buf = dk::CmdBufMaker{m_device}.setUserData(this).setCbAddMem(AddDynCmdMem).create();")

nanovg_replace(cpp_src
    "        m_frag_uniform_buffer.destroy();
        m_textures.clear();
    }"
    "        m_frag_uniform_buffer.destroy();
        m_textures.clear();
        for (auto &mem : m_dyn_cmd_extra) {
            mem.destroy();
        }
    }

    void DkRenderer::AddDynCmdMem(void* user, DkCmdBuf cmdbuf, size_t min_size) {
        auto *self = static_cast<DkRenderer*>(user);
        const size_t size = min_size > DynamicCmdSize ? min_size : DynamicCmdSize;
        CMemPool::Handle mem = self->m_data_mem_pool.allocate(size, DK_CMDMEM_ALIGNMENT);
        if (!mem) {
            return; /* deko3d reports the failure itself */
        }
        dkCmdBufAddMemory(cmdbuf, mem.getMemBlock(), mem.getOffset(), mem.getSize());
        self->m_dyn_cmd_extra.push_back(mem);
    }")

# begin() waits for the previous frame's fence, so its extra chunks are free now
nanovg_replace(cpp_src
    "            m_dyn_cmd_mem.begin(m_dyn_cmd_buf);"
    "            m_dyn_cmd_mem.begin(m_dyn_cmd_buf);
            for (auto &mem : m_dyn_cmd_extra) {
                mem.destroy();
            }
            m_dyn_cmd_extra.clear();")

file(WRITE "${hpp}" "${hpp_src}")
file(WRITE "${cpp}" "${cpp_src}")
message(STATUS "[nanovg-patch] dynamic command buffer grows on demand")
