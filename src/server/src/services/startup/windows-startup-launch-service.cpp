#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include "services/startup/windows-startup-launch-service.hpp"

namespace {

constexpr auto RUN_KEY = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto VALUE_NAME = "Vicinae";

QString startupCommand() {
  return QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}

QSettings runSettings() { return QSettings(QString::fromLatin1(RUN_KEY), QSettings::NativeFormat); }
QString valueName() { return QString::fromLatin1(VALUE_NAME); }

} // namespace

bool WindowsStartupLaunchService::isEnabled() const {
  QSettings settings = runSettings();
  return settings.contains(valueName()) && !settings.value(valueName()).toString().isEmpty();
}

bool WindowsStartupLaunchService::setEnabled(bool enabled) {
  QSettings settings = runSettings();

  if (enabled) {
    settings.setValue(valueName(), startupCommand());
  } else {
    settings.remove(valueName());
  }

  settings.sync();
  return settings.status() == QSettings::NoError;
}
