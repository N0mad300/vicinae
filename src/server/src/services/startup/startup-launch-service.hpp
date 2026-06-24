#pragma once

class StartupLaunchService {
public:
  virtual ~StartupLaunchService() = default;

  virtual bool isSupported() const { return true; }
  virtual bool isEnabled() const = 0;
  virtual bool setEnabled(bool enabled) = 0;
};
