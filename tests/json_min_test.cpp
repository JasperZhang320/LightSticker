// Smoke test for src/json_min.h. Not part of the production build; the
// project's CMakeLists deliberately ignores this folder. It is built and
// run manually during development on any platform with a recent clang/g++.
//
// Compile:
//   clang++ -std=c++17 -Wall -Wextra tests/json_min_test.cpp -o /tmp/jmt
//   /tmp/jmt

#include "../src/json_min.h"

#include <cstdlib>
#include <iostream>
#include <string>

using json_min::Value;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    Value root = Value::make_obj();
    root["version"] = Value::make_int(1);
    Value arr = Value::make_arr();
    {
        Value s = Value::make_obj();
        s["x"] = Value::make_int(120);
        s["y"] = Value::make_int(-30);
        s["w"] = Value::make_int(320);
        s["h"] = Value::make_int(200);
        s["theme"] = Value::make_int(1);
        s["font"] = Value::make_int(0);
        s["size"] = Value::make_int(2);
        s["locked"] = Value::make_bool(true);
        s["text"] = Value::make_str(
            "Hello\n\"world\"\t\\backslash\nUTF-8: \xe4\xb8\xad\xe6\x96\x87");
        arr.a.push_back(std::move(s));
    }
    {
        Value s = Value::make_obj();
        s["text"] = Value::make_str("");
        arr.a.push_back(std::move(s));
    }
    root["stickers"] = std::move(arr);

    const std::string text = json_min::write(root);

    Value parsed;
    check(json_min::parse(text, parsed), "round-trip parse");
    check(parsed.type == Value::Type::Obj, "root is object");
    check(parsed.find("version")->as_int() == 1, "version int round-trip");

    const Value* stickers = parsed.find("stickers");
    check(stickers && stickers->type == Value::Type::Arr, "stickers is array");
    check(stickers->a.size() == 2, "two stickers");

    const Value& s0 = stickers->a[0];
    check(s0.find("x")->as_int() == 120, "x");
    check(s0.find("y")->as_int() == -30, "negative int");
    check(s0.find("locked")->as_bool(), "locked bool");
    const std::string& tx = s0.find("text")->as_str();
    check(tx.find("Hello\n") == 0, "newline preserved");
    check(tx.find("\"world\"") != std::string::npos, "embedded quotes preserved");
    check(tx.find("\\backslash") != std::string::npos, "backslash preserved");
    check(tx.find("\xe4\xb8\xad\xe6\x96\x87") != std::string::npos, "UTF-8 round-trip");

    Value v;
    check(!json_min::parse("{", v), "reject incomplete object");
    check(!json_min::parse("[1,]", v), "reject trailing comma");
    check(!json_min::parse("\"abc\\u\"", v), "reject bad unicode escape");
    check(!json_min::parse("12.5", v), "reject floats by design");
    check(!json_min::parse("nul", v), "reject incomplete null");
    check( json_min::parse("null", v), "accept null");
    check( json_min::parse("[]", v), "accept empty array");
    check( json_min::parse("{}", v), "accept empty object");
    check( json_min::parse("  true  ", v), "accept boolean w/ ws");

    check(json_min::parse("\"\\u00e9\"", v), "parse BMP escape");
    check(v.as_str() == "\xc3\xa9", "U+00E9 -> UTF-8 C3 A9");
    check(json_min::parse("\"\\uD83D\\uDE00\"", v), "parse surrogate pair");
    check(v.as_str() == "\xf0\x9f\x98\x80", "U+1F600 grinning face");

    std::cout << "json_min: all tests passed (" << text.size() << " bytes round-tripped)\n";
    return 0;
}
