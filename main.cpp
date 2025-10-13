#include "zip.h"
#include <filesystem>
#include <iostream>
#include <indicators/progress_bar.hpp>
#include <CLI/CLI.hpp>
#include <set>
namespace fs = std::filesystem;

bool should_ignore(const fs::path &root_path, const fs::path &current_path, const std::set<fs::path> &ignore_list) {
    if (ignore_list.empty()) return false;
    return std::ranges::any_of(ignore_list, [&](const auto &ignore_rel) {
        const fs::path ignore_abs = fs::weakly_canonical(root_path / ignore_rel);
        return fs::equivalent(current_path, ignore_abs);
    });
}

std::set<fs::path> make_ignore_set(const fs::path &root_path, const std::vector<std::string> &ignore_list) {
    std::set<fs::path> rel_set;
    for (const auto &p: ignore_list) {
        rel_set.insert(fs::path{p});
    }
    return rel_set;
}

template<typename Callback>
int compress(const fs::path &zip_path, const fs::path &root_path, const std::set<fs::path> &ignore_set,
             bool windows_style, Callback on_progress) {
    const auto zip = zip_open(zip_path.string().c_str(),ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');

    if (zip == nullptr) {
        std::cout << "zip open error" << std::endl;
        return 0;
    }

    // Get the wrapper folder name (directory name being compressed)
    std::string wrapper_folder;
    if (windows_style) {
        wrapper_folder = root_path.filename().string() + "/";
    }

    int total_files = 0;
    for (fs::recursive_directory_iterator it(
                     root_path,
                     fs::directory_options::skip_permission_denied | fs::directory_options::follow_directory_symlink),
                 end;
         it != end; ++it) {
        const auto &entry = *it;
        auto relative_path = entry.path().lexically_relative(root_path);
        if (entry.is_directory() && should_ignore(root_path, entry.path(), ignore_set)) {
            it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_directory() && should_ignore(root_path, entry.path(), ignore_set)) continue;

        if (entry.is_regular_file()) total_files++;
    }
    int processed = 0;

    for (fs::recursive_directory_iterator it(
                     root_path,
                     fs::directory_options::skip_permission_denied | fs::directory_options::follow_directory_symlink),
                 end;
         it != end; ++it) {
        const auto &entry = *it;
        const auto &filepath = entry.path();
        auto relative_path = filepath.lexically_relative(root_path);

        if (entry.is_directory() && should_ignore(root_path, entry.path(), ignore_set)) {
            std::cout << "ignore dir: " << relative_path << std::endl;
            it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_directory() && should_ignore(root_path, entry.path(), ignore_set)) {
            std::cout << "ignore: " << relative_path << std::endl;
            continue;
        }

        if (entry.is_regular_file()) {
            if (fs::equivalent(filepath, zip_path)) {
                std::cout << "zip file is exist" << std::endl;
                continue;
            }
            std::string entry_path = windows_style ? wrapper_folder + relative_path.generic_string() : relative_path.generic_string();
            zip_entry_open(zip, entry_path.c_str());
#if defined(_WIN32) || defined(__WIN32__)
            // Set Unix-like permissions when creating ZIP on Windows
            zip_entry_set_unix_permissions(zip, 0755, 0);
#endif
            zip_entry_fwrite(zip, filepath.string().c_str());
            zip_entry_close(zip);
            processed++;
            if (processed % 50 == 0 || processed == total_files) {
                int percent = processed * 100 / total_files;
                on_progress(percent);
            }
        } else if (entry.is_directory()) {
            std::string entry_path = windows_style ? wrapper_folder + relative_path.generic_string() + "/" : relative_path.generic_string() + "/";
            zip_entry_open(zip, entry_path.c_str());
#if defined(_WIN32) || defined(__WIN32__)
            zip_entry_set_unix_permissions(zip, 0755, 1);
#endif
            zip_entry_close(zip);
        }
    }
    zip_close(zip);
    return processed;
}

int main(const int argc, char *argv[]) {
    using namespace indicators;
    ProgressBar bar{
        option::BarWidth{50},
        option::Start{"["},
        option::Fill{"="},
        option::Lead{">"},
        option::Remainder{" "},
        option::End{"]"},
        option::PostfixText{"压缩中"},
        option::ForegroundColor{Color::green},
        option::ShowPercentage{true},
        option::FontStyles{std::vector{FontStyle::bold}}
    };

    CLI::App app("一个压缩指定目录的程序");
    std::string zip_name = "output.zip";
    std::string dir_arg;
    std::vector<std::string> ignores;
    bool windows_style = false;
    app.add_option("-f,--filename", zip_name, "输出的文件名")->default_str("output.zip");
    app.add_option("-d,--dir", dir_arg, "要压缩的文件路径,当前目录下相对路径")->required();
    app.add_option("-i,--ignore", ignores, "忽略的相对路径（可多次传入或用逗号分隔）")->delimiter(',');
    app.add_flag("-w,--windows-style", windows_style, "Windows压缩风格，套一层同名文件夹");
    try { app.parse(argc, argv); } catch (const CLI::ParseError &e) { return app.exit(e); }

    const fs::path root_path = fs::weakly_canonical(fs::current_path() / dir_arg);
    if (!fs::exists(root_path)) {
        std::cerr << "要压缩的路径不存在: " << root_path << std::endl;
        return -1;
    }

    std::set<fs::path> ignore_set;
    if (!ignores.empty()) {
        ignore_set = make_ignore_set(fs::current_path(), ignores);
    }

    const fs::path zip_path = fs::current_path() / zip_name;

    const int processed = compress(zip_path, root_path, ignore_set, windows_style,
                                   [&](const int percent) { bar.set_progress(percent); });
    bar.mark_as_completed();
    std::cout << "压缩完成，共处理" << processed << "个文件,输出路径为" << zip_path.string() << std::endl;
    return 0;
}
