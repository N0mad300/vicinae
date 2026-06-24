#pragma once
#include "services/startup/startup-launch-service.hpp"

class WindowsStartupLaunchService : public StartupLaunchService {
public:
  bool isEnabled() const override;
  bool setEnabled(bool enabled) override;
};
