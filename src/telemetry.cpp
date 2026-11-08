#include "telemetry.h"
#include <pdh.h>
#include <windows.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "pdh.lib")

namespace glance {

std::string format_bytes(double bps) {
    const char* units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
    double v = bps;
    int u = 0;
    while (v >= 1024.0 && u < 3) { v /= 1024.0; ++u; }
    char buf[64];
    if (v < 10) snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
    else snprintf(buf, sizeof(buf), "%.0f %s", v, units[u]);
    return std::string(buf);
}
std::string format_percent(double v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f%%", v);
    return std::string(buf);
}

static std::vector<std::wstring> pdh_expand(const wchar_t* wildcard) {
    DWORD bufSize = 0;
    DWORD itemSize = 0;
    PDH_STATUS st = PdhExpandCounterPathW(wildcard, nullptr, &bufSize);
    if (st != PDH_MORE_DATA) return {};
    std::vector<wchar_t> buf(bufSize);
    std::vector<wchar_t> items(1);
    itemSize = 0;
    st = PdhExpandCounterPathW(wildcard, buf.data(), &bufSize);
    if (st != ERROR_SUCCESS) return {};
    // buf is list of \0-separated strings, double \0 terminated
    std::vector<std::wstring> out;
    wchar_t* p = buf.data();
    while (*p) {
        out.emplace_back(p);
        p += wcslen(p) + 1;
    }
    return out;
}

bool Collector::init_pdh() {
    if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) return false;

    // CPU per core
    auto cpu_paths = pdh_expand(L"\\Processor(*)\\% Processor Time");
    for (auto& path : cpu_paths) {
        if (path.find(L"_Total") != std::wstring::npos) {
            PDH_HCOUNTER c;
            if (PdhAddCounterW(query_, path.c_str(), 0, &c) == ERROR_SUCCESS)
                total_cpu_ = c;
            continue;
        }
        // skip if not numeric index, but keep all
        if (path.find(L"\\Processor(") == std::wstring::npos) continue;
        PDH_HCOUNTER c;
        if (PdhAddCounterW(query_, path.c_str(), 0, &c) == ERROR_SUCCESS)
            cpu_counters_.push_back(c);
    }

    // Disk
    auto disk_active_paths = pdh_expand(L"\\PhysicalDisk(*)\\% Disk Time");
    auto disk_read_paths = pdh_expand(L"\\PhysicalDisk(*)\\Disk Read Bytes/sec");
    auto disk_write_paths = pdh_expand(L"\\PhysicalDisk(*)\\Disk Write Bytes/sec");

    // map by index parsing: simple by order matching _Total excluded
    auto add_filtered = [](const std::vector<std::wstring>& paths, std::vector<std::wstring>& names) {
        std::vector<std::wstring> out;
        for (auto& p : paths) {
            if (p.find(L"_Total") != std::wstring::npos) continue;
            // extract instance between ( )
            auto a = p.find(L'(');
            auto b = p.find(L')', a);
            if (a != std::wstring::npos && b != std::wstring::npos)
                out.push_back(p.substr(a + 1, b - a - 1));
            else
                out.push_back(p);
        }
        return out;
    };

    // Build unique disk names
    std::vector<std::wstring> instances = add_filtered(disk_active_paths, disk_names_);
    // dedup names
    std::sort(instances.begin(), instances.end());
    instances.erase(std::unique(instances.begin(), instances.end()), instances.end());

    for (auto& p : disk_active_paths) {
        if (p.find(L"_Total") != std::wstring::npos) continue;
        PDH_HCOUNTER c;
        if (PdhAddCounterW(query_, p.c_str(), 0, &c) == ERROR_SUCCESS) disk_active_.push_back(c);
    }
    for (auto& p : disk_read_paths) {
        if (p.find(L"_Total") != std::wstring::npos) continue;
        PDH_HCOUNTER c;
        if (PdhAddCounterW(query_, p.c_str(), 0, &c) == ERROR_SUCCESS) disk_read_.push_back(c);
    }
    for (auto& p : disk_write_paths) {
        if (p.find(L"_Total") != std::wstring::npos) continue;
        PDH_HCOUNTER c;
        if (PdhAddCounterW(query_, p.c_str(), 0, &c) == ERROR_SUCCESS) disk_write_.push_back(c);
    }
    // names aligned to counters (best effort)
    disk_names_ = instances;
    // pad if mismatch
    while (disk_names_.size() < disk_active_.size()) disk_names_.push_back(L"disk");

    // Network
    auto net_rx_paths = pdh_expand(L"\\Network Interface(*)\\Bytes Received/sec");
    auto net_tx_paths = pdh_expand(L"\\Network Interface(*)\\Bytes Sent/sec");
    std::vector<std::wstring> net_instances;
    for (auto& p : net_rx_paths) {
        auto a = p.find(L'(');
        auto b = p.find(L')', a);
        if (a != std::wstring::npos && b != std::wstring::npos)
            net_instances.push_back(p.substr(a + 1, b - a - 1));
    }
    std::sort(net_instances.begin(), net_instances.end());
    net_instances.erase(std::unique(net_instances.begin(), net_instances.end()), net_instances.end());

