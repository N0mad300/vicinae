#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>
#include "builtin_icon.hpp"
#include "services/app-service/abstract-app-db.hpp"

class WindowsApplication : public AbstractApplication {
public:
  enum class LaunchKind : std::uint8_t {
    Shortcut,
    StartMenuFile,
    Executable,
    AppUserModel,
    Uri,
    ShellOpen,
    Explorer,
    Terminal
  };

  struct Data {
    QString id;
    QString displayName;
    QString description;
    QString program;
    QString launchTarget;
    QString appUserModelId;
    std::filesystem::path path;
    std::filesystem::path targetPath;
    std::filesystem::path iconPath;
    std::optional<BuiltinIcon> builtinIcon;
    std::vector<QString> keywords;
    LaunchKind launchKind = LaunchKind::Shortcut;
    bool displayable = true;
    bool terminalEmulator = false;
  };

  explicit WindowsApplication(Data data);

  const Data &data() const { return m_data; }

  QString id() const override { return m_data.id; }
  QString displayName() const override { return m_data.displayName; }
  bool displayable() const override { return m_data.displayable; }
  bool isTerminalApp() const override { return false; }
  bool isTerminalEmulator() const override { return m_data.terminalEmulator; }
  ImageURL iconUrl() const override;
  std::filesystem::path path() const override { return m_data.path; }
  QString program() const override { return m_data.program; }
  std::optional<QString> windowClass() const override;
  bool matchesWindowClass(const QString &wmClass) const override;
  QString description() const override { return m_data.description; }
  std::vector<QString> keywords() const override { return m_data.keywords; }

private:
  Data m_data;
};
