import Subcommand.Zip;
#include <CLI/CLI.hpp>

int main(const int argc, char *argv[]) {
  CLI::App app("一个压缩工具");
  app.require_subcommand(1);
  Subcommand::zip(app);

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &e) {
    return app.exit(e);
  }
  return 0;
}
