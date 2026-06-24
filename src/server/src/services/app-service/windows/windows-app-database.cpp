#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Objbase.h>
#include <Propkey.h>
#include <Propsys.h>
#include <Windows.h>
#include <KnownFolders.h>
#include <ShlObj.h>
#include <ShObjIdl.h>
#include <Shlwapi.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <wrl/client.h>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <qlogging.h>
#include "windows-app-database.hpp"

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace {

class ComApartment {
public:
  ComApartment() {
    m_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_initialized = SUCCEEDED(m_result);
  }

  ~ComApartment() {
    if (m_initialized) { CoUninitialize(); }
  }

  bool usable() const { return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE; }

private:
  HRESULT m_result = E_FAIL;
  bool m_initialized = false;
};

QString fromPath(const fs::path &path) { return QString::fromStdWString(path.wstring()); }

fs::path toPath(const QString &value) { return fs::path(value.toStdWString()); }

QString lowerKey(QString value) { return value.trimmed().toCaseFolded(); }

QString normalizedPathKey(const fs::path &path) {
  if (path.empty()) return {};
  std::error_code ec;
  auto normalized = fs::weakly_canonical(path, ec);
  if (ec) { normalized = path.lexically_normal(); }
  return lowerKey(fromPath(normalized));
}

QString executableNameKey(const fs::path &path) {
  if (path.empty()) return {};
  return lowerKey(fromPath(path.filename()));
}

QString trimNullTerminated(const wchar_t *value, size_t size) {
  auto end = std::find(value, value + size, L'\0');
  return QString::fromWCharArray(value, static_cast<qsizetype>(std::distance(value, end)));
}

std::optional<fs::path> knownFolder(REFKNOWNFOLDERID id) {
  PWSTR rawPath = nullptr;
  if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &rawPath))) { return std::nullopt; }

  fs::path path(rawPath);
  CoTaskMemFree(rawPath);
  return path;
}

void appendIfDirectory(std::vector<fs::path> &paths, const fs::path &path) {
  std::error_code ec;
  if (!path.empty() && fs::is_directory(path, ec)) { paths.emplace_back(path); }
}

std::vector<fs::path> fallbackStartMenuPaths() {
  std::vector<fs::path> paths;
  auto env = QProcessEnvironment::systemEnvironment();

  if (auto appData = env.value(QStringLiteral("APPDATA")); !appData.isEmpty()) {
    appendIfDirectory(paths, toPath(appData) / "Microsoft" / "Windows" / "Start Menu" / "Programs");
  }

  if (auto programData = env.value(QStringLiteral("PROGRAMDATA")); !programData.isEmpty()) {
    appendIfDirectory(paths, toPath(programData) / "Microsoft" / "Windows" / "Start Menu" / "Programs");
  }

  return paths;
}

QString makeRelativeId(const fs::path &path, const fs::path &root) {
  std::error_code ec;
  auto relative = path.lexically_relative(root);
  if (relative.empty()) { relative = path.filename(); }

  QString id = fromPath(relative);
  id.replace('\\', '.');
  id.replace('/', '.');
  if (id.isEmpty()) { id = fromPath(path.filename()); }
  return id;
}

bool hasShortcutExtension(const fs::path &path) {
  return fromPath(path.extension()).compare(QStringLiteral(".lnk"), Qt::CaseInsensitive) == 0;
}

bool isStartMenuLaunchFile(const fs::path &path) {
  QString const extension = lowerKey(fromPath(path.extension()));
  return extension == QStringLiteral(".lnk") || extension == QStringLiteral(".url") ||
         extension == QStringLiteral(".appref-ms");
}

bool isHidden(const fs::path &path) {
  DWORD const attrs = GetFileAttributesW(path.wstring().c_str());
  return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_HIDDEN) != 0;
}

QString takeCoTaskString(PWSTR raw) {
  if (!raw) return {};

  QString value = QString::fromWCharArray(raw);
  CoTaskMemFree(raw);
  return value;
}

QString expandEnvironmentString(QString value) {
  if (value.isEmpty()) return value;

  auto input = value.toStdWString();
  DWORD const required = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
  if (required <= 1) return value;

  std::wstring buffer(required, L'\0');
  if (ExpandEnvironmentStringsW(input.c_str(), buffer.data(), required) == 0) return value;
  return trimNullTerminated(buffer.data(), buffer.size());
}

QString normalizedIconLocation(QString value) {
  value = expandEnvironmentString(value.trimmed());
  if (value.isEmpty() || value.startsWith(',')) return {};

  qsizetype const comma = value.lastIndexOf(',');
  if (comma > 0) {
    bool ok = false;
    value.mid(comma + 1).toInt(&ok);
    if (ok) { value = value.left(comma); }
  }

  if (value.startsWith('"') && value.endsWith('"') && value.size() > 1) {
    value = value.mid(1, value.size() - 2);
  }

  return value.trimmed();
}

