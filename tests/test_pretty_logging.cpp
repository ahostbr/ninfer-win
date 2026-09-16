#include "product/logging/logging.h"
#include "product/logging/startup_log.h"

#include <spdlog/logger.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>

namespace {

// The CRT spells these with a leading underscore on Windows, _pipe additionally takes a buffer
// size and a mode, and STDERR_FILENO is not defined there. Wrapped rather than #defined, because
// defining `read` and `close` as macros would reach every later declaration in this file.
#if defined(_WIN32)
constexpr int kStderrFd = 2;
int os_pipe(int fds[2]) { return ::_pipe(fds, 65536, _O_BINARY); }
int os_dup(int fd) { return ::_dup(fd); }
int os_dup2(int from, int to) { return ::_dup2(from, to); }
int os_close(int fd) { return ::_close(fd); }
int os_read(int fd, void* buffer, std::size_t bytes) {
    return ::_read(fd, buffer, static_cast<unsigned int>(bytes));
}
#else
constexpr int kStderrFd = STDERR_FILENO;
int os_pipe(int fds[2]) { return ::pipe(fds); }
int os_dup(int fd) { return ::dup(fd); }
int os_dup2(int from, int to) { return ::dup2(from, to); }
int os_close(int fd) { return ::close(fd); }
ssize_t os_read(int fd, void* buffer, std::size_t bytes) { return ::read(fd, buffer, bytes); }
#endif

class StderrCapture {
public:
    StderrCapture() {
        if (os_pipe(pipe_) != 0) { throw std::runtime_error(std::strerror(errno)); }
        saved_ = os_dup(kStderrFd);
        if (saved_ < 0 || os_dup2(pipe_[1], kStderrFd) < 0) {
            throw std::runtime_error(std::strerror(errno));
        }
        os_close(pipe_[1]);
        pipe_[1] = -1;
    }

    ~StderrCapture() {
        if (saved_ >= 0) {
            (void)os_dup2(saved_, kStderrFd);
            os_close(saved_);
        }
        if (pipe_[0] >= 0) { os_close(pipe_[0]); }
    }

    std::string finish() {
        std::fflush(stderr);
        if (os_dup2(saved_, kStderrFd) < 0) { throw std::runtime_error(std::strerror(errno)); }
        os_close(saved_);
        saved_ = -1;

        std::string output;
        std::array<char, 4096> buffer{};
        for (;;) {
            const auto count = os_read(pipe_[0], buffer.data(), buffer.size());
            if (count == 0) { break; }
            if (count < 0) {
                if (errno == EINTR) { continue; }
                throw std::runtime_error(std::strerror(errno));
            }
            output.append(buffer.data(), static_cast<std::size_t>(count));
        }
        os_close(pipe_[0]);
        pipe_[0] = -1;
        return output;
    }

private:
    int pipe_[2]{-1, -1};
    int saved_ = -1;
};

int check(bool condition, const char* message) {
    if (condition) { return 0; }
    std::cerr << message << '\n';
    return 1;
}

std::size_t line_count(std::string_view value) {
    return static_cast<std::size_t>(std::count(value.begin(), value.end(), '\n'));
}

} // namespace

int main() {
    int failures = 0;
    std::string service_output;
    {
        StderrCapture capture;
        {
            ninfer::product::LoggingRuntime logging(
                {.logger_name  = "ninfer-serve",
                 .color        = ninfer::product::LogColorMode::Auto,
                 .presentation = ninfer::product::LogPresentation::Service});
            logging.logger()->info("throughput | sample");
            logging.flush();
        }
        service_output = capture.finish();
    }
    failures += check(
        std::regex_match(
            service_output,
            std::regex(
                R"(^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}  INFO  throughput \| sample\n$)")),
        "service pretty prefix mismatch");
    failures += check(service_output.find("ninfer-serve") == std::string::npos,
                      "service pretty output repeated the executable name");
    failures += check(service_output.find("\x1b[") == std::string::npos,
                      "redirected service output contains ANSI escapes");

    std::string startup_output;
    {
        StderrCapture capture;
        {
            ninfer::product::LoggingRuntime logging(
                {.logger_name  = "ninfer-serve",
                 .color        = ninfer::product::LogColorMode::Never,
                 .presentation = ninfer::product::LogPresentation::Service});
            ninfer::product::StartupLogRenderer startup(logging);
            ninfer::StartupObserver observer = startup.observer();
            observer.callback({.phase  = ninfer::StartupPhase::EngineStartup,
                               .status = ninfer::StartupStatus::Begin});
            observer.callback({.phase  = ninfer::StartupPhase::CudaInitialize,
                               .status = ninfer::StartupStatus::Begin});
            observer.callback({.phase      = ninfer::StartupPhase::CudaInitialize,
                               .status     = ninfer::StartupStatus::Complete,
                               .elapsed_ns = 1'000'000'000});
            observer.callback({.phase         = ninfer::StartupPhase::WeightsMaterialize,
                               .status        = ninfer::StartupStatus::Begin,
                               .progress_unit = ninfer::StartupProgressUnit::Bytes,
                               .total         = 16ULL << 30});
            observer.callback({.phase         = ninfer::StartupPhase::WeightsMaterialize,
                               .status        = ninfer::StartupStatus::Complete,
                               .progress_unit = ninfer::StartupProgressUnit::Bytes,
                               .current       = 16ULL << 30,
                               .total         = 16ULL << 30,
                               .elapsed_ns    = 2'000'000'000});
            observer.callback({.phase      = ninfer::StartupPhase::EngineStartup,
                               .status     = ninfer::StartupStatus::Complete,
                               .elapsed_ns = 3'000'000'000});
            startup.engine_ready({.model_name           = "qwen3.6-27b",
                                  .weight_formats       = {"q4_g64_fp16", "q8_g32_fp16"},
                                  .host_to_device_bytes = 16ULL << 30});
            logging.flush();
        }
        startup_output = capture.finish();
    }
    failures += check(
        line_count(startup_output) == 4 &&
            startup_output.find("starting engine") != std::string::npos &&
            startup_output.find("loading weights | 16.0 GiB") != std::string::npos &&
            startup_output.find("weights ready | 16.0 GiB | 2.0s | 8.00 GiB/s") !=
                std::string::npos &&
            startup_output.find("engine ready | qwen3.6-27b | total 3.0s | weights 16.0 GiB") !=
                std::string::npos &&
            startup_output.find("CUDA initialized") == std::string::npos,
        "normal startup pretty output is noisy or incomplete");

    std::string tool_output;
    {
        StderrCapture capture;
        {
            ninfer::product::LoggingRuntime logging(
                {.logger_name  = "ninfer",
                 .color        = ninfer::product::LogColorMode::Never,
                 .presentation = ninfer::product::LogPresentation::Tool});
            logging.logger()->info("engine ready");
            logging.logger()->error("failed");
            logging.flush();
        }
        tool_output = capture.finish();
    }
    failures +=
        check(tool_output == "engine ready\nerror: failed\n", "tool pretty prefix mismatch");
    return failures == 0 ? 0 : 1;
}
