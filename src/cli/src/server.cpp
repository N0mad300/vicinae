#ifndef _WIN32
#include <csignal>
#include <unistd.h>
#endif
#include <cerrno>
#include <cstring>
#include <glaze/core/reflect.hpp>
#include <thread>
#ifdef _WIN32
#include <QProcess>
#include <QString>
#include <QStringList>
#endif
#include <glaze/glaze.hpp>
#include "server.hpp"
#include "ipc-client.hpp"
#include "common/common.hpp"
#include "CLI11/CLI11.hpp"

struct ServerLaunchOptions {
  bool open = false;
  bool noExtensionRuntime = false;
  std::string config;
};

namespace {

bool terminateProcess(int pid, std::string &error) {
#ifdef _WIN32
  int const code = QProcess::execute("taskkill", QStringList{"/PID", QString::number(pid), "/F"});
  if (code == 0) return true;
  error = std::format("taskkill exited with code {}", code);
  return false;
#else
  if (kill(pid, SIGKILL) == 0) return true;
  error = std::strerror(errno);
  return false;
#endif
}

bool launchServer(const std::filesystem::path &path, const std::string &opts) {
#ifdef _WIN32
  if (!QProcess::startDetached(QString::fromStdString(path.string()), {QString::fromStdString(opts)})) {
    std::println(std::cerr, "Failed to start server process.");
    return false;
  }
  return true;
#else
  char *argv[] = {strdup(path.filename().c_str()), strdup(opts.c_str()), nullptr};

  if (execv(path.c_str(), argv) != 0) {
    std::println(std::cerr, "Failed to exec server: {}", strerror(errno));
    return false;
  }

  return true;
#endif
}

} // namespace

void CliServerCommand::setup(CLI::App *app) {
  app->add_flag("--open", m_open, "Open the main window once the server is started");
  app->add_flag("--replace", m_replace, "Replace the currently running instance if there is one");
  app->add_option("--config", m_config, "Path to the main config file");
  app->add_flag("--no-extension-runtime", m_noExtensionRuntime,
                "Do not start the extension runtime node process. Typescript extensions will not run.");
}

bool CliServerCommand::run(CLI::App *) {
  auto pingRes = cli::IpcClient::connect().and_then([](cli::IpcClient c) { return c.ping(); });

  if (pingRes) {
    if (!m_replace) {
      std::println(std::cerr, "A server is already running (pid {}). Pass --replace to replace it.",
                   pingRes->pid);
      return false;
    }

    std::println(std::cerr, "Killing existing vicinae server (pid {})...", pingRes->pid);

    std::string error;
    if (!terminateProcess(pingRes->pid, error)) {
      std::println(std::cerr, "Failed to kill process: {}", error);
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  const auto path = vicinae::findServerBinary();

  if (!path) {
    std::println(std::cerr, "Could not find vicinae server binary.");
    return false;
  }

  std::string opts;
  ServerLaunchOptions sopts{
      .open = m_open, .noExtensionRuntime = m_noExtensionRuntime, .config = m_config.string()};

  if (auto const error = glz::write_json(sopts, opts)) {
    std::println(std::cerr, "Failed to serialize server arguments: {}", glz::format_error(error));
    return false;
  }

  return launchServer(*path, opts);
}
