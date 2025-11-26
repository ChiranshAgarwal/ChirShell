#include "parser.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace parser {
namespace {

void flush_token(std::string& token, Command& cmd) {
    if (!token.empty()) {
        cmd.args.push_back(token);
        token.clear();
    }
}

void flush_command(Command& cmd, ParseResult& result) {
    if (!cmd.args.empty()) {
        result.pipeline.push_back(cmd);
    }
    cmd = Command{};
}

} // namespace

ParseResult parse_line(const std::string& line) {
    ParseResult result;
    Command current;
    std::string token;
    bool in_single = false;
    bool in_double = false;
    bool escape = false;

    for (const char ch : line) {
        if (escape) {
            token.push_back(ch);
            escape = false;
            continue;
        }

        if (ch == '\\') {
            escape = true;
            continue;
        }

        if (ch == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }

        if (ch == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }

        if (!in_single && !in_double) {
            if (ch == '|') {
                flush_token(token, current);
                flush_command(current, result);
                continue;
            }

            if (ch == '&') {
                flush_token(token, current);
                flush_command(current, result);
                result.background = true;
                continue;
            }

            if (std::isspace(static_cast<unsigned char>(ch))) {
                flush_token(token, current);
                continue;
            }
        }

        token.push_back(ch);
    }

    if (escape) {
        token.push_back('\\');
    }

    flush_token(token, current);
    flush_command(current, result);

    // Remove any accidentally empty commands.
    result.pipeline.erase(
        std::remove_if(result.pipeline.begin(),
                       result.pipeline.end(),
                       [](const Command& cmd) { return cmd.args.empty(); }),
        result.pipeline.end());

    return result;
}

} // namespace parser