struct ShortcutInfo {
  fs::path targetPath;
  fs::path iconPath;
  QString arguments;
  QString description;
};

ShortcutInfo readShortcut(const fs::path &path) {
  ShortcutInfo info;
  ComApartment apartment;
  if (!apartment.usable()) return info;

  ComPtr<IShellLinkW> shellLink;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink)))) {
    return info;
  }

  ComPtr<IPersistFile> persistFile;
  if (FAILED(shellLink.As(&persistFile))) { return info; }
  if (FAILED(persistFile->Load(path.wstring().c_str(), STGM_READ))) { return info; }

  WIN32_FIND_DATAW findData = {};
  std::array<wchar_t, (MAX_PATH * 8) + 1> target = {};
  if (SUCCEEDED(
          shellLink->GetPath(target.data(), static_cast<int>(target.size() - 1), &findData, SLGP_RAWPATH))) {
    auto targetString = trimNullTerminated(target.data(), target.size());
    if (!targetString.isEmpty()) { info.targetPath = toPath(targetString); }
  }

  std::array<wchar_t, INFOTIPSIZE + 1> description = {};
  if (SUCCEEDED(shellLink->GetDescription(description.data(), static_cast<int>(description.size() - 1)))) {
    info.description = trimNullTerminated(description.data(), description.size());
  }

  std::array<wchar_t, INFOTIPSIZE + 1> arguments = {};
  if (SUCCEEDED(shellLink->GetArguments(arguments.data(), static_cast<int>(arguments.size() - 1)))) {
    info.arguments = trimNullTerminated(arguments.data(), arguments.size());
  }

  std::array<wchar_t, (MAX_PATH * 8) + 1> icon = {};
  int iconIndex = 0;
  if (SUCCEEDED(shellLink->GetIconLocation(icon.data(), static_cast<int>(icon.size() - 1), &iconIndex))) {
    auto iconString = normalizedIconLocation(trimNullTerminated(icon.data(), icon.size()));
    if (!iconString.isEmpty()) { info.iconPath = toPath(iconString); }
  }

  return info;
}

QString shellItemDisplayName(IShellItem *item, SIGDN nameKind) {
  PWSTR rawName = nullptr;
  if (!item || FAILED(item->GetDisplayName(nameKind, &rawName))) return {};
  return takeCoTaskString(rawName);
}

QString shellItemPropertyString(IShellItem *item, REFPROPERTYKEY key) {
  if (!item) return {};

  ComPtr<IPropertyStore> store;
  if (FAILED(item->BindToHandler(nullptr, BHID_PropertyStore, IID_PPV_ARGS(&store)))) return {};

  PROPVARIANT value;
  PropVariantInit(&value);

  QString result;
  if (SUCCEEDED(store->GetValue(key, &value))) {
    if (value.vt == VT_LPWSTR && value.pwszVal) {
      result = QString::fromWCharArray(value.pwszVal);
    } else if (value.vt == VT_BSTR && value.bstrVal) {
      result = QString::fromWCharArray(value.bstrVal);
    }
  }

  PropVariantClear(&value);
  return result;
}

QString appUserModelIdForShellItem(IShellItem *item) {
  QString appId = shellItemPropertyString(item, PKEY_AppUserModel_ID);
  if (!appId.isEmpty()) return appId;

  QString parsingName = shellItemDisplayName(item, SIGDN_DESKTOPABSOLUTEPARSING);
  static const QString APPS_FOLDER_PREFIX = QStringLiteral("shell:AppsFolder\\");
  if (parsingName.startsWith(APPS_FOLDER_PREFIX, Qt::CaseInsensitive)) {
    parsingName = parsingName.mid(APPS_FOLDER_PREFIX.size());
  }
  return parsingName;
}

void addSystemKeywords(WindowsApplication::Data &data) {
  QString const id = data.appUserModelId.toCaseFolded();

  if (id.contains(QStringLiteral("immersivecontrolpanel")) ||
      data.launchTarget.compare(QStringLiteral("ms-settings:"), Qt::CaseInsensitive) == 0) {
    data.keywords.emplace_back(QStringLiteral("Settings"));
    data.keywords.emplace_back(QString::fromUtf8("Param\xc3\xa8tres"));
    data.keywords.emplace_back(QStringLiteral("Windows Settings"));
    data.keywords.emplace_back(QStringLiteral("ms-settings:"));
  }

  if (id.contains(QStringLiteral("calculator")) ||
      data.program.compare(QStringLiteral("calc.exe"), Qt::CaseInsensitive) == 0) {
    data.keywords.emplace_back(QStringLiteral("Calculator"));
    data.keywords.emplace_back(QStringLiteral("Calculatrice"));
    data.keywords.emplace_back(QStringLiteral("calc.exe"));
  }
}

