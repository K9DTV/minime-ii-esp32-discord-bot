#ifndef DISPLAY_LAYOUT_H
#define DISPLAY_LAYOUT_H

// LCD landscape paint coords (480x320). Single place for chips, panels, rows, hit pads.
// Native panel is 320x480 portrait (LCD_NATIVE_* in minime_config.h).
//
// Layout invariants:
//   - Brand bar height = LOGO_TOP_PAD + K9DTV_LOGO_H + LOGO_BOTTOM_GAP
//   - Theme chip in left logo gap; layout chip in right logo gap; hit boxes set in placeChip
//   - Controls slider/toggle hit boxes filled during draw (not from these enums);
//     dashForceFull on layout change refreshes them before touch (see ui_controls.cpp)
//   - Left panel [PANEL_LEFT_X, PANEL_BOTTOM_Y); right panel [PANEL_RIGHT_X, PANEL_BOTTOM_Y)
//   - PANEL_RIGHT_X == PANEL_LEFT_X + PANEL_LEFT_W + PANEL_GAP

enum {
  LCD_LANDSCAPE_W = 480,
  LCD_LANDSCAPE_H = 320,

  LOGO_TOP_PAD = 0,
  LOGO_BOTTOM_GAP = 4,
  MENU_CHIP_S = 44,
  MENU_CHIP_VIEWBOX = 32, // SVG viewBox; drawMenuChip scales to MENU_CHIP_S

  // placeChip hit box relative to chip origin / label
  CHIP_HIT_PAD_L = 4,
  CHIP_HIT_PAD_T = 2,
  CHIP_HIT_EXTRA_W = 8,
  CHIP_LABEL_GAP_Y = 1,
  CHIP_LABEL_TEXT_H = 10,

  PANEL_GAP = 4,
  PANEL_LEFT_X = 4,
  PANEL_LEFT_W = 234,
  PANEL_RIGHT_X = 242, // PANEL_LEFT_X + PANEL_LEFT_W + PANEL_GAP
  PANEL_RIGHT_W = 234,
  PANEL_BOTTOM_Y_FULL = 318, // bottom of both panels
  // Controls dogs live inside each panel (Cancel=left, Save=right) — no strip below
  FOOTER_H = 0,
  PANEL_BOTTOM_Y_CTRL = 318, // same as FULL (legacy name)
  PANEL_CORNER_R = 4,
  PANEL_PAD = 6,
  PANEL_PAD_BOTTOM = 4,
  PANEL_CONTENT_TOP = 5,

  DOG_BTN_W = 48,         // mark dog+K9 width (no button chrome)
  DOG_BTN_H_ICON = 32,    // mark dog+K9 height
  DOG_BTN_PAD_X = 8,
  DOG_BTN_H = 42,         // icon + label under dog outline


  USER_PITCH = 9,           // line baseline step (unchanged); 22 rows leave margin at bottom
  USER_TEXT_SIZE = 1,       // GFX size 1 @ pitch 9; web Users font is bumped separately
  DASH_LOG_ROWS = 55,       // LOG + Serial ring depth (LCD snap + web rings)
  LCD_BAR_MAX = 150,

  ROW_PITCH = 10,
  ROW_PITCH_LOOSE = 12,
  ROW_PITCH_UP_T = 11,

  MLINE_VALUE_X = 40,
  MLINE_BAR_MIN_X = 100,
  MLINE_BAR_MIN_W = 24,
  MLINE_BAR_FRAME_H = 10,
  MLINE_BAR_FILL_H = 8,

  SYS_VALUE_X = 56,
  EVENT_VALUE_X = 42,
  USERS_COL_X = 100,
  USER_STATUS_COL_X = 100,
  USER_BOT_COL_X = 168
};

#endif
