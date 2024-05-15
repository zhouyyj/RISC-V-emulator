#include <stdio.h>
#include <string.h>

#include "io.h"

#ifdef __NIOS2__
#define PB_EDGECAPTURE ((volatile int *)0xFF20005C)
#define SWITCHES ((volatile int *)0xFF200040)
#define RLEDs ((volatile int *)0xFF200000)

#define CHAR_W 80
#define CHAR_H 30
#define DECODE_W 40
#define TERM_W 40
#define TERM_H 21

char char_buf[CHAR_H][CHAR_W];
int row_count_l = 0;
int row_count_r = 0;

/* set a single pixel on the screen at x,y
 * x in [0,319], y in [0,239], and colour in [0,65535]
 */
void write_pixel(int x, int y, short colour) {
  volatile short *vga_addr =
      (volatile short *)(0x08000000 + (y << 10) + (x << 1));
  *vga_addr = colour;
}
/* write a single character to the character buffer at x,y
 * x in [0,79], y in [0,59]
 */
void write_char(int x, int y, char c) {
  // VGA character buffer
  volatile char *character_buffer = (char *)(0x09000000 + (y << 7) + x);
  *character_buffer = c;
}
/* use write_pixel to set entire screen to black (does not clear the character
 * buffer) */
void clear_screen() {
  int x, y;
  for (x = 0; x < 320; x++) {
    for (y = 0; y < 240; y++) {
      write_pixel(x, y, 0);
    }
  }

  row_count_l = 0;
  row_count_r = 0;
  for (int i = 0; i < CHAR_H; ++i) {
    for (int j = 0; j < CHAR_W; ++j) {
      char_buf[i][j] = 0;
      write_char(j, i*2, 0);
      write_char(j, i*2+1, 0);
    }
  }
}
#endif

void updateCharBuf() {
#ifdef __NIOS2__
  for (int i = 0; i < CHAR_H; ++i) {
    for (int j = 0; j < CHAR_W; ++j) {
      write_char(j, i*2, char_buf[i][j]);
    }
  }
#endif
}

void resetIO(){
#ifdef __NIOS2__
  clear_screen();
  *PB_EDGECAPTURE = -1;
#endif
}

// void clearTerm(){
// #ifdef __NIOS2__
//   row_count_r = 0;
//   for (int i = 0; i < TERM_H-1; ++i) {
//     for (int j = DECODE_W; j < CHAR_W-1; ++j) {
//       char_buf[i][j] = 0;
//     }
//   }
//   updateCharBuf();
// #endif
// }

void decodePuts(char* str) {
#ifdef __NIOS2__
  if (row_count_l < TERM_H - 1) {
    row_count_l += 1;
  } else {
    for (int i = 1; i < TERM_H-1; ++i){
      memcpy(&char_buf[i], &char_buf[i + 1], DECODE_W);
    }
  }
  char_buf[row_count_l-1][0] = '\0';
  strncat((char *)&char_buf[row_count_l-1], str, DECODE_W - 1);
#endif
  puts(str);
}

void termPuts(char* str) {
  #ifdef __NIOS2__
  if (row_count_r < TERM_H - 1) {
    row_count_r += 1;
  } else {
    for (int i = 0; i < TERM_H - 1; ++i) {
      memcpy(&char_buf[i][DECODE_W], &char_buf[i + 1][DECODE_W], TERM_W);
    }
  }
  char_buf[row_count_r-1][DECODE_W] = '\0';
  strncat((char *)&char_buf[row_count_r-1]+DECODE_W, str, TERM_W - 1);
#endif
  puts(str);
}

void updateReg(uint32_t pc, uint32_t reg[32]) {
#ifdef __NIOS2__
  sprintf(&char_buf[TERM_H][0], "pc  %08x", pc);
  sprintf(&char_buf[TERM_H][20], "Reg Value");
  sprintf(&char_buf[TERM_H][40], "Reg Value");
  sprintf(&char_buf[TERM_H][60], "Reg Value");
  for (int i = 0; i < 8; ++i) {
    sprintf(&char_buf[TERM_H+1+i][0], "x%-2d %08x", i*4, reg[i*4]);
    sprintf(&char_buf[TERM_H+1+i][20], "x%-2d %08x", i*4+1, reg[i*4+1]);
    sprintf(&char_buf[TERM_H+1+i][40], "x%-2d %08x", i*4+2, reg[i*4+2]);
    sprintf(&char_buf[TERM_H+1+i][60], "x%-2d %08x", i*4+3, reg[i*4+3]);
  }
#endif
}

int readKeys(){
#ifdef __NIOS2__
  int PBreleases = *PB_EDGECAPTURE;
  *PB_EDGECAPTURE = -1;
  return PBreleases;
#endif
  return 0;
}

void updateLEDs() {
#ifdef __NIOS2__
  *RLEDs = *SWITCHES;
#endif
}

int readSwitches() {
#ifdef __NIOS2__
  return *SWITCHES;
#endif
}


// #ifdef __NIOS2__
// #define CTRL_D 4
// #else
// #define CTRL_D EOF
// #endif

// #ifdef __NIOS2__
//   clear_screen();
//   int ln = 0;
// #endif

// #ifdef __NIOS2__
//     snprintf(&char_buf[ln], 40, "%-8x0x%08x    ", pc, inst_u32);
//     updateCharBuf();
//     if (ln < CHAR_H-1) ln+=2;
// #endif