bool isTerminalExecutable(const fs::path &path, const QString &displayName);

std::vector<WindowsApplication::Data> appsFolderApplications() {
  std::vector<WindowsApplication::Data> apps;

  ComApartment apartment;
  if (!apartment.usable()) return apps;

  ComPtr<IShellItem> appsFolder;
  if (FAILED(
          SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&appsFolder)))) {
    return apps;
  }

  ComPtr<IEnumShellItems> enumItems;
  if (FAILED(appsFolder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&enumItems)))) return apps;

  ULONG fetched = 0;
  ComPtr<IShellItem> item;
  while (enumItems->Next(1, item.ReleaseAndGetAddressOf(), &fetched) == S_OK && fetched == 1) {
    QString appUserModelId = appUserModelIdForShellItem(item.Get());
    if (appUserModelId.isEmpty()) continue;

    QString displayName = shellItemDisplayName(item.Get(), SIGDN_NORMALDISPLAY);
    if (displayName.isEmpty()) { displayName = shellItemPropertyString(item.Get(), PKEY_ItemNameDisplay); }
    if (displayName.isEmpty()) { displayName = appUserModelId; }

    WindowsApplication::Data data;
    data.id = QStringLiteral("windows.appsfolder.") + appUserModelId;
    data.displayName = displayName;
    data.description = QStringLiteral("Windows application");
    data.program = appUserModelId;
    data.launchTarget = QStringLiteral("shell:AppsFolder\\") + appUserModelId;
    data.appUserModelId = appUserModelId;
    data.path = toPath(data.launchTarget);
    data.launchKind = WindowsApplication::LaunchKind::AppUserModel;
    data.keywords.reserve(6);
    data.keywords.emplace_back(appUserModelId);
    data.keywords.emplace_back(data.launchTarget);
    addSystemKeywords(data);

    apps.emplace_back(std::move(data));
  }

  return apps;
}

std::optional<QString> registryStringValue(HKEY key, const wchar_t *valueName) {
  DWORD type = 0;
  DWORD bytes = 0;
  LSTATUS status =
      RegGetValueW(key, nullptr, valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, nullptr, &bytes);
  if (status != ERROR_SUCCESS || bytes == 0) return std::nullopt;

  std::wstring buffer((bytes / sizeof(wchar_t)) + 1, L'\0');
  status = RegGetValueW(key, nullptr, valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, buffer.data(),
                        &bytes);
  if (status != ERROR_SUCCESS) return std::nullopt;

  QString value = trimNullTerminated(buffer.data(), buffer.size());
  if (type == REG_EXPAND_SZ) { value = expandEnvironmentString(value); }
  if (value.isEmpty()) return std::nullopt;
  return value;
}

std::optional<fs::path> executablePathFromCommand(QString value) {
  value = value.trimmed();
  if (value.isEmpty()) return std::nullopt;

  QString executable;
  if (value.startsWith('"')) {
    qsizetype const endQuote = value.indexOf('"', 1);
    if (endQuote > 1) { executable = value.mid(1, endQuote - 1); }
  }

  if (executable.isEmpty()) {
    qsizetype const exeEnd = value.indexOf(QStringLiteral(".exe"), 0, Qt::CaseInsensitive);
    if (exeEnd >= 0) {
      executable = value.left(exeEnd + 4);
    } else {
      executable = value.section(' ', 0, 0);
    }
  }

  executable = executable.trimmed();
  if (executable.isEmpty()) return std::nullopt;
  return toPath(QDir::toNativeSeparators(executable));
}

