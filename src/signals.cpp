#include "signals.hpp"

#include "jobs.hpp"

#include <csignal>
#include <iostream>

namespace signals {
namespace {

void sigint_handler(int) {
    std::cout << '\n';
    std::cout.flush();
}

void sigchld_handler(int) {
    jobs::notify_sigchld();
}

void install_handler(int sig, void (*fn)(int)) {
    struct sigaction action {};
    action.sa_handler = fn;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(sig, &action, nullptr);
}

} // namespace

void install_handlers() {
    install_handler(SIGINT, sigint_handler);
    install_handler(SIGCHLD, sigchld_handler);
    install_handler(SIGTSTP, SIG_IGN);
}

void ignore_interactive_signals() {
    install_handler(SIGINT, SIG_IGN);
    install_handler(SIGQUIT, SIG_IGN);
}

} // namespace signals

