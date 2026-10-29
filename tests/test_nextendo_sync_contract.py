#!/usr/bin/env python3
"""tools/nextendo/sync.py: the checks that stand in for a human review stop a surprising Prelude release."""
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "nextendo"))
import sync  # noqa: E402

APPLY_C = r'''
char *nextendo_hosts_build(const char *ip) {
    char line[256];
    EMIT_H("# ====\n");
    EMIT_H("#  NEXTENDO NETWORK - Atmosphere DNS-MITM (mode NEXTENDO)\n");
    snprintf(line, sizeof(line), "%s *.nintendo.com\n", ip); EMIT_H(line);
    snprintf(line, sizeof(line), "%s nncs2-lp1.n.n.srv.nintendo.net\n", NEXTENDO_SERVER_IP_NNCSD2); EMIT_H(line);
    snprintf(line, sizeof(line), "%s receive-%%.dg.srv.nintendo.net\n", ip); EMIT_H(line);
#undef EMIT_H
'''


def refused(fn, *args):
    try:
        fn(*args)
    except sync.Refused:
        return True
    return False


def tree(files):
    root = Path(tempfile.mkdtemp())
    for rel, data in files.items():
        p = root / "romfs" / "sd" / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    return root


def main():
    tmpl = sync.build_hosts(APPLY_C)
    assert "{IP} *.nintendo.com" in tmpl and "{IP_NNCS2} nncs2-lp1" in tmpl and "receive-%.dg" in tmpl
    # a literal IP, a 127.0.0.1 line or no mark: stop
    assert refused(sync.build_hosts, APPLY_C.replace('"%s *.nintendo.com\\n", ip', '"1.2.3.4 evil.com\\n", ip'))
    assert refused(sync.build_hosts, APPLY_C.replace('EMIT_H("# ====\\n");', 'EMIT_H("127.0.0.1 x.com\\n");'))
    assert refused(sync.build_hosts, APPLY_C.replace("NEXTENDO NETWORK", "SOMETHING ELSE"))
    assert refused(sync.build_hosts, "int main() {}")

    cfg = '#define NEXTENDO_SERVER_IP_DEFAULT "51.178.29.194"\n#define NEXTENDO_SERVER_IP_NNCSD2 "164.132.111.120"\n'
    assert sync.build_config(cfg)["ip_default"] == "51.178.29.194"
    assert refused(sync.build_config, cfg.replace("51.178.29.194", "192.168.1.5"))  # private
    assert refused(sync.build_config, cfg.replace("51.178.29.194", "example.com"))

    ssl = "atmosphere/exefs_patches/disable_ca_verification/0A6CD7F6CF67BEB4DC9FFFC999EB37A80A788793.ips"
    good = {
        ssl: b"PATCH",
        "atmosphere/exefs_patches/s3peername/0123456789ABCDEF.ips": b"PATCH",
        "atmosphere/exefs_patches/nextendo_bcat_signature/0123456789ABCDEF.ips": b"PATCH",
        "atmosphere/nro_patches/disable_browser_ca_verification/0123456789ABCDEF0123456789ABCDEF.ips": b"PATCH",
        "atmosphere/contents/0100000000000803/romfs/browser/RootCaEtc.pem": b"PEM",
        "rootCA.pem": b"ignored",
    }
    files = sync.collect_patches(tree(good))
    assert set(files) == {
        "atmosphere/exefs_patches/nextendo_disable_ca_verification/0A6CD7F6CF67BEB4DC9FFFC999EB37A80A788793.ips",
        "atmosphere/exefs_patches/nextendo_s3peername/0123456789ABCDEF.ips",
        "atmosphere/exefs_patches/nextendo_bcat_signature/0123456789ABCDEF.ips",  # no double prefix
        "atmosphere/nro_patches/nextendo_disable_browser_ca_verification/0123456789ABCDEF0123456789ABCDEF.ips",
        "atmosphere/contents/0100000000000803/romfs/browser/RootCaEtc.pem",
    }
    # a sysmodule, a file in another title, a non-IPS file or no ssl folder: stop
    for extra in ("atmosphere/contents/4200000000000666/exefs.nsp",
                  "atmosphere/contents/0100000000001000/romfs/lyt/x.szs",
                  "atmosphere/exefs_patches/s3peername/readme.txt",
                  "atmosphere/kip_patches/x/0123456789ABCDEF.ips"):
        assert refused(sync.collect_patches, tree(dict(good, **{extra: b"x"}))), extra
    assert refused(sync.collect_patches, tree({k: v for k, v in good.items() if k != ssl}))
    assert refused(sync.collect_patches, tree(dict(good, **{ssl: b"x" * (sync.MAX_FILE + 1)})))
    print("test_nextendo_sync_contract: ok")


if __name__ == "__main__":
    main()
