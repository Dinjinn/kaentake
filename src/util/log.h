#pragma once

#include <string_view>

namespace Kaentake::Log {

void Initialize() noexcept;
bool IsInitialized() noexcept;
void Debug(std::string_view message) noexcept;
void Error(std::string_view message) noexcept;

} // namespace Kaentake::Log
