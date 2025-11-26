/**
 * @file jobs.hpp
 * @brief Background job tracking utilities.
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <sys/types.h>

namespace jobs {

struct Job {
    int id = 0;
    pid_t pgid = 0;
    std::string command;
    bool running = true;
    bool background = true;
    int processes = 0;
};

void initialize(pid_t shell_pgid);
void shutdown();

int add_job(pid_t pgid, const std::string& command, bool background, int process_count);
void mark_job_finished(pid_t pgid, int status);
void list_jobs();
std::optional<Job> get_job(int id);
bool bring_job_foreground(int id);
bool send_job_background(int id);
bool kill_job(int id, int signal);
void notify_sigchld();

} // namespace jobs

