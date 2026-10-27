#pragma once

#include <stdint.h>

void BatteryDashboard_Create(void);
void BatteryDashboard_Loop(void);
void BatteryDashboard_SetTransport(bool wifi);
void BatteryDashboard_SetNetwork(bool connected);
void BatteryDashboard_Update(int level, bool connected, bool charging, bool fresh);
void BatteryDashboard_UpdateDevice(int device, int level, bool connected, int charging, bool fresh);
void BatteryDashboard_UpdatePerformance(int cpu, int memory, int gpu, int gpuTemperature,
                                        uint32_t uploadKbps, uint32_t downloadKbps,
                                        uint32_t diskFreeGb, uint32_t uptimeSeconds);
