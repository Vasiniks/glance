#include "telemetry.h"
#include <algorithm>
#include <cmath>

#pragma comment(lib, "pdh.lib")

namespace glance {

static inline double ctr_val(PDH_HCOUNTER c) noexcept {
    PDH_FMT_COUNTERVALUE v{};
    if (PdhGetFormattedCounterValue(c, PDH_FMT_DOUBLE, nullptr, &v) != ERROR_SUCCESS) return 0;
    if (v.CStatus != ERROR_SUCCESS && v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA) return 0;
    double d = v.doubleValue;
    if (!std::isfinite(d)) return 0;
    return d;
}

static std::vector<std::wstring> expand(const wchar_t* wild) {
    DWORD bufSz = 0;
    PDH_STATUS st = PdhExpandCounterPathW(wild, nullptr, &bufSz);
    if (st != PDH_MORE_DATA) return {};
    std::vector<wchar_t> buf(bufSz);
    st = PdhExpandCounterPathW(wild, buf.data(), &bufSz);
    if (st != ERROR_SUCCESS) return {};
    std::vector<std::wstring> out;
    out.reserve(16);
    wchar_t* p = buf.data();
    while (*p) { out.emplace_back(p); p += wcslen(p) + 1; }
    return out;
}

static void extract_instance(const std::wstring& path, wchar_t* out, size_t cap) {
    size_t a = path.find(L'(');
    size_t b = path.find(L')', a + 1);
    if (a == std::wstring::npos || b == std::wstring::npos) { out[0] = 0; return; }
    std::wstring ins = path.substr(a + 1, b - a - 1);
    wcsncpy_s(out, cap, ins.c_str(), _TRUNCATE);
}

bool Collector::init_pdh() {
    if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) return false;

    auto cpu_paths = expand(L"\\Processor(*)\\% Processor Time");
    cpu_ctrs_.reserve(cpu_paths.size());
    for (auto& p : cpu_paths) {
        if (p.find(L"_Total") != std::wstring::npos) {
            PDH_HCOUNTER c;
            if (PdhAddCounterW(query_, p.c_str(), 0, &c) == ERROR_SUCCESS) total_cpu_ = c;
            continue;
        }
        PDH_HCOUNTER c;
        if (PdhAddCounterW(query_, p.c_str(), 0, &c) == ERROR_SUCCESS) cpu_ctrs_.push_back(c);
    }

    auto d_act = expand(L"\\PhysicalDisk(*)\\% Disk Time");
    auto d_rd  = expand(L"\\PhysicalDisk(*)\\Disk Read Bytes/sec");
    auto d_wr  = expand(L"\\PhysicalDisk(*)\\Disk Write Bytes/sec");

    d_count_ = 0;
    for (size_t i = 0; i < d_act.size() && d_count_ < 16; ++i) {
        if (d_act[i].find(L"_Total") != std::wstring::npos) continue;
        PDH_HCOUNTER c;
        if (PdhAddCounterW(query_, d_act[i].c_str(), 0, &c) != ERROR_SUCCESS) continue;
        d_active_.push_back(c);
        extract_instance(d_act[i], d_names_[d_count_], 32);
        ++d_count_;
    }
    // read/write align to same count (best effort)
    d_count_ = std::min(d_count_, (size_t)16);
    for (size_t i = 0, added = 0; i < d_rd.size() && added < d_count_; ++i) {
        if (d_rd[i].find(L"_Total") != std::wstring::npos) continue;
        PDH_HCOUNTER c;
        if (PdhAddCounterW(query_, d_rd[i].c_str(), 0, &c) == ERROR_SUCCESS) { d_read_.push_back(c); ++added; }
    }
    for (size_t i = 0, added = 0; i < d_wr.size() && added < d_count_; ++i) {
        if (d_wr[i].find(L"_Total") != std::wstring::npos) continue;
        PDH_HCOUNTER c;
        if (PdhAddCounterW(query_, d_wr[i].c_str(), 0, &c) == ERROR_SUCCESS) { d_write_.push_back(c); ++added; }
    }
    d_count_ = std::min({d_count_, d_active_.size(), d_read_.size(), d_write_.size()});

    auto n_rx = expand(L"\\Network Interface(*)\\Bytes Received/sec");
    auto n_tx = expand(L"\\Network Interface(*)\\Bytes Sent/sec");
    n_count_ = 0;
    for (size_t i = 0; i < n_rx.size() && n_count_ < 32; ++i) {
        if (n_rx[i].find(L"_Total") != std::wstring::npos && n_rx.size() > 1) continue;
        PDH_HCOUNTER c;
        if (PdhAddCounterW(query_, n_rx[i].c_str(), 0, &c) != ERROR_SUCCESS) continue;
        n_rx_.push_back(c);
        extract_instance(n_rx[i], n_names_[n_count_], 64);
        ++n_count_;
    }
    for (size_t i = 0, added = 0; i < n_tx.size() && added < n_count_; ++i) {
        if (n_tx[i].find(L"_Total") != std::wstring::npos && n_tx.size() > 1) continue;
        PDH_HCOUNTER c;
        if (PdhAddCounterW(query_, n_tx[i].c_str(), 0, &c) == ERROR_SUCCESS) { n_tx_.push_back(c); ++added; }
    }
    n_count_ = std::min({n_count_, n_rx_.size(), n_tx_.size()});

