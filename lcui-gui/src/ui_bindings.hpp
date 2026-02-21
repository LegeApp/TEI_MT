#pragma once

#include "job_controller.hpp"

#include <LCUI.h>

#include <string>

namespace tei_mt_gui {

struct AppState {
    bool running = false;
    bool paused = false;

    int total_files = 0;
    int done_files = 0;
    std::string current_file;
    std::string status = "Idle";
    std::string logs;
};

struct UiContext {
    JobController controller;
    AppState state;
    int poll_timer_id = 0;
};

void bind_ui(UiContext& ctx);

}  // namespace tei_mt_gui
