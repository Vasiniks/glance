#include "../src/config.h"
#include "../src/telemetry.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace glance;

    // config round-trip
    Config cfg;
    cfg.thickness = 64;
    cfg.interval_ms = 500;
    cfg.edge = Edge::Bottom;
    cfg.show_cpu = true;
    auto path = config_path();
    std::wcout << L"config path: " << path << L"\n";
    assert(save_config(cfg));
    Config loaded;
    assert(load_config(loaded));
    assert(loaded.thickness == 64);
    assert(loaded.interval_ms == 500);
    assert(loaded.edge == Edge::Bottom);

    // format helpers
    assert(format_bytes(0).find("B/s") != std::string::npos);
    assert(format_bytes(2048).find("KB") != std::string::npos);
    assert(format_percent(42).find("%") != std::string::npos);

    // mailbox
    Mailbox mb;
    Snapshot s;
    s.valid = true;
    s.cpu.total = 50;
    mb.publish(s);
    Snapshot out;
    uint64_t gen = mb.try_consume(out, 0);
    assert(gen == 1);
    assert(out.valid);
    assert(out.cpu.total == 50);

    std::cout << "glance_tests: all OK\n";
    return 0;
}
