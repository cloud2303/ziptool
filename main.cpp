#include "zip.h"
#include <filesystem>
#include <iostream>
#include <indicators/progress_bar.hpp>
#include <CLI/CLI.hpp>
#include <set>
#include <system_error>
#include <utility>
#include <vector>
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
             bool windows_style, const std::vector<std::pair<fs::path, std::string>> &extra_files, Callback on_progress) {
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

    int total_files = static_cast<int>(extra_files.size());
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
            if (total_files > 0 && (processed % 50 == 0 || processed == total_files)) {
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

    for (const auto &[source_path, entry_name] : extra_files) {
        if (!fs::exists(source_path) || !fs::is_regular_file(source_path)) {
            continue;
        }
        if (fs::equivalent(source_path, zip_path)) {
            std::cout << "zip file is exist" << std::endl;
            continue;
        }
        zip_entry_open(zip, entry_name.c_str());
#if defined(_WIN32) || defined(__WIN32__)
        zip_entry_set_unix_permissions(zip, 0755, 0);
#endif
        zip_entry_fwrite(zip, source_path.string().c_str());
        zip_entry_close(zip);
        processed++;
        if (total_files > 0 && (processed % 50 == 0 || processed == total_files)) {
            int percent = processed * 100 / total_files;
            on_progress(percent);
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
    app.require_subcommand(1);

    auto *zip_sub = app.add_subcommand("zip", "直接压缩指定目录");
    std::string zip_name = "output.zip";
    std::string dir_arg;
    std::vector<std::string> ignores;
    std::vector<std::string> extra_inputs;
    bool windows_style = false;
    zip_sub->add_option("-f,--filename", zip_name, "输出的文件名")->default_str("output.zip");
    zip_sub->add_option("-d,--dir", dir_arg, "要压缩的文件路径,当前目录下相对路径")->required();
    zip_sub->add_option("-i,--ignore", ignores, "忽略的相对路径（可多次传入或用逗号分隔）")->delimiter(',');
    zip_sub->add_option("-e,--extra", extra_inputs, "额外压缩的文件路径（可多次传入或用逗号分隔）")->delimiter(',');
    zip_sub->add_flag("-w,--windows-style", windows_style, "Windows压缩风格，套一层同名文件夹");

    // 子命令：重命名文件夹，然后压缩重命名的文件夹
    auto *rename_zip = app.add_subcommand("rename-zip", "重命名文件夹后再压缩");
    std::string rz_dir_arg;
    std::string rz_new_name;
    bool rz_windows_style = false;
    rename_zip->add_option("-d,--dir", rz_dir_arg, "要重命名并压缩的文件夹，相对当前目录")->required();
    rename_zip->add_option("-n,--new-name", rz_new_name, "重命名后的新文件夹名（不含路径）")->required();
    rename_zip->add_flag("-w,--windows-style", rz_windows_style, "Windows压缩风格，套一层同名文件夹");

    try { app.parse(argc, argv); } catch (const CLI::ParseError &e) { return app.exit(e); }

    if (rename_zip->parsed()) {
        // 处理重命名并压缩子命令
        const fs::path original_path = fs::weakly_canonical(fs::current_path() / rz_dir_arg);
        if (!fs::exists(original_path)) {
            std::cerr << "要重命名并压缩的路径不存在: " << original_path << std::endl;
            return -1;
        }
        if (!fs::is_directory(original_path)) {
            std::cerr << "目标不是文件夹: " << original_path << std::endl;
            return -1;
        }
        if (rz_new_name.empty() || rz_new_name.find_first_of("\\/:") != std::string::npos) {
            std::cerr << "新名称非法（不得包含路径分隔符或保留字符）: " << rz_new_name << std::endl;
            return -1;
        }

        const fs::path parent = original_path.parent_path();
        const fs::path renamed_path = parent / rz_new_name;
        if (fs::exists(renamed_path)) {
            std::cerr << "重命名后的目标已存在: " << renamed_path << std::endl;
            return -1;
        }

        std::set<fs::path> ignore_set;
        std::vector<std::pair<fs::path, std::string>> extra_files;

        // 执行重命名
        std::error_code ec;
        fs::rename(original_path, renamed_path, ec);
        if (ec) {
            std::cerr << "重命名失败: " << ec.message() << std::endl;
            return -1;
        }

        // scope guard 用于恢复名称
        bool restored = false;
        auto restore = [&]() {
            std::error_code rec;
            fs::rename(renamed_path, original_path, rec);
            restored = !rec.operator bool();
            if (rec) {
                std::cerr << "警告: 无法恢复原始文件夹名称，请手动将 '" << renamed_path.string()
                          << "' 改回 '" << original_path.string() << "'。错误: " << rec.message() << std::endl;
            }
        };

        // 压缩重命名后的文件夹，压缩包名称固定为 新目录名 + ".zip"
        const fs::path zip_path = fs::current_path() / (rz_new_name + ".zip");
        const int processed = compress(zip_path, renamed_path, ignore_set, rz_windows_style, extra_files,
                                       [&](const int percent) { bar.set_progress(percent); });
        bar.mark_as_completed();
        // 恢复原名
        restore();

        std::cout << "压缩完成，共处理" << processed << "个文件,输出路径为" << zip_path.string() << std::endl;
        return 0;
    }

    // 处理 zip 子命令（原默认流程）
    if (zip_sub->parsed()) {
        const fs::path root_path = fs::weakly_canonical(fs::current_path() / dir_arg);
        if (!fs::exists(root_path)) {
            std::cerr << "要压缩的路径不存在: " << root_path << std::endl;
            return -1;
        }

        std::set<fs::path> ignore_set;
        if (!ignores.empty()) {
            ignore_set = make_ignore_set(fs::current_path(), ignores);
        }

        std::vector<std::pair<fs::path, std::string>> extra_files;
        for (const auto &extra : extra_inputs) {
            if (extra.empty()) continue;
            const fs::path input_path = fs::path(extra);
            const fs::path absolute_path = fs::weakly_canonical(fs::current_path() / input_path);
            if (!fs::exists(absolute_path)) {
                std::cerr << "额外文件不存在: " << absolute_path << std::endl;
                return -1;
            }
            if (!fs::is_regular_file(absolute_path)) {
                std::cerr << "额外文件不是普通文件: " << absolute_path << std::endl;
                return -1;
            }
            std::error_code ec;
            fs::path relative_path = fs::relative(absolute_path, fs::current_path(), ec);
            std::string entry_name;
            if (ec) {
                entry_name = absolute_path.filename().generic_string();
            } else {
                fs::path normalized = relative_path.lexically_normal();
                std::string rel_string = normalized.generic_string();
                if (rel_string.empty() || rel_string.rfind("..", 0) == 0) {
                    entry_name = absolute_path.filename().generic_string();
                } else {
                    entry_name = rel_string;
                }
            }
            extra_files.emplace_back(absolute_path, entry_name);
        }

        const fs::path zip_path = fs::current_path() / zip_name;

        const int processed = compress(zip_path, root_path, ignore_set, windows_style, extra_files,
                                       [&](const int percent) { bar.set_progress(percent); });
        bar.mark_as_completed();
        std::cout << "压缩完成，共处理" << processed << "个文件,输出路径为" << zip_path.string() << std::endl;
        return 0;
    }

    // 理论上因 require_subcommand(1) 不会到达此处
    return 0;
}
