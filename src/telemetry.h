#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>
#include <windows.h>
#include <pdh.h>

namespace glance {

using Clock = std::chrono::steady_clock;

struct CpuSample {
    // Avoid per-sample allocs: reuse vector capacity, but keep as vector for simplicity
    double per_core[64]{}; // support up to 64 logical procs (common max), beyond falls back to total only
    size_t count = 0;
    double total = 0;
    // helper for renderer
    std::vector<double> to_vec() const { return std::vector<double>(per_core, per_core + count); }
};

struct RamSample {
    double used_percent = 0;
    uint64_t used_bytes = 0;
    uint64_t total_bytes = 0;
};

struct DiskSample {
    wchar_t name[32]{};
    double active = 0;
    double read_bps = 0;
    double write_bps = 0;
};

struct NetSample {
    wchar_t name[64]{};
    double rx_bps = 0;
    double tx_bps = 0;
};

struct Snapshot {
    Clock::time_point timestamp = Clock::now();
    CpuSample cpu;
    RamSample ram;
    DiskSample disks[16]{}; // max 16 disks (typical <4) — no heap alloc on publish
    size_t disk_count = 0;
    NetSample net;
    bool valid = false;
};

// SRW-friendly minimal mailbox — one slot, no extra copies beyond Snapshot (now stack-only for disks)
class Mailbox {
public:
    void publish(const Snapshot& s) {
        std::lock_guard<std::mutex> g(mu_);
        data_ = s;
        ++gen_;
    }
    uint64_t try_consume(Snapshot& out, uint64_t last) {
        std::lock_guard<std::mutex> g(mu_);
        if (gen_ == last) return last;
        out = data_;
        return gen_;
    }
private:
    mutable std::mutex mu_;
    Snapshot data_{};
    uint64_t gen_ = 0;
};

class Collector {
public:
    Collector() = default;
    ~Collector() { stop(); }

    bool start(const std::wstring& net_hint = L"");
    void stop();
    void set_interval(uint32_t ms) noexcept { interval_ms_.store(ms, std::memory_order_relaxed); }
    void set_net_iface(std::wstring s);

    Mailbox mailbox;

private:
    void run();
    bool init_pdh();
    void close_pdh() noexcept;

    std::jthread thread_;
    std::atomic<uint32_t> interval_ms_{1000};
    std::mutex iface_mu_;
    std::wstring net_hint_;

    PDH_HQUERY query_ = nullptr;
    std::vector<PDH_HCOUNTER> cpu_ctrs_;
    PDH_HCOUNTER total_cpu_ = nullptr;
    std::vector<PDH_HCOUNTER> d_active_, d_read_, d_write_;
    wchar_t d_names_[16][32]{};
    size_t d_count_ = 0;

    std::vector<PDH_HCOUNTER> n_rx_, n_tx_;
    wchar_t n_names_[32][64]{};
    size_t n_count_ = 0;
    size_t chosen_net_ = 0;

    bool first_ = true;
    std::atomic<bool> running_{false};
};

} // namespace glance
