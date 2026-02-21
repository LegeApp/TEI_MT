#include "job_controller.hpp"

namespace tei_mt_gui {

JobController::JobController() = default;

JobController::~JobController() {
    cancel();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool JobController::start(const RunConfig& cfg) {
    if (running_.load(std::memory_order_relaxed)) {
        return false;
    }

    control_.cancel_requested.store(false, std::memory_order_relaxed);
    control_.pause_requested.store(false, std::memory_order_relaxed);
    paused_.store(false, std::memory_order_relaxed);
    running_.store(true, std::memory_order_relaxed);

    worker_ = std::thread(&JobController::run_worker, this, cfg);
    return true;
}

void JobController::pause() {
    if (!running_.load(std::memory_order_relaxed)) {
        return;
    }
    control_.pause_requested.store(true, std::memory_order_relaxed);
    paused_.store(true, std::memory_order_relaxed);
}

void JobController::resume() {
    if (!running_.load(std::memory_order_relaxed)) {
        return;
    }
    control_.pause_requested.store(false, std::memory_order_relaxed);
    paused_.store(false, std::memory_order_relaxed);
}

void JobController::cancel() {
    control_.cancel_requested.store(true, std::memory_order_relaxed);
    control_.pause_requested.store(false, std::memory_order_relaxed);
    paused_.store(false, std::memory_order_relaxed);
}

bool JobController::is_running() const {
    return running_.load(std::memory_order_relaxed);
}

bool JobController::is_paused() const {
    return paused_.load(std::memory_order_relaxed);
}

std::vector<ProgressEvent> JobController::poll_events() {
    auto events = events_.pop_all();
    bool saw_finished = false;
    for (const auto& e : events) {
        if (e.type == EventType::Finished) {
            saw_finished = true;
        }
    }

    if (saw_finished) {
        if (worker_.joinable()) {
            worker_.join();
        }
        running_.store(false, std::memory_order_relaxed);
        paused_.store(false, std::memory_order_relaxed);
    }

    return events;
}

void JobController::run_worker(RunConfig cfg) {
    auto cb = [&](const ProgressEvent& e) { events_.push(e); };
    run_translation_process(cfg, control_, cb);
}

}  // namespace tei_mt_gui
