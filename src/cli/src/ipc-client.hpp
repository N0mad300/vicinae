#pragma once
#include <common/enumerate.hpp>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <QByteArray>
#include <QLocalSocket>
#include <QString>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#include "generated/ipc-client.hpp"

namespace cli {

inline std::filesystem::path socketPath() {
  std::filesystem::path base;
#ifdef __APPLE__
  if (const char *t = getenv("TMPDIR")) {
    base = t;
  } else {
    base = "/tmp";
  }
#elif defined(_WIN32)
  return "vicinae";
#else
  if (const char *r = getenv("XDG_RUNTIME_DIR")) {
    base = r;
  } else {
    const char *user = getenv("USER");
    base = std::filesystem::path("/tmp") / (std::string("runtime-") + (user ? user : "unknown"));
  }
#endif
  return base / "vicinae" / "vicinae.sock";
}

#ifdef _WIN32

class IpcClient {
public:
  static std::expected<IpcClient, std::string> connect() {
    auto socket = std::make_unique<QLocalSocket>();
    socket->connectToServer(QString::fromStdString(socketPath().string()));

    if (!socket->waitForConnected(1000)) {
      return std::unexpected(std::format("Failed to connect to {}: {}", socketPath().string(),
                                         socket->errorString().toStdString()));
    }

    return IpcClient(std::move(socket));
  }

  IpcClient(IpcClient &&other) noexcept
      : m_socket(std::move(other.m_socket)), m_transport(m_socket.get()), m_rpc(m_transport),
        m_client(m_rpc) {}

  IpcClient(const IpcClient &) = delete;
  IpcClient &operator=(const IpcClient &) = delete;
  IpcClient &operator=(IpcClient &&) = delete;

  std::expected<ipc::PingResponse, std::string> ping() {
    return call<ipc::PingResponse>([&](auto cb) { m_client.ipc().ping(std::move(cb)); });
  }

  std::expected<ipc::DeeplinkResponse, std::string> deeplink(ipc::DeeplinkRequest req) {
    return call<ipc::DeeplinkResponse>([&](auto cb) { m_client.ipc().deeplink(req, std::move(cb)); });
  }

  static std::expected<ipc::DeeplinkResponse, std::string>
  sendDeeplink(std::string_view url, const std::vector<std::pair<std::string, std::string>> &query = {}) {
    std::string fullUrl{url};
    for (const auto &[idx, arg] : query | vicinae::enumerate) {
      fullUrl.append(std::format("{}{}={}", idx == 0 ? "?" : "&", arg.first, arg.second));
    }
    return connect().and_then([&](IpcClient client) { return client.deeplink({.url = std::move(fullUrl)}); });
  }

  std::expected<ipc::DescribeResponse, std::string> describe() {
    return call<ipc::DescribeResponse>([&](auto cb) { m_client.ipc().describe(std::move(cb)); });
  }

  std::expected<ipc::LaunchAppResponse, std::string> launchApp(ipc::LaunchAppRequest req) {
    return call<ipc::LaunchAppResponse>([&](auto cb) { m_client.ipc().launchApp(req, std::move(cb)); });
  }

  std::expected<std::vector<ipc::FileResult>, std::string> fsQuery(std::string_view query, int limit = 100,
                                                                   std::optional<std::string> category = {}) {
    ipc::FsQueryParams params{.limit = limit, .category = std::move(category)};

    return call<std::vector<ipc::FileResult>>(
        [&](auto cb) { m_client.ipc().fsQuery(std::string{query}, params, std::move(cb)); });
  }

  std::expected<ipc::DMenuResponse, std::string> dmenu(ipc::DMenuRequest req) {
    return call<ipc::DMenuResponse>([&](auto cb) { m_client.ipc().dmenu(req, std::move(cb)); });
  }

private:
  explicit IpcClient(std::unique_ptr<QLocalSocket> socket)
      : m_socket(std::move(socket)), m_transport(m_socket.get()), m_rpc(m_transport), m_client(m_rpc) {}

  struct SocketTransport : public ipc::AbstractTransport {
    QLocalSocket *socket;
    explicit SocketTransport(QLocalSocket *socket) : socket(socket) {}
    void send(std::string_view data) override {
      uint32_t size = static_cast<uint32_t>(data.size());
      socket->write(reinterpret_cast<const char *>(&size), sizeof(size));
      socket->write(data.data(), static_cast<qint64>(data.size()));
      socket->waitForBytesWritten(1000);
    }
  };

  std::expected<QByteArray, std::string> readExact(qint64 size) {
    QByteArray buf;
    while (buf.size() < size) {
      if (!m_socket->bytesAvailable() && !m_socket->waitForReadyRead(5000)) {
        return std::unexpected(m_socket->errorString().toStdString());
      }
      buf.append(m_socket->read(size - buf.size()));
    }
    return buf;
  }

  std::expected<std::string, std::string> recv() {
    auto header = readExact(sizeof(uint32_t));
    if (!header) return std::unexpected(header.error());

    uint32_t size;
    std::memcpy(&size, header->constData(), sizeof(size));

    auto body = readExact(size);
    if (!body) return std::unexpected(body.error());
    return std::string(body->constData(), static_cast<size_t>(body->size()));
  }

