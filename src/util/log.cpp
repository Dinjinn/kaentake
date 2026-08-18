#include "pch.h"
#include "util/log.h"

#include <memory>
#include <mutex>

#include <spdlog/logger.h>
#include <spdlog/sinks/msvc_sink.h>

namespace Kaentake::Log {
namespace {

std::once_flag g_initializeOnce;
std::shared_ptr<spdlog::logger> g_logger;

} // namespace

void Initialize() noexcept {
    try {
        std::call_once(g_initializeOnce, [] {
            auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
            g_logger = std::make_shared<spdlog::logger>("kaentake", std::move(sink));
            g_logger->set_pattern("[%H:%M:%S.%e] [%l] %v");
#ifdef _DEBUG
            g_logger->set_level(spdlog::level::debug);
#else
            g_logger->set_level(spdlog::level::warn);
#endif
        });
    } catch (...) {
        g_logger.reset();
    }
}

bool IsInitialized() noexcept {
    return static_cast<bool>(g_logger);
}

void Debug(std::string_view message) noexcept {
    if (g_logger)
        g_logger->debug("{}", message);
}

void Error(std::string_view message) noexcept {
    if (g_logger)
        g_logger->error("{}", message);
}

} // namespace Kaentake::Log
