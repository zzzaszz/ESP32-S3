#pragma once
#include <stdint.h>

void DisplayOrientation_Init();
void DisplayOrientation_Loop();
void DisplayOrientation_PrintDiagnostics();
bool DisplayOrientation_Command(const char *command);
bool DisplayOrientation_IsReversed();
uint8_t DisplayOrientation_Mode();
