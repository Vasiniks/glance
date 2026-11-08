#include "../src/config.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace glance;
    Config cfg; cfg.thickness = 64; cfg.interval_ms = 500; cfg.edge = Edge::Bottom;
    std::wstring path = config_path();
    wprintf(L"config path: %ls\n", path.c_str());
    assert(save_config(cfg));
    Config loaded; assert(load_config(loaded));
    assert(loaded.thickness == 64);
    assert(loaded.interval_ms == 500);
    assert(loaded.edge == Edge::Bottom);

    // mailbox no longer exposes format helpers — just test config roundtrip
    printf("glance_tests: OK (lightweight build)\n");
    return 0;
}
