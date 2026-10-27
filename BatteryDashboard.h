#pragma once

void BatteryDashboard_Create(void);
void BatteryDashboard_Loop(void);
void BatteryDashboard_SetTransport(bool wifi);
void BatteryDashboard_SetNetwork(bool connected);
void BatteryDashboard_Update(int level, bool connected, bool charging, bool fresh);
void BatteryDashboard_UpdateDevice(int device, int level, bool connected, int charging, bool fresh);