  template <typename T>
  std::expected<T, std::string>
  call(std::function<void(std::function<void(std::expected<T, std::string>)>)> fn) {
    std::expected<T, std::string> result = std::unexpected(std::string{"no response"});
    fn([&result](std::expected<T, std::string> res) { result = std::move(res); });

    auto data = recv();
    if (!data) return std::unexpected(data.error());
    if (auto res = m_client.route(*data); !res) return std::unexpected(res.error());

    return result;
  }

  std::unique_ptr<QLocalSocket> m_socket;
  SocketTransport m_transport;
  ipc::RpcTransport m_rpc;
  ipc::Client m_client;
};

#else

class IpcClient {
public:
  static std::expected<IpcClient, std::string> connect() {
    int const fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) return std::unexpected(std::format("Failed to create socket: {}", strerror(errno)));

    auto path = socketPath().string();
    struct sockaddr_un addr{.sun_family = AF_UNIX};
    strncpy(addr.sun_path, path.data(), path.size());

    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1) {
      ::close(fd);
      return std::unexpected(std::format("Failed to connect to {}: {}", path, strerror(errno)));
    }

    return IpcClient(fd);
  }

  ~IpcClient() {
    if (m_fd >= 0) ::close(m_fd);
  }

  IpcClient(IpcClient &&other) noexcept
      : m_fd(other.m_fd), m_transport(m_fd), m_rpc(m_transport), m_client(m_rpc) {
    other.m_fd = -1;
  }

  IpcClient(const IpcClient &) = delete;
  IpcClient &operator=(const IpcClient &) = delete;
  IpcClient &operator=(IpcClient &&) = delete;

  std::expected<ipc::PingResponse, std::string> ping() {
    return call<ipc::PingResponse>([&](auto cb) { m_client.ipc().ping(std::move(cb)); });
  }

  std::expected<ipc::DeeplinkResponse, std::string> deeplink(ipc::DeeplinkRequest req) {
    return call<ipc::DeeplinkResponse>([&](auto cb) { m_client.ipc().deeplink(req, std::move(cb)); });
  }

  static std::expected<ipc::DeeplinkResponse, std::string>
  sendDeeplink(std::string_view url, const std::vector<std::pair<std::string, std::string>> &query = {}) {
    std::string fullUrl{url};
    for (const auto &[idx, arg] : query | vicinae::enumerate) {
      fullUrl.append(std::format("{}{}={}", idx == 0 ? "?" : "&", arg.first, arg.second));
    }
    return connect().and_then([&](IpcClient client) { return client.deeplink({.url = std::move(fullUrl)}); });
  }

  std::expected<ipc::DescribeResponse, std::string> describe() {
    return call<ipc::DescribeResponse>([&](auto cb) { m_client.ipc().describe(std::move(cb)); });
  }

  std::expected<ipc::LaunchAppResponse, std::string> launchApp(ipc::LaunchAppRequest req) {
    return call<ipc::LaunchAppResponse>([&](auto cb) { m_client.ipc().launchApp(req, std::move(cb)); });
  }

  std::expected<std::vector<ipc::FileResult>, std::string> fsQuery(std::string_view query, int limit = 100,
                                                                   std::optional<std::string> category = {}) {
    ipc::FsQueryParams params{
        .limit = limit,
        .category = std::move(category),
    };

    return call<std::vector<ipc::FileResult>>(
        [&](auto cb) { m_client.ipc().fsQuery(std::string{query}, params, std::move(cb)); });
  }

  std::expected<ipc::DMenuResponse, std::string> dmenu(ipc::DMenuRequest req) {
    return call<ipc::DMenuResponse>([&](auto cb) { m_client.ipc().dmenu(req, std::move(cb)); });
  }

private:
  IpcClient(int fd) : m_fd(fd), m_transport(m_fd), m_rpc(m_transport), m_client(m_rpc) {}

  struct SocketTransport : public ipc::AbstractTransport {
    int fd;
    explicit SocketTransport(int &fd) : fd(fd) {}
    void send(std::string_view data) override {
      uint32_t size = data.size();
      ::send(fd, &size, sizeof(size), 0);
      ::send(fd, data.data(), data.size(), 0);
    }
  };

  std::expected<std::string, std::string> recv() {
    uint32_t size;
    if (::recv(m_fd, &size, sizeof(size), 0) < static_cast<ssize_t>(sizeof(size)))
      return std::unexpected("Failed to read response size");

    std::string buf(size, '\0');
    size_t total = 0;
    while (total < size) {
      auto n = ::recv(m_fd, buf.data() + total, size - total, 0);
      if (n <= 0) return std::unexpected("Failed to read response data");
      total += n;
    }
    return buf;
  }

  template <typename T>
  std::expected<T, std::string>
  call(std::function<void(std::function<void(std::expected<T, std::string>)>)> fn) {
    std::expected<T, std::string> result = std::unexpected(std::string{"no response"});
    fn([&result](std::expected<T, std::string> res) { result = std::move(res); });

    auto data = recv();
    if (!data) return std::unexpected(data.error());
    if (auto res = m_client.route(*data); !res) return std::unexpected(res.error());

    return result;
  }

  int m_fd;
  SocketTransport m_transport;
  ipc::RpcTransport m_rpc;
  ipc::Client m_client;
};

#endif

} // namespace cli