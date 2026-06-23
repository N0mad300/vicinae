#include "file-chooser.hpp"
#include <cstdlib>

FileChooser::FileChooser(QObject *parent)
    : QObject(parent)
#ifdef Q_OS_LINUX
      ,
      m_xdp(this)
#endif
{
#ifdef Q_OS_LINUX
  connect(&m_xdp, &AbstractFileChooser::filesChosen, this, &FileChooser::filesChosen);
  connect(&m_xdp, &AbstractFileChooser::rejected, this, &FileChooser::rejected);
#endif
}

bool FileChooser::isAvailable() const {
#ifdef Q_OS_LINUX
  if (std::getenv("VICINAE_FORCE_QT_DIALOG") != nullptr) return false;
  return m_xdp.isAvailable();
#else
  return false;
#endif
}

bool FileChooser::open(const FileChooserOptions &options) {
#ifdef Q_OS_LINUX
  return m_xdp.open(options);
#else
  (void)options;
  return false;
#endif
}

void FileChooser::close() {
#ifdef Q_OS_LINUX
  m_xdp.close();
#endif
}