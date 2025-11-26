#include "builtins.hpp"

#include "jobs.hpp"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unistd.h>

extern char** environ;

namespace builtins {
namespace {

std::string_view command_name(const parser::Command& cmd) {
    if (cmd.args.empty()) {
        return {};
    }
    return cmd.args.front();
}

int builtin_cd(const parser::Command& cmd) {
    std::string target;
    if (cmd.args.size() < 2) {
        const char* home = std::getenv("HOME");
        if (!home) {
            std::cerr << "chirshell: cd: HOME not set\n";
            return 1;
        }
        target = home;
    } else {
        target = cmd.args[1];
    }

    if (chdir(target.c_str()) != 0) {
        perror("cd");
        return 1;
    }
    return 0;
}

int builtin_pwd() {
    std::error_code ec;
    const auto path = std::filesystem::current_path(ec);
    if (ec) {
        std::cerr << "pwd: " << ec.message() << '\n';
        return 1;
    }
    std::cout << path.string() << '\n';
    return 0;
}

int builtin_echo(const parser::Command& cmd) {
    for (std::size_t i = 1; i < cmd.args.size(); ++i) {
        if (i > 1) {
            std::cout << ' ';
        }
        std::cout << cmd.args[i];
    }
    std::cout << '\n';
    return 0;
}

int builtin_env() {
    for (char** env = environ; env && *env; ++env) {
        std::cout << *env << '\n';
    }
    return 0;
}

int builtin_export(const parser::Command& cmd) {
    if (cmd.args.size() < 2) {
        std::cerr << "export: usage: export VAR=value\n";
        return 1;
    }

    const std::string_view assignment = cmd.args[1];
    const auto pos = assignment.find('=');
    if (pos == std::string_view::npos || pos == 0) {
        std::cerr << "export: invalid assignment\n";
        return 1;
    }

    const std::string key(assignment.substr(0, pos));
    const std::string value(assignment.substr(pos + 1));
    if (setenv(key.c_str(), value.c_str(), 1) != 0) {
        perror("export");
        return 1;
    }
    return 0;
}

int builtin_unset(const parser::Command& cmd) {
    if (cmd.args.size() < 2) {
        std::cerr << "unset: missing variable name\n";
        return 1;
    }

    if (unsetenv(cmd.args[1].c_str()) != 0) {
        perror("unset");
        return 1;
    }
    return 0;
}

int builtin_history(History& history) {
    for (std::size_t i = 0; i < history.size(); ++i) {
        std::cout << (i + 1) << ' ' << history[i] << '\n';
    }
    return 0;
}

int parse_job_id(const std::string& token) {
    std::string_view sv = token;
    if (!sv.empty() && sv.front() == '%') {
        sv.remove_prefix(1);
    }
    if (sv.empty()) {
        return -1;
    }
    try {
        return std::stoi(std::string(sv));
    } catch (...) {
        return -1;
    }
}

int builtin_jobs() {
    jobs::list_jobs();
    return 0;
}

int builtin_fg(const parser::Command& cmd) {
    if (cmd.args.size() < 2) {
        std::cerr << "fg: usage: fg %job\n";
        return 1;
    }
    const int job_id = parse_job_id(cmd.args[1]);
    if (job_id < 0) {
        std::cerr << "fg: invalid job specification\n";
        return 1;
    }
    if (!jobs::bring_job_foreground(job_id)) {
        std::cerr << "fg: job not found: %" << job_id << '\n';
        return 1;
    }
    return 0;
}

int builtin_bg(const parser::Command& cmd) {
    if (cmd.args.size() < 2) {
        std::cerr << "bg: usage: bg %job\n";
        return 1;
    }
    const int job_id = parse_job_id(cmd.args[1]);
    if (job_id < 0) {
        std::cerr << "bg: invalid job specification\n";
        return 1;
    }
    if (!jobs::send_job_background(job_id)) {
        std::cerr << "bg: job not found: %" << job_id << '\n';
        return 1;
    }
    return 0;
}

int builtin_kill(const parser::Command& cmd) {
    if (cmd.args.size() < 2) {
        std::cerr << "kill: usage: kill %job|PID\n";
        return 1;
    }

    const int job_id = parse_job_id(cmd.args[1]);
    if (job_id > 0) {
        if (!jobs::kill_job(job_id, SIGTERM)) {
            std::cerr << "kill: job not found: %" << job_id << '\n';
            return 1;
        }
        return 0;
    }

    try {
        pid_t pid = static_cast<pid_t>(std::stoi(cmd.args[1]));
        if (::kill(pid, SIGTERM) != 0) {
            perror("kill");
            return 1;
        }
        return 0;
    } catch (...) {
        std::cerr << "kill: invalid pid\n";
        return 1;
    }
}

} // namespace

bool is_builtin(const parser::Command& cmd) {
    const auto name = command_name(cmd);
    if (name.empty()) {
        return false;
    }
    static const std::vector<std::string_view> builtins = {
        "cd",    "pwd",  "echo",  "env",  "export",
        "unset", "exit", "history", "jobs", "fg",
        "bg",    "kill",
    };
    return std::find(builtins.begin(), builtins.end(), name) != builtins.end();
}

int run(const parser::Command& cmd, bool& should_exit, History& history) {
    const auto name = command_name(cmd);
    if (name.empty()) {
        return 0;
    }

    if (name == "cd") {
        return builtin_cd(cmd);
    }
    if (name == "pwd") {
        return builtin_pwd();
    }
    if (name == "echo") {
        return builtin_echo(cmd);
    }
    if (name == "env") {
        return builtin_env();
    }
    if (name == "export") {
        return builtin_export(cmd);
    }
    if (name == "unset") {
        return builtin_unset(cmd);
    }
    if (name == "exit") {
        should_exit = true;
        return 0;
    }
    if (name == "history") {
        return builtin_history(history);
    }
    if (name == "jobs") {
        return builtin_jobs();
    }
    if (name == "fg") {
        return builtin_fg(cmd);
    }
    if (name == "bg") {
        return builtin_bg(cmd);
    }
    if (name == "kill") {
        return builtin_kill(cmd);
    }
    return 0;
}

} // namespace builtins

