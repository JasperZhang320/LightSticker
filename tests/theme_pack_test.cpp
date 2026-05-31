// Validates that every shipped themes/*.json file is parseable by json_min
// and matches the documented schema. Not part of the production build; this
// gives us a quick portable smoke test for the example packs.
//
// Compile:
//   clang++ -std=c++17 -Wall -Wextra tests/theme_pack_test.cpp -o /tmp/tpt
//   /tmp/tpt

#include "../src/json_min.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using json_min::Value;

static bool LoadFileText(const fs::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool ParseHexColor(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return false;
    for (size_t i = 1; i < 7; ++i) {
        const char c = s[i];
        const bool ok = (c >= '0' && c <= '9') ||
                        (c >= 'a' && c <= 'f') ||
                        (c >= 'A' && c <= 'F');
        if (!ok) return false;
    }
    return true;
}

static bool ValidatePack(const fs::path& p) {
    std::string text;
    if (!LoadFileText(p, text)) {
        std::cerr << "  cannot read\n"; return false;
    }
    Value root;
    if (!json_min::parse(text, root) || root.type != Value::Type::Obj) {
        std::cerr << "  not parseable as object\n"; return false;
    }
    auto require_int = [&](const char* key, long long lo, long long hi) {
        const Value* v = root.find(key);
        if (!v || v->type != Value::Type::Int) return false;
        return v->i >= lo && v->i <= hi;
    };
    auto require_str = [&](const char* key) {
        const Value* v = root.find(key);
        return v && v->type == Value::Type::Str && !v->s.empty();
    };
    if (!require_int("schemaVersion", 1, 1)) { std::cerr << "  schemaVersion\n"; return false; }
    if (!require_str("id"))                  { std::cerr << "  id\n";            return false; }
    if (!require_str("name"))                { std::cerr << "  name\n";          return false; }

    const Value* tokens = root.find("tokens");
    if (!tokens || tokens->type != Value::Type::Obj) { std::cerr << "  tokens\n"; return false; }

    const Value* bg = tokens->find("bgColor");
    const Value* fg = tokens->find("textColor");
    if (!bg || bg->type != Value::Type::Str || !ParseHexColor(bg->s)) {
        std::cerr << "  bgColor invalid\n"; return false;
    }
    if (!fg || fg->type != Value::Type::Str || !ParseHexColor(fg->s)) {
        std::cerr << "  textColor invalid\n"; return false;
    }
    if (const Value* op = tokens->find("opacityPercent")) {
        if (op->type != Value::Type::Int || op->i < 20 || op->i > 100) {
            std::cerr << "  opacityPercent out of range\n"; return false;
        }
    }
    if (const Value* cr = tokens->find("cornerRadius")) {
        if (cr->type != Value::Type::Int || cr->i < 0 || cr->i > 48) {
            std::cerr << "  cornerRadius out of range\n"; return false;
        }
    }
    return true;
}

int main(int argc, char** argv) {
    fs::path dir = (argc >= 2) ? fs::path(argv[1])
                               : fs::path(__FILE__).parent_path().parent_path() / "themes";
    if (!fs::exists(dir)) {
        std::cerr << "themes dir not found: " << dir << "\n";
        return 1;
    }
    int total = 0, ok = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") continue;
        ++total;
        std::cout << "checking " << entry.path().filename().string() << "\n";
        if (ValidatePack(entry.path())) {
            ++ok;
        }
    }
    std::cout << ok << "/" << total << " pack files validate\n";
    return (ok == total && total > 0) ? 0 : 1;
}
