#include "ui_bindings.hpp"

#include <css/properties.h>
#include <ptk.h>

#include <algorithm>
#include <codecvt>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace tei_mt_gui {
namespace {

UiContext* g_ctx = nullptr;

std::string wstring_to_utf8(const std::wstring& ws) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    return conv.to_bytes(ws);
}

std::string read_input_text(ui_widget_t* w) {
    if (!w) {
        return {};
    }
    const size_t len = ui_textinput_get_text_length(w);
    std::vector<wchar_t> buffer(len + 1, 0);
    ui_textinput_get_text_w(w, 0, len, buffer.data());
    return wstring_to_utf8(std::wstring(buffer.data()));
}

int read_input_int(const char* id, int fallback) {
    ui_widget_t* w = ui_get_widget(id);
    if (!w) {
        return fallback;
    }
    try {
        return std::stoi(read_input_text(w));
    } catch (...) {
        return fallback;
    }
}

bool read_input_bool01(const char* id, bool fallback) {
    return read_input_int(id, fallback ? 1 : 0) != 0;
}

void set_text(const char* id, const std::string& text) {
    ui_widget_t* w = ui_get_widget(id);
    if (w) {
        ui_text_set_content(w, text.c_str());
    }
}

void set_buttons_state(bool running, bool paused) {
    ui_widget_t* btn_start = ui_get_widget("btn_start");
    ui_widget_t* btn_pause = ui_get_widget("btn_pause");
    ui_widget_t* btn_resume = ui_get_widget("btn_resume");
    ui_widget_t* btn_cancel = ui_get_widget("btn_cancel");

    if (btn_start) {
        ui_widget_set_disabled(btn_start, running);
    }
    if (btn_pause) {
        ui_widget_set_disabled(btn_pause, !running || paused);
    }
    if (btn_resume) {
        ui_widget_set_disabled(btn_resume, !running || !paused);
    }
    if (btn_cancel) {
        ui_widget_set_disabled(btn_cancel, !running);
    }
}

void update_progress_bar(int done, int total) {
    const int pct = total > 0 ? static_cast<int>((100.0 * static_cast<double>(done)) / static_cast<double>(total)) : 0;
    std::ostringstream ss;
    ss << std::clamp(pct, 0, 100) << "%";
    set_text("progress_text", ss.str());

    ui_widget_t* fill = ui_get_widget("progress_fill");
    if (fill) {
        ui_widget_set_style_string(fill, css_prop_width, ss.str().c_str());
        ui_widget_request_update_style(fill);
    }
}

void refresh_ui(UiContext& ctx) {
    std::ostringstream files;
    const int remaining = std::max(0, ctx.state.total_files - ctx.state.done_files);
    files << "Files: " << ctx.state.done_files << "/" << ctx.state.total_files << " (remaining " << remaining << ")";

    set_text("status_line", ctx.state.status);
    set_text("file_counter", files.str());
    set_text("log_text", ctx.state.logs);
    update_progress_bar(ctx.state.done_files, ctx.state.total_files);
    set_buttons_state(ctx.state.running, ctx.state.paused);
}

RunConfig collect_run_config() {
    RunConfig cfg;
    cfg.tei_mt_path = read_input_text(ui_get_widget("tei_mt_path"));
    cfg.input_path = read_input_text(ui_get_widget("input_path"));
    cfg.output_path = read_input_text(ui_get_widget("output_path"));
    cfg.model_path = read_input_text(ui_get_widget("model_path"));
    cfg.workers = read_input_int("workers", 2);
    cfg.threads = read_input_int("threads", 8);
    cfg.ctx = read_input_int("ctx", 2048);
    cfg.max_tokens = read_input_int("max_tokens", 192);
    cfg.n_gpu_layers = read_input_int("n_gpu_layers", -1);
    cfg.emit_markdown = read_input_bool01("emit_markdown", false);
    cfg.no_resume = read_input_bool01("no_resume", false);
    cfg.overwrite_existing = read_input_bool01("overwrite_existing", false);
    cfg.no_progress = read_input_bool01("no_progress", true);
    return cfg;
}

