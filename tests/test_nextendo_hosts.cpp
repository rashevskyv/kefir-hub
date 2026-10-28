// Host test for sphaira/include/nextendo_hosts.hpp

#include "nextendo_hosts.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::nextendo;

int main() {
    // IPv4 check
    assert(IsIpv4("51.178.29.194") && IsIpv4("0.0.0.0") && IsIpv4("255.255.255.255"));
    assert(!IsIpv4("") && !IsIpv4("1.2.3") && !IsIpv4("1.2.3.4.5") && !IsIpv4("256.1.1.1") && !IsIpv4("1.2.3.4 "));
    assert(!IsIpv4("a.b.c.d") && !IsIpv4("1..2.3") && !IsIpv4("1.2.3.4:80") && !IsIpv4("1234.1.1.1") && !IsIpv4("host.name"));

    // build: every placeholder is gone, the mark is in the first lines, last matching line wins so order is kept
    const auto ips = SnapshotIps();
    assert(ips.main == "51.178.29.194" && ips.nncs2 == "164.132.111.120");
    const auto text = BuildHosts(ips);
    assert(!text.empty() && text.find('{') == std::string::npos);
    assert(HasMark(text));
    assert(text.find("51.178.29.194    *.nintendo.com\n") != std::string::npos);
    assert(text.find("164.132.111.120 g2b309e01-lp1.s.n.srv.nintendo.net\n") != std::string::npos);
    assert(text.find("51.178.29.194          receive-%.dg.srv.nintendo.net\n") != std::string::npos);
    // d4c is not redirected on purpose (nim keeps a persistent flag)
    assert(text.find("d4c.nintendo.net") == std::string::npos);
    assert(text.find("ctest.cdn.nintendo.net\n") != std::string::npos);
    // a bad IP never gives a half file
    assert(BuildHosts({"1.2.3", ips.nncs2}).empty());
    assert(BuildHosts({ips.main, "evil\n1.1.1.1"}).empty());
    // other IP, same shape
    const auto other = BuildHosts({"10.0.0.1", "10.0.0.2"});
    assert(other.size() < text.size() && other.find("51.178.29.194") == std::string::npos);

    // classify: own file, stale file, user file, empty, missing
    assert(Classify(std::nullopt, text) == FileState::Missing);
    assert(Classify(std::string{}, text) == FileState::Missing);
    assert(Classify(std::string{" \n\t"}, text) == FileState::Missing);
    assert(Classify(text, text) == FileState::Current);
    assert(Classify(other, text) == FileState::Stale);
    assert(Classify(std::string{"1.2.3.4 example.com\n"}, text) == FileState::UserOwned);
    // a user file that quotes the mark far below the top is still the user's
    assert(Classify(std::string(600, '#') + std::string(kHostsMark), text) == FileState::UserOwned);

    // plans
    assert(PlanApply(FileState::Missing) == ApplyAction::Write);
    assert(PlanApply(FileState::Current) == ApplyAction::Skip);
    assert(PlanApply(FileState::Stale) == ApplyAction::Write);
    assert(PlanApply(FileState::UserOwned) == ApplyAction::BackupThenWrite);
    assert(PlanRemove(FileState::Current) == RemoveAction::Delete);
    assert(PlanRemove(FileState::Stale) == RemoveAction::Delete);
    assert(PlanRemove(FileState::UserOwned) == RemoveAction::Leave);
    assert(PlanRemove(FileState::Missing) == RemoveAction::Leave);

    // config text: the real lines of nextendo_config.h v4.0.11, a comment copy, garbage
    const std::string cfg =
        "#define NEXTENDO_SERVER_IP_DEFAULT  \"1.1.1.1\"\n"
        "#define NEXTENDO_SERVER_IP_ALT      \"3.135.232.168\"\n"
        "#define NEXTENDO_SERVER_IP_NNCSD2   \"2.2.2.2\"\n";
    const auto parsed = ParseConfigIps(cfg);
    assert(parsed && parsed->main == "1.1.1.1" && parsed->nncs2 == "2.2.2.2");
    assert(!ParseConfigIps(""));
    assert(!ParseConfigIps("garbage \"1.1.1.1\" NEXTENDO_SERVER_IP_DEFAULT"));
    assert(!ParseConfigIps("#define NEXTENDO_SERVER_IP_DEFAULT \"not-an-ip\"\n#define NEXTENDO_SERVER_IP_NNCSD2 \"2.2.2.2\"\n"));
    assert(!ParseConfigIps("// #define NEXTENDO_SERVER_IP_DEFAULT \"1.1.1.1\"\n#define NEXTENDO_SERVER_IP_NNCSD2 \"2.2.2.2\"\n"));
    assert(!ParseConfigIps("#define NEXTENDO_SERVER_IP_DEFAULT \"1.1.1.1\"\n")); // second IP missing
    assert(!ParseConfigIps("#define NEXTENDO_SERVER_IP_DEFAULT\n\"1.1.1.1\"\n#define NEXTENDO_SERVER_IP_NNCSD2 \"2.2.2.2\"\n"));

    // firmware guard: exact versions of the pinned tag only
    assert(FirmwareSupported(22, 5, 0) && FirmwareSupported(23, 0, 1));
    assert(!FirmwareSupported(23, 0, 0) && !FirmwareSupported(22, 4, 0) && !FirmwareSupported(23, 1, 0) && !FirmwareSupported(0, 0, 0));
    assert(AccountLinkAvailable(22) && !AccountLinkAvailable(23) && !AccountLinkAvailable(24));

    std::puts("test_nextendo_hosts: ok");
    return 0;
}
