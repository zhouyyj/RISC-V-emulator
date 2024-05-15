#pragma once

#include <stdint.h>

void resetIO();

void decodePuts(char* str);

void termPuts(char* str);

void updateReg(uint32_t pc, uint32_t reg[32]);

void updateCharBuf();

int readKeys();
void updateLEDs();
int readSwitches();
