#include "pipeline.hpp"

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <stop_token>
#include <thread>

bool translate_segments_parallel(
    const std::vector<Segment>& segments,
    const Translator& prototype,
    std::size_t workers,
    std::vector<std::string>& out_translations,
    TranslationStats& out_stats,
    std::string& error,
    const std::function<void(std::size_t, std::size_t)>& progress_callback
) {
    out_stats = TranslationStats{};
    out_stats.segments_total = segments.size();
    out_translations.clear();

    if (segments.empty()) {
        return true;
    }

    if (workers == 0) {
        workers = 1;
    }

    const std::size_t workers_used = std::min(workers, segments.size());
    out_stats.workers_used = workers_used;

    out_translations.resize(segments.size());

    std::atomic<std::size_t> next_index{0};
    std::atomic<std::size_t> completed{0};
    std::atomic<bool> failed{false};
    std::mutex error_mutex;
    std::stop_source stop_source;

    const auto started = std::chrono::steady_clock::now();

    std::jthread reporter;
    if (progress_callback) {
        reporter = std::jthread([&](std::stop_token stop_token) {
            std::size_t last_completed = std::numeric_limits<std::size_t>::max();
            while (!stop_token.stop_requested()) {
                const std::size_t done = completed.load(std::memory_order_relaxed);
                if (done != last_completed) {
                    progress_callback(done, segments.size());
                    last_completed = done;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            const std::size_t final_done = completed.load(std::memory_order_relaxed);
            if (final_done != last_completed) {
                progress_callback(final_done, segments.size());
            }
        });
    }

    std::vector<std::jthread> pool;
    pool.reserve(workers_used);

    auto worker_fn = [&](std::stop_token stop_token) {
        auto local_translator = prototype.clone();

        while (!stop_token.stop_requested() && !failed.load(std::memory_order_relaxed)) {
            const std::size_t index = next_index.fetch_add(1, std::memory_order_relaxed);
            if (index >= segments.size()) {
                return;
            }

            try {
                out_translations[index] = local_translator->translate(segments[index]);
                completed.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& ex) {
                std::lock_guard<std::mutex> lock(error_mutex);
                if (!failed.exchange(true, std::memory_order_relaxed)) {
                    error = ex.what();
                }
                stop_source.request_stop();
                return;
            } catch (...) {
                std::lock_guard<std::mutex> lock(error_mutex);
                if (!failed.exchange(true, std::memory_order_relaxed)) {
                    error = "Unknown translation error";
                }
                stop_source.request_stop();
                return;
            }
        }
    };

    for (std::size_t i = 0; i < workers_used; ++i) {
        pool.emplace_back(worker_fn, stop_source.get_token());
    }

    for (auto& thread : pool) {
        thread.join();
    }

    if (failed.load(std::memory_order_relaxed)) {
        return false;
    }

    const auto ended = std::chrono::steady_clock::now();
    out_stats.wall_time = std::chrono::duration_cast<std::chrono::milliseconds>(ended - started);

    const double wall_seconds = static_cast<double>(out_stats.wall_time.count()) / 1000.0;
    if (wall_seconds > 0.0) {
        out_stats.segments_per_second = static_cast<double>(segments.size()) / wall_seconds;
    }

    if (!segments.empty()) {
        out_stats.ms_per_segment = static_cast<double>(out_stats.wall_time.count()) /
            static_cast<double>(segments.size());
    }

    return true;
}
