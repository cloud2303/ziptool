
module;
#include <CLI/CLI.hpp>
#include <filesystem>
#include <print>
#include <string>
export module Subcommand.Zip;
import Compress;

namespace Subcommand {
export void zip(CLI::App &app) {
  auto zip_folder = app.add_subcommand("zip", "压缩文件夹");
  struct ZipFolderOptions {
    std::string name;
    std::string source_dir;
    bool windows_style{false};
  };
  auto options = std::make_shared<ZipFolderOptions>();
  namespace fs = std::filesystem;
  zip_folder
      ->add_option("-n,--name", options->name, "输出的压缩文件名,不可加拓展名")
      ->required();
  zip_folder->add_option("-s,--source", options->source_dir, "要压缩的源文件夹")
      ->required();
  zip_folder->add_flag("-w,--windows-style", options->windows_style,
                       "多一层name目录");

  zip_folder->callback([options]() {
    fs::path output_name = options->name;
    if (output_name.extension() != ".zip") {
      output_name += ".zip";
    }
    auto zip_path = fs::weakly_canonical(fs::current_path() / output_name);

    std::string archive_root_name;
    if (options->windows_style) {
      archive_root_name = fs::path(options->name).stem().string();
    }

    auto source_path =
        fs::weakly_canonical(fs::current_path() / options->source_dir);
    return Compress::compress(
        zip_path, source_path, archive_root_name, [](int progress, int total) {
          std::println("Progress: {}/{}", progress, total);
        });
  });
}
} // namespace Subcommand