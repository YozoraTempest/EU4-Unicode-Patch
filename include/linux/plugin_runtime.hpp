#pragma once
#include <filesystem>

namespace eu4unicode::linux_native {
std::filesystem::path plugin_directory();
std::filesystem::path resource_directory();
void prepare_environment(const std::filesystem::path& directory);
}
