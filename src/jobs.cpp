#include "jobs.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#include <termios.h>

namespace jobs {
namespace {

std::vector<Job> job_table;
std::mutex job_mutex;
std::atomic<bool> running{false};
std::thread reaper;
pid_t shell_pgid = 0;
const int shell_terminal = STDIN_FILENO;
bool terminal_attached = false;
int next_job_id = 1;
volatile sig_atomic_t sigchld_flag = 0;

Job* find_job_locked(int id) {
    for (auto& job : job_table) {
        if (job.id == id) {
            return &job;
        }
    }
    return nullptr;
}

Job* find_job_by_pgid_locked(pid_t pgid) {
    for (auto& job : job_table) {
        if (job.pgid == pgid) {
            return &job;
        }
    }
    return nullptr;
}

void notify_completion(const Job& job, int status) {
    if (!job.background) {
        return;
    }
    if (WIFEXITED(status)) {
        std::cout << "\n[" << job.id << "] Done " << job.command << '\n';
    } else if (WIFSIGNALED(status)) {
        std::cout << "\n[" << job.id << "] Killed (" << WTERMSIG(status) << ") "
                  << job.command << '\n';
    } else {
        std::cout << "\n[" << job.id << "] Finished " << job.command << '\n';
    }
    std::cout.flush();
}

void handle_child_exit(pid_t pid, int status) {
    pid_t pgid = ::getpgid(pid);
    if (pgid < 0) {
        pgid = pid;
    }
    std::lock_guard<std::mutex> lock(job_mutex);
    if (Job* job = find_job_by_pgid_locked(pgid)) {
        if (job->processes > 0) {
            --job->processes;
        }
        if (job->processes <= 0) {
            job->running = false;
            notify_completion(*job, status);
            remove_finished_locked();
        }
    }
}

void reap_once() {
    int status = 0;
    pid_t pid = 0;
    while ((pid = ::waitpid(-1, &status, WNOHANG)) > 0) {
        handle_child_exit(pid, status);
    }
}

void reaper_loop() {
    while (running.load()) {
        if (sigchld_flag) {
            sigchld_flag = 0;
            reap_once();
        } else {
            reap_once();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    reap_once();
}

void remove_finished_locked() {
    job_table.erase(
        std::remove_if(job_table.begin(), job_table.end(),
                       [](const Job& job) { return !job.running; }),
        job_table.end());
}

void give_terminal_to(pid_t pgid) {
    if (!terminal_attached) {
        return;
    }
    tcsetpgrp(shell_terminal, pgid);
}

void reclaim_terminal() {
    if (!terminal_attached) {
        return;
    }
    tcsetpgrp(shell_terminal, shell_pgid);
}

} // namespace

void initialize(pid_t pgid) {
    shell_pgid = pgid;
    terminal_attached = ::isatty(shell_terminal);
    running = true;
    reaper = std::thread(reaper_loop);
}

void shutdown() {
    running = false;
    if (reaper.joinable()) {
        reaper.join();
    }
}

int add_job(pid_t pgid, const std::string& command, bool background, int process_count) {
    std::lock_guard<std::mutex> lock(job_mutex);
    Job job;
    job.id = next_job_id++;
    job.pgid = pgid;
    job.command = command;
    job.running = true;
    job.background = background;
    job.processes = process_count;
    job_table.push_back(job);
    return job.id;
}

void mark_job_finished(pid_t pgid, int status) {
    std::lock_guard<std::mutex> lock(job_mutex);
    if (Job* job = find_job_by_pgid_locked(pgid)) {
        job->running = false;
        job->processes = 0;
        notify_completion(*job, status);
    }
    remove_finished_locked();
}

void list_jobs() {
    std::lock_guard<std::mutex> lock(job_mutex);
    for (const auto& job : job_table) {
        std::cout << '[' << job.id << "] "
                  << (job.running ? "Running " : "Stopped ") << job.command;
        if (job.background) {
            std::cout << " &";
        }
        std::cout << '\n';
    }
}

std::optional<Job> get_job(int id) {
    std::lock_guard<std::mutex> lock(job_mutex);
    if (Job* job = find_job_locked(id)) {
        return *job;
    }
    return std::nullopt;
}

bool bring_job_foreground(int id) {
    std::unique_lock<std::mutex> lock(job_mutex);
    Job* job = find_job_locked(id);
    if (!job) {
        return false;
    }
    job->background = false;
    job->running = true;
    if (job->processes <= 0) {
        job->processes = 1;
    }
    const pid_t pgid = job->pgid;
    lock.unlock();

    give_terminal_to(pgid);
    if (::kill(-pgid, SIGCONT) < 0) {
        perror("fg");
        reclaim_terminal();
        return false;
    }

    int status = 0;
    pid_t waited = 0;
    do {
        waited = ::waitpid(-pgid, &status, WUNTRACED);
    } while (waited > 0 && !WIFEXITED(status) && !WIFSIGNALED(status));

    reclaim_terminal();
    mark_job_finished(pgid, status);
    return true;
}

bool send_job_background(int id) {
    std::lock_guard<std::mutex> lock(job_mutex);
    Job* job = find_job_locked(id);
    if (!job) {
        return false;
    }
    if (::kill(-job->pgid, SIGCONT) < 0) {
        perror("bg");
        return false;
    }
    job->background = true;
    job->running = true;
    if (job->processes <= 0) {
        job->processes = 1;
    }
    std::cout << '[' << job->id << "] " << job->command << " &\n";
    return true;
}

bool kill_job(int id, int signal) {
    std::lock_guard<std::mutex> lock(job_mutex);
    Job* job = find_job_locked(id);
    if (!job) {
        return false;
    }
    if (::kill(-job->pgid, signal) != 0) {
        perror("kill");
        return false;
    }
    return true;
}

void notify_sigchld() {
    sigchld_flag = 1;
}

} // namespace jobs