std::vector<WindowsApplication::Data> appPathRegistryApplications() {
  static constexpr wchar_t APP_PATHS_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths";
  std::vector<WindowsApplication::Data> apps;
  std::set<QString> seenExecutables;

  for (HKEY rootKey : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
    HKEY appPathsKey = nullptr;
    if (RegOpenKeyExW(rootKey, APP_PATHS_KEY, 0, KEY_READ, &appPathsKey) != ERROR_SUCCESS) continue;

    for (DWORD index = 0;; ++index) {
      std::array<wchar_t, MAX_PATH + 1> subkey = {};
      DWORD subkeySize = static_cast<DWORD>(subkey.size());
      LSTATUS const enumStatus =
          RegEnumKeyExW(appPathsKey, index, subkey.data(), &subkeySize, nullptr, nullptr, nullptr, nullptr);
      if (enumStatus == ERROR_NO_MORE_ITEMS) break;
      if (enumStatus != ERROR_SUCCESS) continue;

      HKEY appKey = nullptr;
      if (RegOpenKeyExW(appPathsKey, subkey.data(), 0, KEY_READ, &appKey) != ERROR_SUCCESS) continue;

      auto executableValue = registryStringValue(appKey, nullptr);
      RegCloseKey(appKey);
      if (!executableValue) continue;

      auto executable = executablePathFromCommand(*executableValue);
      if (!executable) continue;

      std::error_code ec;
      if (!fs::is_regular_file(*executable, ec)) continue;

      QString executableKey = normalizedPathKey(*executable);
      if (executableKey.isEmpty() || seenExecutables.contains(executableKey)) continue;
      seenExecutables.insert(executableKey);

      QFileInfo const info(fromPath(*executable));
      QString const fileName = trimNullTerminated(subkey.data(), subkey.size());
      QString displayName = QFileInfo(fileName).completeBaseName();
      if (displayName.isEmpty()) { displayName = info.completeBaseName(); }
      if (displayName.isEmpty()) continue;

      WindowsApplication::Data data;
      data.id = QStringLiteral("windows.app-path.") + executableKey;
      data.displayName = displayName;
      data.description = QStringLiteral("Registered Windows application");
      data.program = info.fileName();
      data.path = *executable;
      data.targetPath = *executable;
      data.launchKind = WindowsApplication::LaunchKind::Executable;
      data.terminalEmulator = isTerminalExecutable(*executable, displayName);
      data.keywords.reserve(3);
      data.keywords.emplace_back(info.fileName());
      data.keywords.emplace_back(fromPath(*executable));
      addSystemKeywords(data);

      apps.emplace_back(std::move(data));
    }

    RegCloseKey(appPathsKey);
  }

  return apps;
}

bool isTerminalExecutable(const fs::path &path, const QString &displayName) {
  static const std::set<QString> TERMINALS = {
      QStringLiteral("wt.exe"),        QStringLiteral("windowsterminal.exe"),
      QStringLiteral("cmd.exe"),       QStringLiteral("powershell.exe"),
      QStringLiteral("pwsh.exe"),      QStringLiteral("wezterm-gui.exe"),
      QStringLiteral("alacritty.exe"), QStringLiteral("conemu64.exe"),
      QStringLiteral("conemu.exe"),
  };

  if (TERMINALS.contains(executableNameKey(path))) return true;
  return displayName.contains(QStringLiteral("terminal"), Qt::CaseInsensitive) ||
         displayName.contains(QStringLiteral("powershell"), Qt::CaseInsensitive);
}

QString quoteWindowsArgument(const QString &arg) {
  if (arg.isEmpty()) return QStringLiteral("\"\"");

  bool const needsQuotes = arg.contains(QRegularExpression(QStringLiteral(R"([\s"])")));
  if (!needsQuotes) return arg;

  QString out = QStringLiteral("\"");
  int backslashes = 0;

  for (const QChar ch : arg) {
    if (ch == u'\\') {
      ++backslashes;
      continue;
    }

    if (ch == u'"') {
      out += QString(backslashes * 2 + 1, u'\\');
      out += ch;
      backslashes = 0;
      continue;
    }

    if (backslashes > 0) {
      out += QString(backslashes, u'\\');
      backslashes = 0;
    }
    out += ch;
  }

  if (backslashes > 0) { out += QString(backslashes * 2, u'\\'); }
  out += u'"';
  return out;
}

QString joinWindowsArguments(const std::vector<QString> &args) {
  QStringList quoted;
  quoted.reserve(static_cast<qsizetype>(args.size()));
  for (const auto &arg : args) {
    quoted.emplace_back(quoteWindowsArgument(arg));
  }
  return quoted.join(' ');
}

QString joinWindowsArguments(const QStringList &args) {
  QStringList quoted;
  quoted.reserve(args.size());
  for (const auto &arg : args) {
    quoted.emplace_back(quoteWindowsArgument(arg));
  }
  return quoted.join(' ');
}

bool shellExecute(const QString &target, const QString &parameters = {}, const fs::path &workingDir = {}) {
  if (target.isEmpty()) return false;

  auto targetW = target.toStdWString();
  auto paramsW = parameters.toStdWString();
  auto workingDirW = workingDir.empty() ? std::wstring{} : workingDir.wstring();

  SHELLEXECUTEINFOW execInfo = {};
  execInfo.cbSize = sizeof(execInfo);
  execInfo.fMask = SEE_MASK_FLAG_NO_UI;
  execInfo.lpVerb = L"open";
  execInfo.lpFile = targetW.c_str();
  execInfo.lpParameters = paramsW.empty() ? nullptr : paramsW.c_str();
  execInfo.lpDirectory = workingDirW.empty() ? nullptr : workingDirW.c_str();
  execInfo.nShow = SW_SHOWNORMAL;

  if (!ShellExecuteExW(&execInfo)) {
    qWarning() << "ShellExecute failed for" << target << "error" << GetLastError();
    return false;
  }

  return true;
}

bool shellOpenTarget(const QString &target) {
  QUrl const url(target);
  if (url.isValid() && !url.scheme().isEmpty() && !url.isLocalFile()) { return shellExecute(url.toString()); }

  QString localTarget = target;
  if (url.isLocalFile()) { localTarget = url.toLocalFile(); }
  return shellExecute(QDir::toNativeSeparators(localTarget));
}

bool activateAppUserModelId(const QString &appUserModelId, const std::vector<QString> &args) {
  if (appUserModelId.isEmpty()) return false;

  ComApartment apartment;
  if (!apartment.usable()) return false;

  ComPtr<IApplicationActivationManager> activationManager;
  if (FAILED(CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&activationManager)))) {
    return shellExecute(QStringLiteral("shell:AppsFolder\\") + appUserModelId, joinWindowsArguments(args));
  }

  auto appUserModelIdW = appUserModelId.toStdWString();
  auto argsW = joinWindowsArguments(args).toStdWString();
  DWORD processId = 0;
  HRESULT const result = activationManager->ActivateApplication(
      appUserModelIdW.c_str(), argsW.empty() ? nullptr : argsW.c_str(), AO_NONE, &processId);
  if (SUCCEEDED(result)) return true;

  qWarning() << "ActivateApplication failed for" << appUserModelId << "error" << Qt::hex << result;
  return shellExecute(QStringLiteral("shell:AppsFolder\\") + appUserModelId, joinWindowsArguments(args));
}

