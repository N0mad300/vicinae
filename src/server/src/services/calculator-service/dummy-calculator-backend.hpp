#pragma once
#include "services/calculator-service/abstract-calculator-backend.hpp"

class DummyCalculatorBackend : public AbstractCalculatorBackend {
public:
  QString id() const override { return "dummy"; }
  QString displayName() const override { return "Disabled"; }
  bool isActivatable() const override { return true; }

  ComputeResult compute(const QString &question, const ComputeOptions &opts) override;
  QFuture<ComputeResult> asyncCompute(const QString &question, const ComputeOptions &opts) override;
};