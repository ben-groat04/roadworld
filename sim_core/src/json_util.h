#pragma once
// Private JSON-reading helpers shared by the data loaders (region_pack, markings, layout_io).
// Exception-free: parse with allow_exceptions=false; errors travel as a message in Err and the
// first error wins. Not a public header (nlohmann is a private dependency of sim_core).
#include <string>

#include "nlohmann/json.hpp"

namespace cp::jsonu {

using json = nlohmann::json;

struct Err {
    std::string msg;
    bool failed() const { return !msg.empty(); }
    void set(const std::string& m) {
        if (msg.empty()) msg = m;
    }
};

// The member `key` of object `j`, or nullptr when `j` is not an object or has no such member.
inline const json* member(const json& j, const char* key) {
    if (!j.is_object()) return nullptr;
    auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

}  // namespace cp::jsonu