std::optional<QString> associationForTarget(const QString &target) {
  QUrl const url(target);
  if (url.isValid() && !url.scheme().isEmpty() && !url.isLocalFile()) { return url.scheme(); }

  QString localTarget = target;
  if (url.isLocalFile()) { localTarget = url.toLocalFile(); }

  QFileInfo const info(localTarget);
  if (info.isDir()) { return QStringLiteral("Directory"); }
  if (!info.suffix().isEmpty()) { return QStringLiteral(".") + info.suffix(); }

  if (target.startsWith('.') && !target.contains(' ')) { return target; }
  return std::nullopt;
}

std::optional<fs::path> executableForAssociation(const QString &association) {
  auto assocW = association.toStdWString();
  DWORD size = 0;
  HRESULT result =
      AssocQueryStringW(ASSOCF_VERIFY, ASSOCSTR_EXECUTABLE, assocW.c_str(), L"open", nullptr, &size);
  if (result != S_FALSE || size == 0) return std::nullopt;

  std::wstring buffer(size + 1, L'\0');
  size = static_cast<DWORD>(buffer.size());
  result =
      AssocQueryStringW(ASSOCF_VERIFY, ASSOCSTR_EXECUTABLE, assocW.c_str(), L"open", buffer.data(), &size);
  if (FAILED(result)) return std::nullopt;

  QString executable = trimNullTerminated(buffer.data(), buffer.size());
  if (executable.isEmpty()) return std::nullopt;
  return toPath(executable);
}

std::optional<fs::path> executableForTarget(const QString &target) {
  auto association = associationForTarget(target);
  if (!association) return std::nullopt;
  return executableForAssociation(*association);
}

std::shared_ptr<WindowsApplication> makeStaticApp(WindowsApplication::LaunchKind kind, QString id,
                                                  QString displayName, QString program,
                                                  fs::path executable = {}) {
  WindowsApplication::Data data;
  data.id = std::move(id);
  data.displayName = std::move(displayName);
  data.program = std::move(program);
  data.launchTarget = data.program;
  data.path = executable;
  data.targetPath = executable;
  data.launchKind = kind;
  data.displayable = false;
  data.terminalEmulator = kind == WindowsApplication::LaunchKind::Terminal;
  return std::make_shared<WindowsApplication>(std::move(data));
}

std::shared_ptr<WindowsApplication> makeDisplayableUriApp(QString id, QString displayName, QString uri,
                                                          std::vector<QString> keywords, BuiltinIcon icon) {
  WindowsApplication::Data data;
  data.id = std::move(id);
  data.displayName = std::move(displayName);
  data.description = data.displayName;
  data.program = uri;
  data.launchTarget = std::move(uri);
  data.launchKind = WindowsApplication::LaunchKind::Uri;
  data.builtinIcon = icon;
  data.keywords = std::move(keywords);
  addSystemKeywords(data);
  return std::make_shared<WindowsApplication>(std::move(data));
}

