// main.cpp - benchmark driver
//
// Usage:
//   ./bench <variant> <threads> <push_ratio> <duration_ms> <prefill> [csv|header]
//
// Examples:
//   ./bench all 4 0.5 2000 1000 csv          # 4 threads, 50/50, run 2 s
//   ./bench HP  8 0.9 5000 0   csv           # HP only, 8 threads, 90% push
//   ./bench all 1 0.5 1000 100 header        # print CSV header then run

#include <iostream>
#include <iomanip>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <string>

#include "common.hpp"
#include "stack_leak.hpp"
#include "stack_rc.hpp"
#include "stack_hp.hpp"
#include "stack_ebr.hpp"

using namespace std::chrono;

struct BenchResult {
    double throughput_mops = 0;
    double avg_latency_ns  = 0;
    double p50_ns          = 0;
    double p99_ns          = 0;
    double p999_ns         = 0;
    int64_t peak_live      = 0;
    int64_t peak_pending   = 0;
    int64_t total_ops      = 0;
};

template<typename Stack>
BenchResult run_bench(int num_threads, double push_ratio,
                      int duration_ms, int prefill,
                      bool sample_latency) {
    Stack stack;
    lf::reset_counters();

    // Prefill
    for (int i = 0; i < prefill; i++) stack.push(i);

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};

    std::vector<std::thread> threads;
    std::vector<std::vector<uint64_t>> latencies(num_threads);
    std::vector<int64_t> ops_count(num_threads, 0);
    std::atomic<int64_t> peak_live{0};
    std::atomic<int64_t> peak_pending{0};

    // Monitor thread: periodically samples live_objects and pending_objects.
    std::thread monitor([&]() {
        while (!start.load(std::memory_order_acquire)) {}
        while (!stop.load(std::memory_order_relaxed)) {
            int64_t live = lf::live_objects();
            int64_t pending = lf::pending_objects();

            int64_t prev_live = peak_live.load(std::memory_order_relaxed);
            while (live > prev_live &&
                   !peak_live.compare_exchange_weak(prev_live, live)) {}

            int64_t prev_pending = peak_pending.load(std::memory_order_relaxed);
            while (pending > prev_pending &&
                   !peak_pending.compare_exchange_weak(prev_pending, pending)) {}

            std::this_thread::sleep_for(milliseconds(1));
        }
    });

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            std::mt19937 rng(t * 7919 + 1);
            std::uniform_real_distribution<double> coin(0, 1);
            int64_t local_ops = 0;

            // Latency sampling: 1/N rate to avoid storing every measurement.
            constexpr int LAT_SAMPLE_RATE = 64;
            if (sample_latency) latencies[t].reserve(2'000'000);

            while (!start.load(std::memory_order_acquire)) {}

            while (!stop.load(std::memory_order_relaxed)) {
                bool do_push = coin(rng) < push_ratio;
                int val = (int)local_ops;

                if (sample_latency && (local_ops % LAT_SAMPLE_RATE == 0)) {
                    auto t0 = high_resolution_clock::now();
                    if (do_push) {
                        stack.push(val);
                    } else {
                        int out;
                        stack.pop(out);
                    }
                    auto t1 = high_resolution_clock::now();
                    latencies[t].push_back(
                        duration_cast<nanoseconds>(t1 - t0).count());
                } else {
                    if (do_push) {
                        stack.push(val);
                    } else {
                        int out;
                        stack.pop(out);
                    }
                }
                local_ops++;
            }
            ops_count[t] = local_ops;
        });
    }

    auto t0 = high_resolution_clock::now();
    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(milliseconds(duration_ms));
    stop.store(true, std::memory_order_release);

    for (auto& th : threads) th.join();
    monitor.join();
    auto t1 = high_resolution_clock::now();

    BenchResult r;
    r.peak_live = peak_live.load();
    r.peak_pending = peak_pending.load();

    int64_t total = 0;
    std::vector<uint64_t> all_lat;
    for (int t = 0; t < num_threads; t++) {
        total += ops_count[t];
        all_lat.insert(all_lat.end(),
                       latencies[t].begin(), latencies[t].end());
    }
    r.total_ops = total;

    double elapsed_s = duration_cast<microseconds>(t1 - t0).count() / 1e6;
    r.throughput_mops = total / elapsed_s / 1e6;

    if (!all_lat.empty()) {
        std::sort(all_lat.begin(), all_lat.end());
        double sum = 0;
        for (auto x : all_lat) sum += x;
        r.avg_latency_ns = sum / all_lat.size();
        r.p50_ns  = all_lat[all_lat.size() *  50 / 100];
        r.p99_ns  = all_lat[all_lat.size() *  99 / 100];
        r.p999_ns = all_lat[all_lat.size() * 999 / 1000];
    }

    return r;
}

void print_csv_header() {
    std::cout << "variant,threads,push_ratio,duration_ms,prefill,"
              << "throughput_mops,total_ops,"
              << "avg_lat_ns,p50_ns,p99_ns,p999_ns,"
              << "peak_live,peak_pending\n";
}

void print_row(const char* variant, int nt, double pr, int dur, int pre,
               const BenchResult& r) {
    std::cout << variant << ","
              << nt << ","
              << pr << ","
              << dur << ","
              << pre << ","
              << std::fixed << std::setprecision(4) << r.throughput_mops << ","
              << r.total_ops << ","
              << std::setprecision(1) << r.avg_latency_ns << ","
              << r.p50_ns << ","
              << r.p99_ns << ","
              << r.p999_ns << ","
              << r.peak_live << ","
              << r.peak_pending << "\n";
    std::cout.flush();
}

int main(int argc, char** argv) {
    std::string variant   = argc > 1 ? argv[1] : "all";
    int    num_threads    = argc > 2 ? std::atoi(argv[2]) : 4;
    double push_ratio     = argc > 3 ? std::atof(argv[3]) : 0.5;
    int    duration_ms    = argc > 4 ? std::atoi(argv[4]) : 2000;
    int    prefill        = argc > 5 ? std::atoi(argv[5]) : 1000;
    std::string mode      = argc > 6 ? argv[6] : "csv";

    if (mode == "header") {
        print_csv_header();
        return 0;
    }

    bool sample_latency = true;

    auto run_one = [&](const std::string& name, auto&& fn) {
        if (variant != "all" && variant != name) return;
        auto r = fn();
        print_row(name.c_str(), num_threads, push_ratio,
                  duration_ms, prefill, r);
    };

    run_one("Leak", [&] {
        return run_bench<lf::StackLeak<int>>(num_threads, push_ratio,
                                              duration_ms, prefill, sample_latency);
    });
    run_one("RC", [&] {
        return run_bench<lf::StackRC<int>>(num_threads, push_ratio,
                                            duration_ms, prefill, sample_latency);
    });
    run_one("HP", [&] {
        return run_bench<lf::StackHP<int>>(num_threads, push_ratio,
                                            duration_ms, prefill, sample_latency);
    });
    run_one("EBR", [&] {
        return run_bench<lf::StackEBR<int>>(num_threads, push_ratio,
                                             duration_ms, prefill, sample_latency);
    });

    return 0;
}
