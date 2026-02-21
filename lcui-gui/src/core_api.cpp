#include "core_api.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace tei_mt_gui {
namespace {

bool has_xml_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".xml";
}

int count_input_xml(const std::string& input_path) {
    std::error_code ec;
    const std::filesystem::path input(input_path);
    if (!std::filesystem::exists(input, ec)) {
        return 0;
    }
    if (std::filesystem::is_regular_file(input, ec)) {
        return has_xml_extension(input) ? 1 : 0;
    }

    int count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(input)) {
        if (entry.is_regular_file() && has_xml_extension(entry.path())) {
            ++count;
        }
    }
    return count;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

void emit_log(const ProgressCallback& callback, const std::string& line) {
    if (!callback) {
        return;
    }
    ProgressEvent e;
    e.type = EventType::Log;
    e.message = line;
    callback(e);
}

void parse_cli_line(const std::string& line, int total_files, int& done_files, const ProgressCallback& callback) {
    if (callback) {
        emit_log(callback, line);
    }

    if (starts_with(line, "[ok] ") || starts_with(line, "[skip] ")) {
        ++done_files;
        ProgressEvent e;
        e.type = EventType::FileDone;
        e.message = line;
        e.total_files = total_files;
        e.done_files = done_files;

        const auto first_space = line.find(' ');
        if (first_space != std::string::npos) {
            const auto second_space = line.find(' ', first_space + 1);
            if (second_space != std::string::npos) {
                e.path = line.substr(first_space + 1, second_space - first_space - 1);
            }
        }
        callback(e);
    } else if (starts_with(line, "[error] ")) {
        ProgressEvent e;
        e.type = EventType::Error;
        e.message = line;
        callback(e);
    }
}

}  // namespace

bool run_translation_process(const RunConfig& cfg, RunControl& control, const ProgressCallback& callback) {
    ProgressEvent scan_start;
    scan_start.type = EventType::ScanStarted;
    callback(scan_start);

    const int total_files = count_input_xml(cfg.input_path);
    ProgressEvent scan_done;
    scan_done.type = EventType::ScanFinished;
    scan_done.total_files = total_files;
    callback(scan_done);

#if !defined(__linux__)
    ProgressEvent err;
    err.type = EventType::Error;
    err.message = "LCUI GUI wrapper currently implements process control on Linux only.";
    callback(err);

    ProgressEvent finished;
    finished.type = EventType::Finished;
    finished.success = false;
    callback(finished);
    return false;
#else
    if (cfg.tei_mt_path.empty()) {
        ProgressEvent err;
        err.type = EventType::Error;
        err.message = "tei_mt path is empty";
        callback(err);
        return false;
    }

    std::vector<std::string> args;
    args.push_back(cfg.tei_mt_path);
    args.push_back("--input");
    args.push_back(cfg.input_path);
    args.push_back("--output");
    args.push_back(cfg.output_path);
    args.push_back("--model");
    args.push_back(cfg.model_path);
    args.push_back("--workers");
    args.push_back(std::to_string(cfg.workers));
    args.push_back("--threads");
    args.push_back(std::to_string(cfg.threads));
    args.push_back("--ctx");
    args.push_back(std::to_string(cfg.ctx));
    args.push_back("--max-tokens");
    args.push_back(std::to_string(cfg.max_tokens));
    args.push_back("--n-gpu-layers");
    args.push_back(std::to_string(cfg.n_gpu_layers));

    if (cfg.emit_markdown) {
        args.push_back("--emit-markdown");
    }
    if (cfg.no_resume) {
        args.push_back("--no-resume");
    }
    if (cfg.overwrite_existing) {
        args.push_back("--overwrite-existing-translations");
    }
    if (cfg.no_progress) {
        args.push_back("--no-progress");
    }

    std::vector<char*> cargs;
    cargs.reserve(args.size() + 1);
    for (std::string& s : args) {
        cargs.push_back(s.data());
    }
    cargs.push_back(nullptr);

    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) {
        ProgressEvent err;
        err.type = EventType::Error;
        err.message = std::string("pipe() failed: ") + std::strerror(errno);
        callback(err);
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        ProgressEvent err;
        err.type = EventType::Error;
        err.message = std::string("fork() failed: ") + std::strerror(errno);
        callback(err);
        return false;
    }

    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(cargs[0], cargs.data());
        _exit(127);
    }

    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    bool child_paused = false;
    bool child_exited = false;
    int wait_status = 0;
    int done_files = 0;
    std::string buf;
    char chunk[4096];

    while (!child_exited) {
        if (control.cancel_requested.load(std::memory_order_relaxed)) {
            kill(pid, SIGTERM);
        }

        const bool should_pause = control.pause_requested.load(std::memory_order_relaxed);
        if (should_pause && !child_paused) {
            kill(pid, SIGSTOP);
            child_paused = true;
        } else if (!should_pause && child_paused) {
            kill(pid, SIGCONT);
            child_paused = false;
        }

        const pid_t w = waitpid(pid, &wait_status, WNOHANG);
        if (w == pid) {
            child_exited = true;
        }

        const ssize_t n = read(pipefd[0], chunk, sizeof(chunk));
        if (n > 0) {
            buf.append(chunk, static_cast<size_t>(n));
            size_t pos = 0;
            while (true) {
                const size_t nl = buf.find_first_of("\n\r", pos);
                if (nl == std::string::npos) {
                    buf.erase(0, pos);
                    break;
                }
                std::string line = buf.substr(pos, nl - pos);
                if (!line.empty()) {
                    parse_cli_line(line, total_files, done_files, callback);
                }
                size_t next = nl + 1;
                while (next < buf.size() && (buf[next] == '\n' || buf[next] == '\r')) {
                    ++next;
                }
                pos = next;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
    }

    close(pipefd[0]);

    if (!buf.empty()) {
        parse_cli_line(buf, total_files, done_files, callback);
    }

    const bool success = WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0 &&
        !control.cancel_requested.load(std::memory_order_relaxed);

    ProgressEvent finished;
    finished.type = EventType::Finished;
    finished.success = success;
    finished.total_files = total_files;
    finished.done_files = done_files;
    callback(finished);
    return success;
#endif
}

}  // namespace tei_mt_gui
