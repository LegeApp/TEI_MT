#include "event_queue.hpp"

namespace tei_mt_gui {

void EventQueue::push(const ProgressEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(event);
}

std::vector<ProgressEvent> EventQueue::pop_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ProgressEvent> out;
    out.swap(events_);
    return out;
}

}  // namespace tei_mt_gui
