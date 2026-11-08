#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
#include <optional>
#include <mutex>
#include <thread>
#include <atomic>
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

namespace glance {

using Clock = std::chrono::steady_clock;

struct CpuSample {
    std::vector<double> per_core; // 0..100, index = logical processor
    double total = 0;
};

struct RamSample {
    double used_percent = 0; // 0..100
    uint64_t used_bytes = 0;
    uint64_t total_bytes = 0;
};

struct DiskSample {
    std::wstring name; // e.g. "0 C:"
    double active = 0; // 0..100
    double read_bps = 0;
    double write_bps = 0;
};

struct NetSample {
    std::wstring name;
    double rx_bps = 0;
    double tx_bps = 0;
};

struct Snapshot {
    Clock::time_point timestamp = Clock::now();
    CpuSample cpu;
    RamSample ram;
    std::vector<DiskSample> disks;
    NetSample net;
    bool valid = false;
};

// Thread-safe single-slot mailbox. No allocations on read path if possible.
class Mailbox {
public:
    void publish(Snapshot s) {
        std::lock_guard<std::mutex> g(mu_);
        data_ = std::move(s);
        gen_++;
    }
    // returns generation
    uint64_t try_consume(Snapshot& out, uint64_t last_gen) {
        std::lock_guard<std::mutex> g(mu_);
        if (gen_ == last_gen) return last_gen;
        out = data_;
        return gen_;
    }
    uint64_t generation() const {
        std::lock_guard<std::mutex> g(mu_);
        return gen_;
    }
private:
    mutable std::mutex mu_;
    Snapshot data_;
    uint64_t gen_ = 0;
};

// Minimal PDH collector — only uses pdh.dll, no WMI, no DXGI, no vendor SDKs.
class Collector {
public:
    Collector() = default;
    ~Collector() { stop(); }

    bool start(const std::wstring& net_iface_hint = L"");
    void stop();
    void set_interval(uint32_t ms) { interval_ms_.store(ms, std::memory_order_relaxed); }
    void set_net_iface(std::wstring s) {
        std::lock_guard<std::mutex> g(iface_mu_);
        net_iface_hint_ = std::move(s);
    }

    Mailbox mailbox;

private:
    void run();
    bool init_pdh();
    void close_pdh();

    std::jthread thread_;
    std::atomic<uint32_t> interval_ms_{1000};
    std::mutex iface_mu_;
    std::wstring net_iface_hint_;

    PDH_HQUERY query_ = nullptr;
    std::vector<PDH_HCOUNTER> cpu_counters_;
    PDH_HCOUNTER total_cpu_ = nullptr;
    std::vector<PDH_HCOUNTER> disk_active_;
    std::vector<PDH_HCOUNTER> disk_read_;
    std::vector<PDH_HCOUNTER> disk_write_;
    std::vector<std::wstring> disk_names_;
    std::vector<PDH_HCOUNTER> net_rx_;
    std::vector<PDH_HCOUNTER> net_tx_;
    std::vector<std::wstring> net_names_;
    size_t chosen_net_ = 0;

    bool first_sample_ = true;
    std::atomic<bool> running_{false};
};

std::string format_bytes(double bps);
std::string format_percent(double v);

} // namespace glance