    // pick net
    chosen_net_ = 0;
    {
        std::lock_guard<std::mutex> g(iface_mu_);
        if (!net_hint_.empty()) {
            for (size_t i = 0; i < n_count_; ++i)
                if (wcsstr(n_names_[i], net_hint_.c_str())) { chosen_net_ = i; break; }
        }
    }

    PdhCollectQueryData(query_);
    first_ = true;
    return true;
}

void Collector::close_pdh() noexcept {
    if (query_) { PdhCloseQuery(query_); query_ = nullptr; }
    cpu_ctrs_.clear(); d_active_.clear(); d_read_.clear(); d_write_.clear();
    n_rx_.clear(); n_tx_.clear();
    total_cpu_ = nullptr;
    d_count_ = 0; n_count_ = 0;
}

void Collector::run() {
    while (running_.load(std::memory_order_relaxed)) {
        uint32_t ms = interval_ms_.load(std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        if (!running_.load()) break;

        if (!query_ && !init_pdh()) { std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
        PdhCollectQueryData(query_);
        if (first_) { first_ = false; continue; }

        Snapshot snap;
        snap.timestamp = Clock::now();

        // CPU
        snap.cpu.count = std::min(cpu_ctrs_.size(), (size_t)64);
        for (size_t i = 0; i < snap.cpu.count; ++i) snap.cpu.per_core[i] = std::clamp(ctr_val(cpu_ctrs_[i]), 0.0, 100.0);
        if (total_cpu_) snap.cpu.total = std::clamp(ctr_val(total_cpu_), 0.0, 100.0);
        else if (snap.cpu.count) {
            double s = 0; for (size_t i = 0; i < snap.cpu.count; ++i) s += snap.cpu.per_core[i];
            snap.cpu.total = s / snap.cpu.count;
        }

        // RAM — GlobalMemoryStatusEx lives in kernel32, no heap
        MEMORYSTATUSEX mem{}; mem.dwLength = sizeof(mem);
        if (GlobalMemoryStatusEx(&mem) && mem.ullTotalPhys) {
            snap.ram.total_bytes = mem.ullTotalPhys;
            snap.ram.used_bytes = mem.ullTotalPhys - mem.ullAvailPhys;
            snap.ram.used_percent = 100.0 * (double)snap.ram.used_bytes / (double)snap.ram.total_bytes;
        }

        // Disks
        snap.disk_count = std::min(d_count_, (size_t)16);
        for (size_t i = 0; i < snap.disk_count; ++i) {
            auto& ds = snap.disks[i];
            wcsncpy_s(ds.name, d_names_[i], _TRUNCATE);
            ds.active = std::clamp(ctr_val(d_active_[i]), 0.0, 100.0);
            ds.read_bps = std::max(0.0, ctr_val(d_read_[i]));
            ds.write_bps = std::max(0.0, ctr_val(d_write_[i]));
        }

        // Net — pick most active, but respect hint
        if (n_count_ > 0) {
            size_t best = chosen_net_ < n_count_ ? chosen_net_ : 0;
            double bestSum = -1;
            for (size_t i = 0; i < n_count_; ++i) {
                double s = ctr_val(n_rx_[i]) + ctr_val(n_tx_[i]);
                if (s > bestSum) { bestSum = s; best = i; }
            }
            {
                std::lock_guard<std::mutex> g(iface_mu_);
                if (!net_hint_.empty()) best = chosen_net_;
            }
            chosen_net_ = best;
            auto& ns = snap.net;
            wcsncpy_s(ns.name, n_names_[best], _TRUNCATE);
            ns.rx_bps = std::max(0.0, ctr_val(n_rx_[best]));
            ns.tx_bps = std::max(0.0, ctr_val(n_tx_[best]));
        }

        snap.valid = true;
        mailbox.publish(snap);
    }
}

bool Collector::start(const std::wstring& hint) {
    if (running_.exchange(true)) return false;
    { std::lock_guard<std::mutex> g(iface_mu_); net_hint_ = hint; }
    thread_ = std::jthread([this](std::stop_token){ run(); });
    return true;
}
void Collector::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    close_pdh();
}
void Collector::set_net_iface(std::wstring s) {
    std::lock_guard<std::mutex> g(iface_mu_);
    net_hint_ = std::move(s);
    // if running, update chosen
    for (size_t i = 0; i < n_count_; ++i)
        if (wcsstr(n_names_[i], net_hint_.c_str())) { chosen_net_ = i; break; }
}

} // namespace glance
