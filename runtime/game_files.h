#pragma once
#include "ppc_recomp_shared.h"
#include <string>
namespace gh2::files {
void initialize(const std::string& root);
void shutdown();
void report();
bool service(const std::string& name, PPCContext& context, uint8_t* base);
}
