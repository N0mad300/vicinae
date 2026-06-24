#pragma once
#include <cstdint>
#include <expected>
#include <unordered_map>
#include <QAbstractNativeEventFilter>
#include "services/global-shortcuts/abstract-global-shortcut-backend.hpp"

class WindowsGlobalShortcutBackend : public AbstractGlobalShortcutBackend, public QAbstractNativeEventFilter {
  Q_OBJECT

public:
  ~WindowsGlobalShortcutBackend() override;

  QString id() const override { return QStringLiteral("win32"); }

  bool start() override;
  std::expected<void, QString> bindShortcut(const GlobalShortcutRequest &request) override;
  void unbindShortcut(const QString &id) override;
  void unbindAll() override;

  bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
  struct Binding {
    QString id;
    int nativeId = 0;
  };

  std::unordered_map<QString, Binding> m_bindings;
  std::unordered_map<int, QString> m_idByNativeId;
  int m_nextNativeId = 1;
  bool m_started = false;
};
