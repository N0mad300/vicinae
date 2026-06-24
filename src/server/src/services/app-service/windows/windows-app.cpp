#include <QFileInfo>
#include "builtin_icon.hpp"
#include "ui/image/url.hpp"
#include "windows-app.hpp"

WindowsApplication::WindowsApplication(Data data) : m_data(std::move(data)) {}

ImageURL WindowsApplication::iconUrl() const {
  if (!m_data.iconPath.empty()) { return ImageURL::fileIcon(m_data.iconPath); }
  if (!m_data.targetPath.empty()) { return ImageURL::fileIcon(m_data.targetPath); }
  if (!m_data.path.empty()) { return ImageURL::fileIcon(m_data.path); }
  if (m_data.builtinIcon) { return ImageURL::builtin(*m_data.builtinIcon); }
  return ImageURL::builtin(BuiltinIcon::AppWindow);
}

std::optional<QString> WindowsApplication::windowClass() const {
  if (!m_data.program.isEmpty()) { return m_data.program; }
  return m_data.id;
}

bool WindowsApplication::matchesWindowClass(const QString &wmClass) const {
  if (wmClass.isEmpty()) return false;

  auto matches = [&](const QString &value) {
    return !value.isEmpty() && value.compare(wmClass, Qt::CaseInsensitive) == 0;
  };

  if (matches(m_data.id) || matches(m_data.displayName) || matches(m_data.program)) return true;
  if (matches(m_data.appUserModelId) || matches(m_data.launchTarget)) return true;

  if (!m_data.targetPath.empty()) {
    QFileInfo const target(QString::fromStdWString(m_data.targetPath.wstring()));
    return matches(target.completeBaseName()) || matches(target.fileName());
  }

  return false;
}
