#pragma once
#include <memory>
#include "services/startup/startup-launch-service.hpp"

std::unique_ptr<StartupLaunchService> createStartupLaunchService();