void append_log(UiContext& ctx, const std::string& line) {
    if (!ctx.state.logs.empty()) {
        ctx.state.logs.push_back('\n');
    }
    ctx.state.logs += line;

    constexpr size_t kMaxChars = 32000;
    if (ctx.state.logs.size() > kMaxChars) {
        ctx.state.logs.erase(0, ctx.state.logs.size() - kMaxChars);
    }
}

void on_start_click(ui_widget_t* /*self*/, ui_event_t* /*e*/, void* /*arg*/) {
    if (!g_ctx || g_ctx->controller.is_running()) {
        return;
    }

    RunConfig cfg = collect_run_config();
    g_ctx->state.logs.clear();
    g_ctx->state.status = "Starting...";
    g_ctx->state.done_files = 0;
    g_ctx->state.total_files = 0;

    if (!g_ctx->controller.start(cfg)) {
        g_ctx->state.status = "Failed to start";
    } else {
        g_ctx->state.running = true;
        g_ctx->state.paused = false;
    }
    refresh_ui(*g_ctx);
}

void on_pause_click(ui_widget_t* /*self*/, ui_event_t* /*e*/, void* /*arg*/) {
    if (!g_ctx) {
        return;
    }
    g_ctx->controller.pause();
    g_ctx->state.paused = true;
    g_ctx->state.status = "Paused";
    refresh_ui(*g_ctx);
}

void on_resume_click(ui_widget_t* /*self*/, ui_event_t* /*e*/, void* /*arg*/) {
    if (!g_ctx) {
        return;
    }
    g_ctx->controller.resume();
    g_ctx->state.paused = false;
    g_ctx->state.status = "Running";
    refresh_ui(*g_ctx);
}

void on_cancel_click(ui_widget_t* /*self*/, ui_event_t* /*e*/, void* /*arg*/) {
    if (!g_ctx) {
        return;
    }
    g_ctx->controller.cancel();
    g_ctx->state.status = "Cancel requested...";
    refresh_ui(*g_ctx);
}

void on_timer_tick(void* arg) {
    auto* ctx = static_cast<UiContext*>(arg);
    if (!ctx) {
        return;
    }

    auto events = ctx->controller.poll_events();
    if (events.empty()) {
        return;
    }

    for (const auto& e : events) {
        switch (e.type) {
        case EventType::ScanStarted:
            ctx->state.status = "Scanning input...";
            break;
        case EventType::ScanFinished:
            ctx->state.total_files = e.total_files;
            ctx->state.done_files = 0;
            ctx->state.status = "Translating...";
            break;
        case EventType::FileDone:
            ctx->state.done_files = e.done_files;
            ctx->state.current_file = e.path;
            ctx->state.status = "Processing: " + e.path;
            break;
        case EventType::Error:
            append_log(*ctx, e.message);
            ctx->state.status = "Error";
            break;
        case EventType::Log:
            append_log(*ctx, e.message);
            break;
        case EventType::Finished:
            ctx->state.running = false;
            ctx->state.paused = false;
            ctx->state.status = e.success ? "Completed" : "Stopped with errors";
            if (e.total_files > 0) {
                ctx->state.total_files = e.total_files;
            }
            if (e.done_files > 0) {
                ctx->state.done_files = e.done_files;
            }
            break;
        default:
            break;
        }
    }

    refresh_ui(*ctx);
}

}  // namespace

void bind_ui(UiContext& ctx) {
    g_ctx = &ctx;

    ui_widget_t* log = ui_get_widget("log_text");
    if (log) {
        ui_text_set_multiline(log, true);
    }

    ui_widget_on(ui_get_widget("btn_start"), "click", on_start_click, nullptr);
    ui_widget_on(ui_get_widget("btn_pause"), "click", on_pause_click, nullptr);
    ui_widget_on(ui_get_widget("btn_resume"), "click", on_resume_click, nullptr);
    ui_widget_on(ui_get_widget("btn_cancel"), "click", on_cancel_click, nullptr);

    ctx.poll_timer_id = ptk_set_interval(100, on_timer_tick, &ctx);
    refresh_ui(ctx);
}

}  // namespace tei_mt_gui
