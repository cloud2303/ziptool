module;
#include <print>
export module Utils.Utils;
namespace Utils {
    export void clear_current_line() {
  std::print("\r\033[2K");
  std::fflush(stdout);
}
}