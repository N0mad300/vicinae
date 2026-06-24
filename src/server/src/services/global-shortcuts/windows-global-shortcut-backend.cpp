#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <optional>
#include <QCoreApplication>
#include <QDebug>
#include "services/global-shortcuts/windows-global-shortcut-backend.hpp"

namespace {

std::optional<UINT> windowsVirtualKeyForQtKey(Qt::Key key) {
  switch (key) {
  case Qt::Key_A:
  case Qt::Key_B:
  case Qt::Key_C:
  case Qt::Key_D:
  case Qt::Key_E:
  case Qt::Key_F:
  case Qt::Key_G:
  case Qt::Key_H:
  case Qt::Key_I:
  case Qt::Key_J:
  case Qt::Key_K:
  case Qt::Key_L:
  case Qt::Key_M:
  case Qt::Key_N:
  case Qt::Key_O:
  case Qt::Key_P:
  case Qt::Key_Q:
  case Qt::Key_R:
  case Qt::Key_S:
  case Qt::Key_T:
  case Qt::Key_U:
  case Qt::Key_V:
  case Qt::Key_W:
  case Qt::Key_X:
  case Qt::Key_Y:
  case Qt::Key_Z:
    return static_cast<UINT>('A' + (key - Qt::Key_A));
  case Qt::Key_0:
  case Qt::Key_1:
  case Qt::Key_2:
  case Qt::Key_3:
  case Qt::Key_4:
  case Qt::Key_5:
  case Qt::Key_6:
  case Qt::Key_7:
  case Qt::Key_8:
  case Qt::Key_9:
    return static_cast<UINT>('0' + (key - Qt::Key_0));
  case Qt::Key_Space:
    return VK_SPACE;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    return VK_RETURN;
  case Qt::Key_Escape:
    return VK_ESCAPE;
  case Qt::Key_Tab:
    return VK_TAB;
  case Qt::Key_Backspace:
    return VK_BACK;
  case Qt::Key_Delete:
    return VK_DELETE;
  case Qt::Key_Home:
    return VK_HOME;
  case Qt::Key_End:
    return VK_END;
  case Qt::Key_PageUp:
    return VK_PRIOR;
  case Qt::Key_PageDown:
    return VK_NEXT;
  case Qt::Key_Left:
    return VK_LEFT;
  case Qt::Key_Right:
    return VK_RIGHT;
  case Qt::Key_Up:
    return VK_UP;
  case Qt::Key_Down:
    return VK_DOWN;
  case Qt::Key_Minus:
    return VK_OEM_MINUS;
  case Qt::Key_Equal:
  case Qt::Key_Plus:
    return VK_OEM_PLUS;
  case Qt::Key_BracketLeft:
  case Qt::Key_BraceLeft:
    return VK_OEM_4;
  case Qt::Key_BracketRight:
  case Qt::Key_BraceRight:
    return VK_OEM_6;
  case Qt::Key_Backslash:
    return VK_OEM_5;
  case Qt::Key_Semicolon:
    return VK_OEM_1;
  case Qt::Key_Apostrophe:
    return VK_OEM_7;
  case Qt::Key_Comma:
    return VK_OEM_COMMA;
  case Qt::Key_Period:
    return VK_OEM_PERIOD;
  case Qt::Key_Slash:
    return VK_OEM_2;
  case Qt::Key_QuoteLeft:
    return VK_OEM_3;
  case Qt::Key_F1:
  case Qt::Key_F2:
  case Qt::Key_F3:
  case Qt::Key_F4:
  case Qt::Key_F5:
  case Qt::Key_F6:
  case Qt::Key_F7:
  case Qt::Key_F8:
  case Qt::Key_F9:
  case Qt::Key_F10:
  case Qt::Key_F11:
  case Qt::Key_F12:
    return static_cast<UINT>(VK_F1 + (key - Qt::Key_F1));
  default:
    return std::nullopt;
  }
}

UINT windowsModifiers(Qt::KeyboardModifiers mods) {
  UINT result = MOD_NOREPEAT;
  if (mods.testFlag(Qt::ControlModifier)) { result |= MOD_CONTROL; }
  if (mods.testFlag(Qt::AltModifier)) { result |= MOD_ALT; }
  if (mods.testFlag(Qt::ShiftModifier)) { result |= MOD_SHIFT; }
  if (mods.testFlag(Qt::MetaModifier)) { result |= MOD_WIN; }
  return result;
}

QString errorForRegisterHotKey(DWORD error) {
  if (error == ERROR_HOTKEY_ALREADY_REGISTERED) {
    return QStringLiteral("This shortcut is already in use by another application");
  }
  return QStringLiteral("RegisterHotKey failed (%1)").arg(error);
}

} // namespace

WindowsGlobalShortcutBackend::~WindowsGlobalShortcutBackend() {
  if (m_started && QCoreApplication::instance()) {
    QCoreApplication::instance()->removeNativeEventFilter(this);
  }
  unbindAll();
}

bool WindowsGlobalShortcutBackend::start() {
  if (m_started) { return true; }

  auto *app = QCoreApplication::instance();
  if (!app) { return false; }

  app->installNativeEventFilter(this);
  m_started = true;
  emit ready();
  return true;
}

std::expected<void, QString>
WindowsGlobalShortcutBackend::bindShortcut(const GlobalShortcutRequest &request) {
  unbindShortcut(request.id);

  const auto virtualKey = windowsVirtualKeyForQtKey(request.trigger.key());
  if (!virtualKey) { return std::unexpected(QStringLiteral("unsupported or invalid trigger")); }

  const int nativeId = m_nextNativeId++;
  if (!RegisterHotKey(nullptr, nativeId, windowsModifiers(request.trigger.mods()), *virtualKey)) {
    return std::unexpected(errorForRegisterHotKey(GetLastError()));
  }

  m_bindings.emplace(request.id, Binding{.id = request.id, .nativeId = nativeId});
  m_idByNativeId.emplace(nativeId, request.id);
  return {};
}

void WindowsGlobalShortcutBackend::unbindShortcut(const QString &id) {
  const auto it = m_bindings.find(id);
  if (it == m_bindings.end()) { return; }

  UnregisterHotKey(nullptr, it->second.nativeId);
  m_idByNativeId.erase(it->second.nativeId);
  m_bindings.erase(it);
}

void WindowsGlobalShortcutBackend::unbindAll() {
  for (const auto &[id, binding] : m_bindings) {
    UnregisterHotKey(nullptr, binding.nativeId);
  }
  m_bindings.clear();
  m_idByNativeId.clear();
}

bool WindowsGlobalShortcutBackend::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) {
  if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") { return false; }

  auto *msg = static_cast<MSG *>(message);
  if (!msg || msg->message != WM_HOTKEY) { return false; }

  const auto nativeId = static_cast<int>(msg->wParam);
  if (const auto it = m_idByNativeId.find(nativeId); it != m_idByNativeId.end()) {
    emit shortcutActivated(it->second, msg->time);
    return true;
  }

  return false;
}
