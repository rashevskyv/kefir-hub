// Host test for sphaira/include/nextendo_versions.hpp and nextendo_accounts.hpp

#include "nextendo_accounts.hpp"
#include "nextendo_versions.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::nextendo;

int main() {
    assert(*ParseVersion("v5.0.2") == (Version{5, 0, 2}));
    assert(*ParseVersion("5.1") == (Version{5, 1, 0}));
    assert(*ParseVersion("v5.0.2-rc1") == (Version{5, 0, 2}));
    assert(!ParseVersion("") && !ParseVersion("abc") && !ParseVersion("v"));
    assert(Compare({5, 0, 1}, {5, 0, 2}) < 0 && Compare({5, 1, 0}, {5, 0, 9}) > 0 && Compare({4, 9, 9}, {5, 0, 0}) < 0);
    assert(Compare({5, 0, 2}, {5, 0, 2}) == 0);

    assert(ParseHealthVersion("{\"ok\":true}").empty());
    assert(ParseHealthVersion("{\"ok\":true,\"version\":\"v5.0.3\"}") == "v5.0.3");
    assert(ParseHealthVersion("{\"version\": \"nonsense\"}").empty());
    assert(ParseHealthVersion("{\"version\":").empty());

    // the real case: Hub carries v5.0.1, the forge has v5.0.2
    assert(Judge({"v5.0.1", "v5.0.2", ""}) == Verdict::HubBehind);
    assert(Judge({"v5.0.2", "v5.0.2", ""}) == Verdict::Match);
    assert(Judge({"v5.0.3", "v5.0.2", ""}) == Verdict::HubAhead);
    assert(Judge({"v5.0.1", "", ""}) == Verdict::Unknown);
    assert(Judge({"v5.0.1", "v5.0.1", "v5.1.0"}) == Verdict::ServerNewer);
    assert(Judge({"v5.0.1", "", "v5.1.0"}) == Verdict::ServerNewer); // server alone is enough
    assert(Judge({"v5.0.1", "v5.0.1", "v5.0.1"}) == Verdict::Match);
    assert(ActionFor(Verdict::HubBehind).offer_update && ActionFor(Verdict::HubBehind).warn);
    assert(!ActionFor(Verdict::Match).warn && !ActionFor(Verdict::Unknown).offer_update && !ActionFor(Verdict::HubAhead).warn);

    assert(VersionsDue(1000, 0) && VersionsDue(10, 1000) && !VersionsDue(1000 + 3599, 1000) && VersionsDue(1000 + 3600, 1000));

    // account marks
    assert(MarkFor(true, false, false) == AccountMark::NotLinked);
    assert(MarkFor(true, false, true) == AccountMark::NotLinked); // unlinked meanwhile
    assert(MarkFor(true, true, true) == AccountMark::Nextendo);
    assert(MarkFor(true, true, false) == AccountMark::OtherLink);
    assert(MarkFor(false, false, true) == AccountMark::NextendoUnread);
    assert(MarkFor(false, false, false) == AccountMark::Unknown);
    assert(CanSignIn(AccountMark::NotLinked) && CanSignIn(AccountMark::Unknown));
    assert(!CanSignIn(AccountMark::OtherLink) && !CanSignIn(AccountMark::Nextendo));

    const std::string a(32, 'A');
    const std::string b = std::string(31, '0') + "1";
    auto rec = ParseRecord(a + "," + b + "," + a + ",zz,12");
    assert(rec.size() == 2 && rec[0] == a && rec[1] == b);
    assert(ParseRecord(std::string(32, 'a')).front() == a); // lower case is accepted
    assert(FormatRecord(rec) == a + "," + b);
    assert(IsRecorded(Remember(rec, a), a) && Remember(rec, a).size() == 2);
    assert(Forget(rec, a).size() == 1 && !IsRecorded(Forget(rec, a), a));
    assert(ParseRecord("").empty() && FormatRecord({}).empty());

    std::puts("test_nextendo_versions: ok");
    return 0;
}
