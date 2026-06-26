module;
#include "zip.h"
#include <concepts>
#include <filesystem>
#include <print>
export module Compress;
namespace fs = std::filesystem;

namespace Compress {
export template <typename Callback>
concept ProgressCallback = std::invocable<Callback, int, int>;

export template <ProgressCallback Callback>
int compress(const fs::path &zip_path, const fs::path &source_path,
             const fs::path &archive_root_name, Callback on_progress) {
  const auto zip =
      zip_open(zip_path.string().c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');

  if (zip == nullptr) {
    std::println("zip open error");
    return 0;
  }
  int total_files = 0;
  for (const auto &entry : fs::recursive_directory_iterator(
           source_path, fs::directory_options::skip_permission_denied)) {

    if (!entry.is_regular_file()) {
      continue;
    }
    std::error_code ec;
    const bool same_file = fs::equivalent(entry.path(), zip_path, ec);

    if (!ec && same_file) {
      std::println("skip zip file itself");
      continue;
    }
    total_files++;
  }
  int processed = 0;
  for (const auto &entry : fs::recursive_directory_iterator(
           source_path, fs::directory_options::skip_permission_denied)) {
    auto file_path = entry.path();
    auto file_relative_path = file_path.lexically_relative(source_path);
    auto entry_path = (archive_root_name / file_relative_path);
    if (entry.is_regular_file()) {
      std::error_code ec;
      const bool same_file = fs::equivalent(file_path, zip_path, ec);
      if (!ec && same_file) {
        std::println("zip file is exist");
        continue;
      }
      if (zip_entry_open(zip, entry_path.generic_string().c_str()) != 0) {
        std::println("failed to open zip entry: {}",
                     entry_path.generic_string());
        continue;
      }
      zip_entry_set_unix_permissions(zip, 0644, 0);
      if (zip_entry_fwrite(zip, file_path.string().c_str()) != 0) {
        std::println("failed to write file: {}", file_path.string());
        zip_entry_close(zip);
        continue;
      }
      zip_entry_close(zip);
      processed++;
      on_progress(processed, total_files);

    } else if (entry.is_directory()) {
      std::string dir_entry_path = (entry_path / "").generic_string();
      if (zip_entry_open(zip, dir_entry_path.c_str()) != 0) {
        std::println("failed to open zip entry: {}", dir_entry_path);
        continue;
      }
      zip_entry_set_unix_permissions(zip, 0755, 1);
      zip_entry_close(zip);
    }
  }
  zip_close(zip);
  return 0;
}

} // namespace Compress