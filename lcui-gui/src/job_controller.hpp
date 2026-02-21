#pragma once

#include "core_api.hpp"
#include "event_queue.hpp"

#include <atomic>
#include <thread>

namespace tei_mt_gui {

class JobController {
public:
    JobController();
    ~JobController();

    bool start(const RunConfig& cfg);
    void pause();
    void resume();
    void cancel();

    bool is_running() const;
    bool is_paused() const;

    std::vector<ProgressEvent> poll_events();

private:
    void run_worker(RunConfig cfg);

    EventQueue events_;
    RunControl control_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
};

}  // namespace tei_mt_gui
