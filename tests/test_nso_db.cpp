// NSO core: text-level lclassics.titlesdb editing on real db files (NES v0 list, SNES v0 dict with Python
// separators, NES update pretty printed, N64 mod compact) and edge cases. Every byte outside the edit stays.
#include "nso_test_util.hpp"
#include "nso/json.hpp"
#include "nso/titlesdb.hpp"

using namespace nso;
namespace db = nso::titlesdb;

static std::vector<std::string> Lines(const std::string& s) {
    std::vector<std::string> v;
    std::istringstream in(s);
    std::string l;
    while (std::getline(in, l)) v.push_back(l);
    return v;
}

static void RealFile(const std::string& name, bool want_list) {
    const std::string orig = ReadFile(name);
    const auto codes = Lines(ReadFile(name + ".codes"));
    const auto got = db::ListCodes(orig);
    CHECK(got.has_value());
    CHECK(*got == codes);
    CHECK_EQ(db::Scan(orig).is_list, want_list);

    // add: bytes before and after the insertion point are untouched, entry text is exactly ours
    const std::string entry = json::Obj().Str("code", "X-1_e").Str("title", "Brace } \"q\" ] \\").Int("n", 1).Text();
    std::string t = orig;
    CHECK(db::Add(t, "X-1_e", entry));
    const db::Db before = db::Scan(orig);
    const size_t pos = before.entries.empty() ? before.open + 1 : before.entries.back().end;
    const std::string piece = t.substr(pos, t.size() - orig.size());
    CHECK_EQ(t.substr(0, pos), orig.substr(0, pos));
    CHECK_EQ(t.substr(pos + piece.size()), orig.substr(pos));
    const db::Db after = db::Scan(t);
    CHECK(after.ok);
    CHECK_EQ(after.entries.size(), codes.size() + 1);
    CHECK_EQ(after.entries.back().code, std::string("X-1_e"));
    CHECK_EQ(std::string(db::EntryText(t, after, "X-1_e")), entry);
    for (size_t i = 0; i < codes.size(); i++)  // old entries byte-identical
        CHECK_EQ(db::EntryText(t, after, codes[i]), db::EntryText(orig, before, codes[i]));

    // add + remove = original, byte for byte
    CHECK(db::Remove(t, "X-1_e"));
    CHECK_EQ(t, orig);

    // replace an existing code: same count, new text
    t = orig;
    const std::string entry2 = json::Obj().Str("code", codes[1]).Str("title", "new").Text();
    CHECK(db::Add(t, codes[1], entry2));
    const db::Db rep = db::Scan(t);
    CHECK(rep.ok);
    CHECK_EQ(rep.entries.size(), codes.size());
    CHECK_EQ(std::string(db::EntryText(t, rep, codes[1])), entry2);

    // remove each entry (first, middle, last ...): the others stay byte-identical
    for (size_t i = 0; i < codes.size(); i++) {
        t = orig;
        CHECK(db::Remove(t, codes[i]));
        const db::Db r = db::Scan(t);
        CHECK(r.ok);
        CHECK_EQ(r.entries.size(), codes.size() - 1);
        for (size_t k = 0; k < codes.size(); k++) {
            if (k == i) { CHECK(!db::Has(r, codes[k])); continue; }
            CHECK_EQ(db::EntryText(t, r, codes[k]), db::EntryText(orig, before, codes[k]));
        }
    }
    CHECK(!db::Remove(t, "nope"));
}

int main() {
    RealFile("db_nes_v0.json", true);         // list form, ", " separators
    RealFile("db_snes_v0.json", false);       // dict form, Python separators
    RealFile("db_nes_upd_small.json", false); // pretty printed
    RealFile("db_n64_small.json", false);     // compact

    // empty containers, no leading comma, other top-level keys, BOM
    for (const char* base : {"{\"titles\":{}}", "{\"titles\":[]}", "{ \"a\" : [1,{\"x\":\"}\"}] , \"titles\" : { } , \"z\":1 }",
                             "\xEF\xBB\xBF{\"titles\":{}}"}) {
        std::string t = base;
        const bool list = db::Scan(t).is_list;
        CHECK(db::Scan(t).ok);
        CHECK(db::Add(t, "A", "{\"code\":\"A\"}"));
        CHECK(db::Add(t, "B", "{\"code\":\"B\"}"));
        CHECK((*db::ListCodes(t) == std::vector<std::string>{"A", "B"}));
        if (!list) CHECK(t.find("\"A\":{\"code\":\"A\"},\"B\":{\"code\":\"B\"}") != std::string::npos);
        CHECK(db::Remove(t, "B"));
        CHECK(db::Remove(t, "A"));
        CHECK_EQ(t, std::string(base));
    }
    CHECK_EQ(db::Scan("{\"titles\":{}}").ok, true);
    CHECK(!db::Scan("not json").ok);
    CHECK(!db::Scan("{\"other\":{}}").ok);
    CHECK(!db::Scan("{\"titles\":{\"A\":{\"x\":").ok);  // truncated
    std::string bad = "{\"other\":1}";
    CHECK(!db::Add(bad, "A", "{}"));
    CHECK_EQ(bad, std::string("{\"other\":1}"));

    // escaping == python json.dumps(ensure_ascii=False)
    CHECK_EQ(json::Quote("a\"b\\c\n\r\t\b\f\x01\x1f\x7f\xC2\xA9\xE3\x83\xBC"),
             std::string("\"a\\\"b\\\\c\\n\\r\\t\\b\\f\\u0001\\u001f\x7f\xC2\xA9\xE3\x83\xBC\""));
    CHECK_EQ(json::Quote("bad\xFF" "x\xC3"), std::string("\"bad?x?\""));  // invalid UTF-8 -> '?'
    CHECK_EQ(json::Quote("\xED\xA0\x80"), std::string("\"???\""));         // surrogate is invalid
    return Finish("test_nso_db");
}
