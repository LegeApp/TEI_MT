#pragma once

#include <string>

namespace tei_mt_gui {

enum class EventType {
    ScanStarted,
    ScanFinished,
    FileStarted,
    FileDone,
    Log,
    Error,
    Finished
};

struct ProgressEvent {
    EventType type = EventType::Log;
    std::string message;
    std::string path;
    int total_files = 0;
    int done_files = 0;
    int total_segments = 0;
    int done_segments = 0;
    bool success = false;
};

}  // namespace tei_mt_gui
