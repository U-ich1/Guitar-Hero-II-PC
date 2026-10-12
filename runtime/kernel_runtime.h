#pragma once
#include "ppc_recomp_shared.h"
#include <cstdint>
#include <string>

namespace gh2::kernel {
inline constexpr uint32_t state_base = 0x60000000;
inline constexpr uint32_t module_cell = state_base;
inline constexpr uint32_t module_record = state_base + 0x100;
inline constexpr uint32_t xex_header = state_base + 0x10000;
inline constexpr uint32_t thread_record = state_base + 0x20000;
inline constexpr uint32_t pcr_record = state_base + 0x21000;
inline constexpr uint32_t tls_block = state_base + 0x22000;
void initialize(uint8_t* base, PPCContext& context, const std::string& xex_path);
void shutdown();
void report();
void checkpoint();
void import(const char* name, PPCContext& context, uint8_t* base);
uint32_t read32(const uint8_t* base, uint32_t address);
void write32(uint8_t* base, uint32_t address, uint32_t value);
uint32_t query_header(uint8_t* base, uint32_t header, uint32_t key);
bool signal_event(uint32_t handle, bool reset);
}
