#include "dummy-calculator-backend.hpp"
#include <QtConcurrent/QtConcurrent>

AbstractCalculatorBackend::ComputeResult DummyCalculatorBackend::compute(const QString &,
                                                                         const ComputeOptions &) {
  return std::unexpected(CalculatorError("Calculator backend unavailable"));
}

QFuture<AbstractCalculatorBackend::ComputeResult>
DummyCalculatorBackend::asyncCompute(const QString &question, const ComputeOptions &opts) {
  return QtFuture::makeReadyValueFuture(compute(question, opts));
}