std::shared_ptr<WindowsApplication> makeDisplayableExecutableApp(QString id, QString displayName,
                                                                 QString executable,
                                                                 std::vector<QString> keywords,
                                                                 BuiltinIcon icon) {
  WindowsApplication::Data data;
  data.id = std::move(id);
  data.displayName = std::move(displayName);
  data.description = data.displayName;
  data.program = executable;
  data.launchTarget = executable;
  data.targetPath = toPath(executable);
  data.launchKind = WindowsApplication::LaunchKind::Executable;
  data.builtinIcon = icon;
  data.keywords = std::move(keywords);
  addSystemKeywords(data);
  return std::make_shared<WindowsApplication>(std::move(data));
}

} // namespace

WindowsAppDatabase::WindowsAppDatabase() {
  rebuildStaticApps();
  scan(defaultSearchPaths());
}

void WindowsAppDatabase::rebuildStaticApps() {
  m_shellOpenApp =
      makeStaticApp(WindowsApplication::LaunchKind::ShellOpen, QStringLiteral("windows.shell-open"),
                    QStringLiteral("Default Application"), QStringLiteral("ShellExecute"));
  m_explorerApp =
      makeStaticApp(WindowsApplication::LaunchKind::Explorer, QStringLiteral("windows.explorer"),
                    QStringLiteral("File Explorer"), QStringLiteral("explorer.exe"), toPath("explorer.exe"));
  m_cmdApp = makeStaticApp(WindowsApplication::LaunchKind::Terminal, QStringLiteral("windows.cmd"),
                           QStringLiteral("Command Prompt"), QStringLiteral("cmd.exe"), toPath("cmd.exe"));
}

std::vector<fs::path> WindowsAppDatabase::defaultSearchPaths() const {
  std::vector<fs::path> paths;
  paths.reserve(6);

  for (const auto &id : {FOLDERID_Programs, FOLDERID_CommonPrograms, FOLDERID_StartMenu,
                         FOLDERID_CommonStartMenu, FOLDERID_Desktop, FOLDERID_PublicDesktop}) {
    if (auto path = knownFolder(id)) { appendIfDirectory(paths, *path); }
  }

  for (auto &&path : fallbackStartMenuPaths()) {
    appendIfDirectory(paths, path);
  }

  std::set<QString> seen;
  std::vector<fs::path> unique;
  unique.reserve(paths.size());
  for (auto &path : paths) {
    auto key = normalizedPathKey(path);
    if (key.isEmpty() || seen.contains(key)) continue;
    seen.insert(key);
    unique.emplace_back(std::move(path));
  }

  return unique;
}

