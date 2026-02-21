#pragma once

#include "progress_event.hpp"

#include <mutex>
#include <vector>

namespace tei_mt_gui {

class EventQueue {
public:
    void push(const ProgressEvent& event);
    std::vector<ProgressEvent> pop_all();

private:
    std::mutex mutex_;
    std::vector<ProgressEvent> events_;
};

}  // namespace tei_mt_gui
