#include <QtGlobal>
#include "services/startup/dummy-startup-launch-service.hpp"
#include "services/startup/startup-launch-service-factory.hpp"
#ifdef Q_OS_WIN
#include "services/startup/windows-startup-launch-service.hpp"
#endif

std::unique_ptr<StartupLaunchService> createStartupLaunchService() {
#ifdef Q_OS_WIN
  return std::make_unique<WindowsStartupLaunchService>();
#else
  return std::make_unique<DummyStartupLaunchService>();
#endif
}
