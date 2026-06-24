#pragma once
#include "services/startup/startup-launch-service.hpp"

class DummyStartupLaunchService : public StartupLaunchService {
public:
  bool isSupported() const override { return false; }
  bool isEnabled() const override { return false; }
  bool setEnabled(bool) override { return false; }
};
