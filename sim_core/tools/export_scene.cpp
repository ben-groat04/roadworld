// export_scene [--data <dir>] <fixture>  ->  JSON (contract v1) on stdout.
#include <cstdio>
#include <cstring>
#include <string>

#include "scene_fixture.h"

#ifndef CP_DATA_DIR
#define CP_DATA_DIR "data"
#endif

int main(int argc, char** argv) {
    std::string data = CP_DATA_DIR, name;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else name = argv[i];
    }
    if (name.empty()) {
        std::fprintf(stderr, "usage: export_scene [--data <dir>] <fixture>   fixtures:");
        for (const auto& n : cp::tools::scene_names()) std::fprintf(stderr, " %s", n.c_str());
        std::fprintf(stderr, "\n");
        return 2;
    }
    std::string err;
    auto sc = cp::tools::build_scene(name, data, err);
    if (!sc) {
        std::fprintf(stderr, "export_scene: %s\n", err.c_str());
        return 1;
    }
    const std::string json = cp::tools::export_scene_json(*sc, cp::tools::scene_junctions(*sc));
    std::fwrite(json.data(), 1, json.size(), stdout);
    return 0;
}
