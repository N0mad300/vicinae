#pragma once
#include <memory>
#include <unordered_map>
#include <vector>
#include "services/app-service/abstract-app-db.hpp"
#include "windows-app.hpp"

class WindowsAppDatabase : public AbstractAppDatabase {
public:
  WindowsAppDatabase();

  std::vector<std::filesystem::path> defaultSearchPaths() const override;
  bool scan(const std::vector<std::filesystem::path> &paths) override;

  bool launch(const AbstractApplication &app, const std::vector<QString> &args = {}) const override;
  bool launchTerminalCommand(const std::vector<QString> &cmdline,
                             const LaunchTerminalCommandOptions &opts = {}) const override;

  PreferenceList preferences() const override;
  std::vector<AppPtr> findOpeners(const Target &target) const override;
  AppPtr findDefaultOpener(const Target &target) const override;
  AppPtr findById(const QString &id) const override;
  std::vector<AppPtr> list() const override;
  AppPtr findByClass(const QString &name) const override;

  AppPtr fileBrowser() const override;
  AppPtr genericTextEditor() const override;
  AppPtr webBrowser() const override;
  AppPtr terminalEmulator() const override;
  bool showInFileBrowser(const std::filesystem::path &path, bool select) const override;

private:
  void rebuildStaticApps();
  void indexApp(const std::shared_ptr<WindowsApplication> &app);
  AppPtr findByExecutable(const std::filesystem::path &path) const;
  AppPtr shellOpenApp() const { return m_shellOpenApp; }

  std::vector<std::shared_ptr<WindowsApplication>> m_apps;
  std::unordered_map<QString, std::shared_ptr<WindowsApplication>> m_appsById;
  std::unordered_map<QString, std::shared_ptr<WindowsApplication>> m_appsByExecutable;
  std::shared_ptr<WindowsApplication> m_shellOpenApp;
  std::shared_ptr<WindowsApplication> m_explorerApp;
  std::shared_ptr<WindowsApplication> m_cmdApp;
};
