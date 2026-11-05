#include "rar_reader.hpp"

#include <dll.hpp> // UnRAR DLL API; needs _UNIX (set by the unrar target)

namespace sphaira::rar {
namespace {

int CALLBACK OnUnrar(UINT msg, LPARAM user, LPARAM p1, LPARAM p2) {
    auto r = reinterpret_cast<Reader*>(user);
    switch (msg) {
        case UCM_PROCESSDATA:
            if (r->sink && *r->sink && !(*r->sink)(reinterpret_cast<const void*>(p1), static_cast<size_t>(p2))) {
                r->sink_stopped = true;
                return -1;
            }
            return 1;
        case UCM_NEEDPASSWORD:
        case UCM_NEEDPASSWORDW:
            r->password_asked = true;
            return -1; // no password support: stop with a clear error
        case UCM_CHANGEVOLUME:
        case UCM_CHANGEVOLUMEW:
            // NOTIFY = the next volume was found and opened; ASK = it is missing.
            return p2 == RAR_VOL_NOTIFY ? 1 : -1;
        case UCM_LARGEDICT:
            return 0; // a dictionary over 4 GB does not fit the console: refuse
    }
    return 0;
}

} // namespace

Error MapCode(int code, bool password_asked, bool stopped) {
    if (code == ERAR_SUCCESS) {
        return Error::None;
    }
    if (stopped) {
        return Error::Stopped;
    }
    if (password_asked) {
        return Error::Password;
    }
    switch (code) {
        case ERAR_MISSING_PASSWORD:
        case ERAR_BAD_PASSWORD:
            return Error::Password;
        case ERAR_NO_MEMORY:
        case ERAR_LARGE_DICT:
            return Error::NoMemory;
        case ERAR_EOPEN:
        case ERAR_EREAD:
            return Error::SourceRead;
        case ERAR_UNKNOWN_FORMAT:
            return Error::Unsupported;
        default:
            return Error::Damaged;
    }
}

Reader::Reader() = default;

Reader::~Reader() {
    if (handle) {
        RARCloseArchive(handle);
    }
}

Error Reader::Open(const std::string& path, bool extract) {
    header = std::make_unique<RARHeaderDataEx>(); // ~14 KB, zeroed as UnRAR requires
    RAROpenArchiveDataEx d{};
    d.ArcName = const_cast<char*>(path.c_str());
    d.OpenMode = extract ? RAR_OM_EXTRACT : RAR_OM_LIST;
    d.Callback = OnUnrar;
    d.UserData = reinterpret_cast<LPARAM>(this);
    handle = RAROpenArchiveEx(&d);
    if (!handle) {
        if (d.OpenResult == ERAR_BAD_ARCHIVE) {
            return Error::Unsupported; // at open this means "no RAR signature"
        }
        return MapCode(d.OpenResult ? d.OpenResult : ERAR_UNKNOWN, password_asked, false);
    }
    solid = d.Flags & ROADF_SOLID;
    return Error::None;
}

Error Reader::Next(Entry* out, bool* end) {
    *end = false;
    const auto code = RARReadHeaderEx(handle, header.get());
    if (code == ERAR_END_ARCHIVE) {
        *end = true;
        return Error::None;
    }
    if (code != ERAR_SUCCESS) {
        return MapCode(code, password_asked, false);
    }
    const auto& h = *header;
    out->name = WideToUtf8(h.FileNameW);
    out->size = static_cast<std::int64_t>(h.UnpSize | static_cast<std::uint64_t>(h.UnpSizeHigh) << 32);
    out->mtime = FileTimeToUnix(h.MtimeLow | static_cast<std::uint64_t>(h.MtimeHigh) << 32);
    out->is_dir = h.Flags & RHDF_DIRECTORY;
    out->is_file = !out->is_dir && h.RedirType == 0;
    return Error::None;
}

Error Reader::Process(int operation, const Sink& s) {
    sink = &s;
    sink_stopped = false;
    const auto code = RARProcessFile(handle, operation, nullptr, nullptr);
    sink = nullptr;
    return MapCode(code, password_asked, sink_stopped);
}

Error Reader::Skip(const Sink& s) {
    if (!solid) {
        static const Sink none{};
        return Process(RAR_SKIP, none); // seeks over the packed data
    }
    // solid: the entry must be decompressed anyway; RAR_TEST lets `s` stop it.
    return Process(RAR_TEST, s);
}

Error Reader::Read(const Sink& s) {
    return Process(RAR_TEST, s);
}

namespace {
constexpr size_t PIPE_SIZE = 2 * 1024 * 1024;
// UnRAR keeps its window and tables on the heap; this covers its call depth with room.
constexpr size_t WORKER_STACK = 256 * 1024;
} // namespace

EntryStream::EntryStream(std::string path, Match match)
    : m_path{std::move(path)}, m_match{std::move(match)}, m_pipe{std::make_unique<BytePipe>(PIPE_SIZE)} {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, WORKER_STACK);
    m_started = pthread_create(&m_thread, &attr, Run, this) == 0;
    pthread_attr_destroy(&attr);
    if (!m_started) {
        m_error = Error::NoMemory;
        m_pipe->Finish();
    }
}

EntryStream::~EntryStream() {
    m_pipe->Close();
    if (m_started) {
        pthread_join(m_thread, nullptr);
    }
}

size_t EntryStream::Read(void* out, size_t size) {
    auto dst = static_cast<std::uint8_t*>(out);
    size_t done = 0;
    while (done < size) {
        const auto got = m_pipe->Read(dst + done, size - done);
        if (!got) {
            break;
        }
        done += got;
    }
    return done;
}

void* EntryStream::Run(void* p) {
    auto self = static_cast<EntryStream*>(p);
    auto& pipe = *self->m_pipe;
    const auto finish = [&](Error e) {
        self->m_error = e;
        pipe.Finish();
        return nullptr;
    };

    Reader r;
    if (const auto e = r.Open(self->m_path, true); e != Error::None) {
        return finish(e);
    }
    const Reader::Sink keep_going = [&](const void*, size_t) { return !pipe.Closed(); };
    const Reader::Sink to_pipe = [&](const void* data, size_t size) { return pipe.Write(data, size); };

    Entry entry;
    while (true) {
        bool end{};
        if (const auto e = r.Next(&entry, &end); e != Error::None) {
            return finish(e);
        }
        if (end) {
            return finish(Error::NotFound);
        }
        if (entry.is_file && self->m_match(entry)) {
            return finish(r.Read(to_pipe));
        }
        if (const auto e = r.Skip(keep_going); e != Error::None) {
            return finish(e);
        }
    }
}

} // namespace sphaira::rar
