// The parts of Setup (setup.c) for its pages in other files (the Wi-Fi page): the widgets of the
// FT812 commands the firmware used, the interface of a page.
#pragma once
#include <stdint.h>
#include "ui.h"

#define TAB_H 32                 // a button
#define FIELD_H 28               // a text field (FT_PAGE_FIELD_H)
#define ERROR_COLOR 0xff4040
#define OK_COLOR 0x40d040
#define CURSOR_COLOR 0x00d8d8
#define HIT_PAGE 1000            // the clickable areas of a page: HIT_PAGE + its id

// a page after the settings (firmware ft_page_t): drawn at x, y, w wide, h high; focused: it has the keys
typedef struct {
  void (*open)(void);            // the keys go to it
  void (*close)(void);           // back to the sections
  void (*show)(void);            // its section is chosen (it may start work: the Wi-Fi status)
  void (*hide)(void);            // another section
  void (*draw)(int x, int y, int w, int h, int focused);
  int (*key)(const key_ev_t *e); // 0: back to the sections
  int (*tick)(void);             // 1: draw again
  void (*click)(int id);         // id of the page (HIT_PAGE + id)
  const char *(*hint)(void);
  const char *(*keys)(void);
} cpage_t;

// ft_draw_rect: line width r, so the rectangle grows by r on each side
void ft_rect(int x, int y, int w, int h, int r, uint32_t c, int32_t surface);
void button(int x, int y, int w, int h, const font_t *f, uint32_t fg, uint32_t bg, int32_t surface, const char *label);
void hover_frame(int x, int y, int w, int h);
void marker(int x, int y, int h, uint32_t surface);
// the row of a setting: focused (the marker) or hovered, on the button colour; returns the surface
int32_t row_back(int x, int y, int w, int h, int focused, int hovered);
// a button of a page (ft_page_button): lit when focused on a focused page, a hover frame
void page_button(int x, int y, int w, const char *label, int focused, int focused_page, int id);
