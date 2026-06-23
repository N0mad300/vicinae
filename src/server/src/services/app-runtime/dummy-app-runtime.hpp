#pragma once
#include "services/app-runtime/abstract-app-runtime.hpp"

class DummyAppRuntime : public AbstractAppRuntime {
public:
  bool isRunning(const AbstractApplication &) const override { return false; }
  std::shared_ptr<AbstractApplication> frontmostApp() const override { return nullptr; }
  bool activate(const AbstractApplication &) const override { return false; }
};