bool WindowsAppDatabase::scan(const std::vector<fs::path> &paths) {
  m_apps.clear();
  m_appsById.clear();
  m_appsByExecutable.clear();

  std::set<QString> seenIds;
  std::set<QString> seenDisplayNames;
  std::set<QString> seenLaunchFiles;
  bool seenWindowsSettings = false;
  bool seenWindowsCalculator = false;

  auto addDisplayableApp = [&](std::shared_ptr<WindowsApplication> app, bool deduplicateDisplayName = false) {
    if (!app) return;

    QString const idKey = lowerKey(app->id());
    QString const displayKey = lowerKey(app->displayName());
    if (!idKey.isEmpty() && seenIds.contains(idKey)) return;
    if (deduplicateDisplayName && !displayKey.isEmpty() && seenDisplayNames.contains(displayKey)) return;

    const auto &data = app->data();
    QString const markers =
        QStringLiteral("%1 %2 %3 %4 %5")
            .arg(data.id, data.displayName, data.program, data.launchTarget, data.appUserModelId)
            .toCaseFolded();
    seenWindowsSettings = seenWindowsSettings || markers.contains(QStringLiteral("immersivecontrolpanel")) ||
                          markers.contains(QStringLiteral("ms-settings"));
    seenWindowsCalculator = seenWindowsCalculator || markers.contains(QStringLiteral("calculator")) ||
                            markers.contains(QStringLiteral("calc.exe"));

    if (!idKey.isEmpty()) { seenIds.insert(idKey); }
    if (!displayKey.isEmpty()) { seenDisplayNames.insert(displayKey); }

    indexApp(app);
    m_apps.emplace_back(std::move(app));
  };

  for (const auto &root : paths) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) continue;

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator const end;
    if (ec) continue;

    while (it != end) {
      const auto path = it->path();
      it.increment(ec);
      if (ec) {
        ec.clear();
        continue;
      }

      if (!isStartMenuLaunchFile(path) || isHidden(path)) continue;

      auto launchFileKey = normalizedPathKey(path);
      if (!launchFileKey.isEmpty() && seenLaunchFiles.contains(launchFileKey)) continue;
      seenLaunchFiles.insert(std::move(launchFileKey));

      QString id = makeRelativeId(path, root);
      if (seenIds.contains(lowerKey(id))) continue;

      bool const isShortcut = hasShortcutExtension(path);
      ShortcutInfo const shortcut = isShortcut ? readShortcut(path) : ShortcutInfo{};
      QFileInfo const shortcutInfo(fromPath(path));
      QString const displayName = shortcutInfo.completeBaseName();

      WindowsApplication::Data data;
      data.id = id;
      data.displayName = displayName;
      data.description = shortcut.description;
      data.launchTarget = fromPath(path);
      data.path = path;
      data.targetPath = shortcut.targetPath;
      data.iconPath = shortcut.iconPath;
      data.program =
          shortcut.targetPath.empty() ? shortcutInfo.fileName() : fromPath(shortcut.targetPath.filename());
      data.launchKind = isShortcut ? WindowsApplication::LaunchKind::Shortcut
                                   : WindowsApplication::LaunchKind::StartMenuFile;
      data.terminalEmulator = isTerminalExecutable(shortcut.targetPath, displayName);
      data.keywords.reserve(4);
      data.keywords.emplace_back(shortcutInfo.fileName());
      if (!shortcut.description.isEmpty()) { data.keywords.emplace_back(shortcut.description); }
      if (!shortcut.arguments.isEmpty()) { data.keywords.emplace_back(shortcut.arguments); }
      if (!shortcut.targetPath.empty()) { data.keywords.emplace_back(fromPath(shortcut.targetPath)); }
      if (!isShortcut) { data.keywords.emplace_back(fromPath(path)); }

      auto app = std::make_shared<WindowsApplication>(std::move(data));
      addDisplayableApp(std::move(app));
    }
  }

  for (auto &&data : appsFolderApplications()) {
    addDisplayableApp(std::make_shared<WindowsApplication>(std::move(data)), true);
  }

  for (auto &&data : appPathRegistryApplications()) {
    addDisplayableApp(std::make_shared<WindowsApplication>(std::move(data)), true);
  }

  if (!seenWindowsSettings) {
    addDisplayableApp(
        makeDisplayableUriApp(
            QStringLiteral("windows.settings"), QStringLiteral("Settings"), QStringLiteral("ms-settings:"),
            {QString::fromUtf8("Param\xc3\xa8tres"), QStringLiteral("Windows Settings")}, BuiltinIcon::Cog),
        true);
  }
  if (!seenWindowsCalculator) {
    addDisplayableApp(
        makeDisplayableExecutableApp(
            QStringLiteral("windows.calculator"), QStringLiteral("Calculator"), QStringLiteral("calc.exe"),
            {QStringLiteral("Calculatrice"), QStringLiteral("calculator:")}, BuiltinIcon::Calculator),
        true);
  }

  indexApp(m_shellOpenApp);
  indexApp(m_explorerApp);
  indexApp(m_cmdApp);

  std::ranges::sort(m_apps, [](const auto &a, const auto &b) {
    return a->displayName().compare(b->displayName(), Qt::CaseInsensitive) < 0;
  });

  return true;
}

void WindowsAppDatabase::indexApp(const std::shared_ptr<WindowsApplication> &app) {
  if (!app) return;

  m_appsById.emplace(app->id(), app);

  const auto &data = app->data();
  for (const auto &key : {normalizedPathKey(data.targetPath), normalizedPathKey(data.path),
                          executableNameKey(data.targetPath), lowerKey(data.program)}) {
    if (!key.isEmpty() && !m_appsByExecutable.contains(key)) { m_appsByExecutable.emplace(key, app); }
  }
}

WindowsAppDatabase::AppPtr WindowsAppDatabase::findByExecutable(const fs::path &path) const {
  for (const auto &key : {normalizedPathKey(path), executableNameKey(path)}) {
    if (auto it = m_appsByExecutable.find(key); it != m_appsByExecutable.end()) { return it->second; }
  }

  return nullptr;
}

bool WindowsAppDatabase::launch(const AbstractApplication &app, const std::vector<QString> &args) const {
  auto windowsApp = dynamic_cast<const WindowsApplication *>(&app);
  if (!windowsApp) return false;

  const auto &data = windowsApp->data();

  switch (data.launchKind) {
  case WindowsApplication::LaunchKind::Shortcut:
    return shellExecute(fromPath(data.path), joinWindowsArguments(args));
  case WindowsApplication::LaunchKind::StartMenuFile:
    return shellExecute(fromPath(data.path), joinWindowsArguments(args));
  case WindowsApplication::LaunchKind::Executable:
    return shellExecute(data.launchTarget.isEmpty() ? fromPath(data.targetPath) : data.launchTarget,
                        joinWindowsArguments(args));
  case WindowsApplication::LaunchKind::AppUserModel:
    return activateAppUserModelId(data.appUserModelId, args);
  case WindowsApplication::LaunchKind::Uri:
    return shellExecute(data.launchTarget, joinWindowsArguments(args));
  case WindowsApplication::LaunchKind::ShellOpen:
    if (args.empty()) return false;
    return std::ranges::all_of(args, shellOpenTarget);
  case WindowsApplication::LaunchKind::Explorer:
    return shellExecute(QStringLiteral("explorer.exe"), joinWindowsArguments(args));
  case WindowsApplication::LaunchKind::Terminal:
    return shellExecute(data.program, joinWindowsArguments(args));
  }

  return false;
}

