#include <exception>
#include <iostream>
#include <glaze/glaze.hpp>
#include "server.hpp"

int main(int argc, char **argv) {
  try {
    ServerLaunchOptions opts;

    if (argc >= 2) {
      if (auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(opts, std::string_view{argv[1]})) {
        std::println(std::cerr, "Failed to parse launch options: {}", glz::format_error(err));
        return 1;
      }
    }

    return startServer(opts);
  } catch (const std::exception &error) {
    std::cerr << "Fatal server startup error: " << error.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Fatal server startup error: unknown exception" << std::endl;
    return 1;
  }
}