    for (auto& p : net_rx_paths) {
        PDH_HCOUNTER c;
        if (PdhAddCounterW(query_, p.c_str(), 0, &c) == ERROR_SUCCESS) net_rx_.push_back(c);
    }
    for (auto& p : net_tx_paths) {
        PDH_HCOUNTER c;
        if (PdhAddCounterW(query_, p.c_str(), 0, &c) == ERROR_SUCCESS) net_tx_.push_back(c);
    }
    net_names_ = net_instances;
    while (net_names_.size() < net_rx_.size()) net_names_.push_back(L"net");

    // pick chosen net
    {
        std::lock_guard<std::mutex> g(iface_mu_);
        if (!net_iface_hint_.empty()) {
            for (size_t i = 0; i < net_names_.size(); ++i) {
                if (net_names_[i].find(net_iface_hint_) != std::wstring::npos) { chosen_net_ = i; break; }
            }
        }
    }

    // prime first sample
    PdhCollectQueryData(query_);
    first_sample_ = true;
    return true;
}

void Collector::close_pdh() {
    if (query_) {
        PdhCloseQuery(query_);
        query_ = nullptr;
    }
    cpu_counters_.clear();
    disk_active_.clear();
    disk_read_.clear();
    disk_write_.clear();
    net_rx_.clear();
    net_tx_.clear();
    disk_names_.clear();
    net_names_.clear();
    total_cpu_ = nullptr;
}

static double counter_val(PDH_HCOUNTER c) {
    PDH_FMT_COUNTERVALUE v;
    if (PdhGetFormattedCounterValue(c, PDH_FMT_DOUBLE, nullptr, &v) != ERROR_SUCCESS) return 0;
    if (v.CStatus != ERROR_SUCCESS && v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA) return 0;
    return v.doubleValue;
}

void Collector::run() {
    while (running_.load(std::memory_order_relaxed)) {
        uint32_t ms = interval_ms_.load(std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        if (!running_.load()) break;

        if (!query_ && !init_pdh()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        PdhCollectQueryData(query_);

        if (first_sample_) {
            first_sample_ = false;
            continue; // need two samples
        }

        Snapshot snap;
        snap.timestamp = Clock::now();

        // CPU
        snap.cpu.per_core.reserve(cpu_counters_.size());
        for (auto c : cpu_counters_) snap.cpu.per_core.push_back(std::clamp(counter_val(c), 0.0, 100.0));
        if (total_cpu_) snap.cpu.total = std::clamp(counter_val(total_cpu_), 0.0, 100.0);
        else if (!snap.cpu.per_core.empty()) {
            double sum = 0; for (auto v : snap.cpu.per_core) sum += v;
            snap.cpu.total = sum / snap.cpu.per_core.size();
        }

        // RAM
        MEMORYSTATUSEX mem{};
        mem.dwLength = sizeof(mem);
        if (GlobalMemoryStatusEx(&mem)) {
            snap.ram.total_bytes = mem.ullTotalPhys;
            snap.ram.used_bytes = mem.ullTotalPhys - mem.ullAvailPhys;
            snap.ram.used_percent = snap.ram.total_bytes ? (100.0 * snap.ram.used_bytes / snap.ram.total_bytes) : 0;
        }

        // Disks
        size_t dn = std::min({disk_active_.size(), disk_read_.size(), disk_write_.size(), disk_names_.size()});
        for (size_t i = 0; i < dn; ++i) {
            DiskSample ds;
            ds.name = disk_names_[i];
            ds.active = std::clamp(counter_val(disk_active_[i]), 0.0, 100.0);
            ds.read_bps = std::max(0.0, counter_val(disk_read_[i]));
            ds.write_bps = std::max(0.0, counter_val(disk_write_[i]));
            snap.disks.push_back(std::move(ds));
        }

        // Net — choose most active if no hint
        if (!net_rx_.empty() && !net_tx_.empty()) {
            size_t best = chosen_net_;
            double best_sum = -1;
            for (size_t i = 0; i < std::min(net_rx_.size(), net_tx_.size()); ++i) {
                double s = counter_val(net_rx_[i]) + counter_val(net_tx_[i]);
                if (s > best_sum) { best_sum = s; best = i; }
            }
            // if hinted iface is still present, stay on it unless it is 0 and others are active
            {
                std::lock_guard<std::mutex> g(iface_mu_);
                if (!net_iface_hint_.empty() && chosen_net_ < net_names_.size()) best = chosen_net_;
            }
            chosen_net_ = best;
            snap.net.name = (chosen_net_ < net_names_.size()) ? net_names_[chosen_net_] : L"";
            snap.net.rx_bps = counter_val(net_rx_[chosen_net_]);
            snap.net.tx_bps = counter_val(net_tx_[chosen_net_]);
        }

        snap.valid = true;
        mailbox.publish(std::move(snap));
    }
}

bool Collector::start(const std::wstring& net_iface_hint) {
    if (running_.exchange(true)) return false;
    {
        std::lock_guard<std::mutex> g(iface_mu_);
        net_iface_hint_ = net_iface_hint;
    }
    thread_ = std::jthread([this](std::stop_token){ run(); });
    return true;
}

void Collector::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    close_pdh();
}

} // namespace glance