bool WindowsAppDatabase::launchTerminalCommand(const std::vector<QString> &cmdline,
                                               const LaunchTerminalCommandOptions &opts) const {
  if (cmdline.empty()) return false;

  QString command = joinWindowsArguments(cmdline);
  if (opts.title) { command = QStringLiteral("title %1 && %2").arg(*opts.title, command); }

  QStringList args;
  args << (opts.hold ? QStringLiteral("/K") : QStringLiteral("/C"));
  args << command;

  return shellExecute(QStringLiteral("cmd.exe"), joinWindowsArguments(args),
                      opts.workingDirectory ? toPath(*opts.workingDirectory) : fs::path{});
}

PreferenceList WindowsAppDatabase::preferences() const {
  auto paths = Preference::directories("paths");
  QJsonArray defaultPaths;
  for (const auto &searchPath : defaultSearchPaths()) {
    defaultPaths.push_back(fromPath(searchPath));
  }
  paths.setTitle("Application directories");
  paths.setDescription("Windows Start Menu directories used for application search.");
  paths.setReadOnly(true);
  paths.setDefaultValue(defaultPaths);

  return {paths};
}

std::vector<WindowsAppDatabase::AppPtr> WindowsAppDatabase::findOpeners(const Target &target) const {
  std::vector<AppPtr> apps;
  if (auto executable = executableForTarget(target)) {
    if (auto app = findByExecutable(*executable)) { apps.emplace_back(std::move(app)); }
  }
  if (apps.empty() || apps.front()->id() != m_shellOpenApp->id()) { apps.emplace_back(m_shellOpenApp); }
  return apps;
}

WindowsAppDatabase::AppPtr WindowsAppDatabase::findDefaultOpener(const Target &target) const {
  if (auto executable = executableForTarget(target)) {
    if (auto app = findByExecutable(*executable)) { return app; }
  }
  return m_shellOpenApp;
}

WindowsAppDatabase::AppPtr WindowsAppDatabase::findById(const QString &id) const {
  if (auto it = m_appsById.find(id); it != m_appsById.end()) return it->second;
  if (auto it = m_appsById.find(id + QStringLiteral(".lnk")); it != m_appsById.end()) return it->second;
  return nullptr;
}

std::vector<WindowsAppDatabase::AppPtr> WindowsAppDatabase::list() const {
  return {m_apps.begin(), m_apps.end()};
}

WindowsAppDatabase::AppPtr WindowsAppDatabase::findByClass(const QString &name) const {
  if (auto direct = findById(name)) return direct;

  for (const auto &app : m_apps) {
    if (app->matchesWindowClass(name)) return app;
  }

  return nullptr;
}

WindowsAppDatabase::AppPtr WindowsAppDatabase::fileBrowser() const { return m_explorerApp; }

WindowsAppDatabase::AppPtr WindowsAppDatabase::genericTextEditor() const {
  if (auto opener = findDefaultOpener(QStringLiteral(".txt"))) return opener;
  return nullptr;
}

WindowsAppDatabase::AppPtr WindowsAppDatabase::webBrowser() const {
  if (auto opener = findDefaultOpener(QStringLiteral("https://example.com"))) return opener;
  return nullptr;
}

WindowsAppDatabase::AppPtr WindowsAppDatabase::terminalEmulator() const {
  auto terminals = m_apps | std::views::filter([](const auto &app) { return app->isTerminalEmulator(); });
  if (!terminals.empty()) { return terminals.front(); }
  return m_cmdApp;
}

bool WindowsAppDatabase::showInFileBrowser(const fs::path &path, bool select) const {
  std::error_code ec;
  fs::path target = path;

  if (!fs::exists(target, ec)) {
    target = target.parent_path();
    select = false;
  }

  if (target.empty()) return false;

  if (select) {
    QString const arg = QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(fromPath(target)));
    return shellExecute(QStringLiteral("explorer.exe"), arg);
  }

  if (!fs::is_directory(target, ec)) {
    target = target.parent_path();
    if (target.empty()) return false;
  }

  return shellExecute(QDir::toNativeSeparators(fromPath(target)));
}
