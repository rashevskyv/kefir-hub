// Web upload connections option (web_upload_conns): 0 = Off, 2,3,4,6 = fixed count, 7 = Auto (page picks, up to 6).
// Libnx-free so it has a host test (tests/test_web_upload_conns.cpp).
#pragma once

namespace sphaira::web {

inline int UploadConnsFixed(long opt) {
    if (opt < 2) return 0;
    if (opt == 5) return 4;
    if (opt >= 7) return 6;
    return (int)opt;
}

inline bool UploadConnsAuto(long opt) {
    return opt == 7;
}

// Number advertised to the page as "conns". An install holds one worker itself, so it gets at most 5.
inline int UploadConnsMax(long opt, bool install) {
    const int n = UploadConnsFixed(opt);
    const int cap = install ? 5 : 6;
    return n > cap ? cap : n;
}

} // namespace sphaira::web
