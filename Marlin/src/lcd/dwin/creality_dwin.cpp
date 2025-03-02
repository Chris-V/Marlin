/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

/**
 * DWIN by Creality3D
 * Rewrite and Extui Port by Jacob Myers
 */

#include "../../inc/MarlinConfigPre.h"

#if ENABLED(DWIN_CREALITY_LCD)

#include "creality_dwin.h"

#include "../marlinui.h"
#include "../../MarlinCore.h"

#include "../../module/temperature.h"
#include "../../module/planner.h"
#include "../../module/settings.h"
#include "../../libs/buzzer.h"
#include "../../inc/Conditionals_post.h"

#if ENABLED(ADVANCED_PAUSE_FEATURE)
  #include "../../feature/pause.h"
#endif

#if ENABLED(FILAMENT_RUNOUT_SENSOR)
  #include "../../feature/runout.h"
#endif

#if ENABLED(HOST_ACTION_COMMANDS)
  #include "../../feature/host_actions.h"
#endif

#if ANY(AUTO_BED_LEVELING_BILINEAR, AUTO_BED_LEVELING_LINEAR, AUTO_BED_LEVELING_3POINT) && DISABLED(PROBE_MANUALLY)
  #define HAS_ONESTEP_LEVELING 1
#endif

#if ANY(BABYSTEPPING, HAS_BED_PROBE, HAS_WORKSPACE_OFFSET)
  #define HAS_ZOFFSET_ITEM 1
#endif

#ifndef strcasecmp_P
  #define strcasecmp_P(a, b) strcasecmp((a), (b))
#endif

#if HAS_LEVELING
  #include "../../feature/bedlevel/bedlevel.h"
#endif

#if ENABLED(AUTO_BED_LEVELING_UBL)
  #include "../../libs/least_squares_fit.h"
  #include "../../libs/vector_3.h"
#endif

#if HAS_BED_PROBE
  #include "../../module/probe.h"
#endif

#if ANY(HAS_HOTEND, HAS_HEATED_BED, HAS_FAN) && PREHEAT_COUNT
  #define HAS_PREHEAT 1
#endif

#if ENABLED(POWER_LOSS_RECOVERY)
  #include "../../feature/powerloss.h"
#endif

#define MACHINE_SIZE STRINGIFY(X_BED_SIZE) "x" STRINGIFY(Y_BED_SIZE) "x" STRINGIFY(Z_MAX_POS)

#define CORP_WEBSITE_E "github.com/Jyers"

#define BUILD_NUMBER "1.3.5"

#define DWIN_FONT_MENU font8x16
#define DWIN_FONT_STAT font10x20
#define DWIN_FONT_HEAD font10x20

#define MENU_CHAR_LIMIT  24
#define STATUS_Y 352

#define MAX_PRINT_SPEED   500
#define MIN_PRINT_SPEED   10

#if HAS_FAN
  #define MAX_FAN_SPEED     255
  #define MIN_FAN_SPEED     0
#endif

#define MAX_XY_OFFSET 100

#if HAS_ZOFFSET_ITEM
  #define MAX_Z_OFFSET 9.99
  #if HAS_BED_PROBE
    #define MIN_Z_OFFSET -9.99
  #else
    #define MIN_Z_OFFSET -1
  #endif
#endif

#if HAS_HOTEND
  #define MAX_FLOW_RATE   200
  #define MIN_FLOW_RATE   10

  #define MAX_E_TEMP    (HEATER_0_MAXTEMP - HOTEND_OVERSHOOT)
  #define MIN_E_TEMP    0
#endif

#if HAS_HEATED_BED
  #define MAX_BED_TEMP  BED_MAXTEMP
  #define MIN_BED_TEMP  0
#endif

constexpr uint16_t TROWS = 6, MROWS = TROWS - 1,
                   TITLE_HEIGHT = 30,
                   MLINE = 53,
                   LBLX = 60,
                   MENU_CHR_W = 8, MENU_CHR_H = 16, STAT_CHR_W = 10;

#define MBASE(L) (49 + MLINE * (L))

constexpr float default_max_feedrate[]        = DEFAULT_MAX_FEEDRATE;
constexpr float default_max_acceleration[]    = DEFAULT_MAX_ACCELERATION;
constexpr float default_steps[]               = DEFAULT_AXIS_STEPS_PER_UNIT;
#if HAS_CLASSIC_JERK
  constexpr float default_max_jerk[]            = { DEFAULT_XJERK, DEFAULT_YJERK, DEFAULT_ZJERK, DEFAULT_EJERK };
#endif

uint8_t active_menu = MainMenu;
uint8_t last_menu = MainMenu;
uint8_t selection = 0;
uint8_t last_selection = 0;
uint8_t scrollpos = 0;
uint8_t process = Main;
uint8_t last_process = Main;
PopupID popup;
PopupID last_popup;

void (*funcpointer)() = nullptr;
void *valuepointer = nullptr;
float tempvalue;
float valuemin;
float valuemax;
uint8_t valueunit;
uint8_t valuetype;

char cmd[MAX_CMD_SIZE+16], str_1[16], str_2[16], str_3[16];
char statusmsg[64];
char filename[LONG_FILENAME_LENGTH];
bool printing = false;
bool paused = false;
bool sdprint = false;

int16_t pausetemp, pausebed, pausefan;

bool livemove = false;
bool liveadjust = false;
uint8_t preheatmode = 0;
float zoffsetvalue = 0;
uint8_t gridpoint;
float corner_avg;
float corner_pos;

bool probe_deployed = false;

CrealityDWINClass CrealityDWIN;

#if HAS_MESH
  struct Mesh_Settings {
    bool viewer_asymmetric_range = false;
    bool viewer_print_value = false;
    bool goto_mesh_value = false;
    bool drawing_mesh = false;
    uint8_t mesh_x = 0;
    uint8_t mesh_y = 0;

    #if ENABLED(AUTO_BED_LEVELING_UBL)
      bed_mesh_t &mesh_z_values = ubl.z_values;
      uint8_t tilt_grid = 1;

      void manual_value_update(bool undefined=false) {
        sprintf_P(cmd, PSTR("M421 I%i J%i Z%s %s"), mesh_x, mesh_y, dtostrf(current_position.z, 1, 3, str_1), undefined ? "N" : "");
        gcode.process_subcommands_now_P(cmd);
        planner.synchronize();
      }

      bool create_plane_from_mesh() {
        struct linear_fit_data lsf_results;
        incremental_LSF_reset(&lsf_results);
        GRID_LOOP(x, y) {
          if (!isnan(mesh_z_values[x][y])) {
            xy_pos_t rpos;
            rpos.x = ubl.mesh_index_to_xpos(x);
            rpos.y = ubl.mesh_index_to_ypos(y);
            incremental_LSF(&lsf_results, rpos, mesh_z_values[x][y]);
          }
        }
        
        if (finish_incremental_LSF(&lsf_results)) {
          SERIAL_ECHOPGM("Could not complete LSF!");
          return true;
        }

        ubl.set_all_mesh_points_to_value(0);

        matrix_3x3 rotation = matrix_3x3::create_look_at(vector_3(lsf_results.A, lsf_results.B, 1));
        GRID_LOOP(i, j) {
          float mx = ubl.mesh_index_to_xpos(i),
                my = ubl.mesh_index_to_ypos(j),
                mz = mesh_z_values[i][j];

          if (DEBUGGING(LEVELING)) {
            DEBUG_ECHOPAIR_F("before rotation = [", mx, 7);
            DEBUG_CHAR(',');
            DEBUG_ECHO_F(my, 7);
            DEBUG_CHAR(',');
            DEBUG_ECHO_F(mz, 7);
            DEBUG_ECHOPGM("]   ---> ");
            DEBUG_DELAY(20);
          }

          rotation.apply_rotation_xyz(mx, my, mz);

          if (DEBUGGING(LEVELING)) {
            DEBUG_ECHOPAIR_F("after rotation = [", mx, 7);
            DEBUG_CHAR(',');
            DEBUG_ECHO_F(my, 7);
            DEBUG_CHAR(',');
            DEBUG_ECHO_F(mz, 7);
            DEBUG_ECHOLNPGM("]");
            DEBUG_DELAY(20);
          }

          mesh_z_values[i][j] = mz - lsf_results.D;
        }
        return false;
      }

    #else
      bed_mesh_t &mesh_z_values = z_values;

      void manual_value_update() {
        sprintf_P(cmd, PSTR("G29 I%i J%i Z%s"), mesh_x, mesh_y, dtostrf(current_position.z, 1, 3, str_1));
        gcode.process_subcommands_now_P(cmd);
        planner.synchronize();
      }

    #endif

    void manual_move(bool zmove=false) {
      if (zmove) {
        planner.synchronize();
        current_position.z = goto_mesh_value ? mesh_z_values[mesh_x][mesh_y] : Z_CLEARANCE_BETWEEN_PROBES;
        planner.buffer_line(current_position, homing_feedrate(Z_AXIS), active_extruder);
        planner.synchronize();
      }
      else {
        CrealityDWIN.Popup_Handler(MoveWait);
        sprintf_P(cmd, PSTR("G0 F300 Z%s"), dtostrf(Z_CLEARANCE_BETWEEN_PROBES, 1, 3, str_1));
        gcode.process_subcommands_now_P(cmd);
        sprintf_P(cmd, PSTR("G42 F4000 I%i J%i"), mesh_x, mesh_y);
        gcode.process_subcommands_now_P(cmd);
        planner.synchronize();
        current_position.z = goto_mesh_value ? mesh_z_values[mesh_x][mesh_y] : Z_CLEARANCE_BETWEEN_PROBES;
        planner.buffer_line(current_position, homing_feedrate(Z_AXIS), active_extruder);
        planner.synchronize();
        CrealityDWIN.Redraw_Menu();
      }
    }

    float get_max_value() {
      float max = __FLT_MIN__;
      GRID_LOOP(x, y) {
        if (!isnan(mesh_z_values[x][y]) && mesh_z_values[x][y] > max)
          max = mesh_z_values[x][y];
      }
      return max;
    }

    float get_min_value() {
      float min = __FLT_MAX__;
      GRID_LOOP(x, y) {
        if (!isnan(mesh_z_values[x][y]) && mesh_z_values[x][y] < min)
          min = mesh_z_values[x][y];
      }
      return min;
    }

    void Draw_Bed_Mesh(int16_t selected = -1, uint8_t gridline_width = 1, uint16_t padding_x = 8, uint16_t padding_y_top = 40 + 53 - 7) {
      drawing_mesh = true;
      const uint16_t total_width_px = DWIN_WIDTH - padding_x - padding_x;
      const uint16_t cell_width_px  = total_width_px / GRID_MAX_POINTS_X;
      const uint16_t cell_height_px = total_width_px / GRID_MAX_POINTS_Y;
      const float v_max = abs(get_max_value()), v_min = abs(get_min_value()), range = max(v_min, v_max);

      // Clear background from previous selection and select new square
      DWIN_Draw_Rectangle(1, Color_Bg_Black, max(0, padding_x - gridline_width), max(0, padding_y_top - gridline_width), padding_x + total_width_px, padding_y_top + total_width_px);
      if (selected >= 0) {
        const auto selected_y = selected / GRID_MAX_POINTS_X;
        const auto selected_x = selected - (GRID_MAX_POINTS_X * selected_y);
        const auto start_y_px = padding_y_top + selected_y * cell_height_px;
        const auto start_x_px = padding_x + selected_x * cell_width_px;
        DWIN_Draw_Rectangle(1, Color_White, max(0, start_x_px - gridline_width), max(0, start_y_px - gridline_width), start_x_px + cell_width_px, start_y_px + cell_height_px);
      }

      // Draw value square grid
      char buf[8];
      GRID_LOOP(x, y) {
        const auto start_x_px = padding_x + x * cell_width_px;
        const auto end_x_px   = start_x_px + cell_width_px - 1 - gridline_width;
        const auto start_y_px = padding_y_top + (GRID_MAX_POINTS_Y - y - 1) * cell_height_px;
        const auto end_y_px   = start_y_px + cell_height_px - 1 - gridline_width;
        DWIN_Draw_Rectangle(1,        // RGB565 colors: http://www.barth-dev.de/online/rgb565-color-picker/
          isnan(mesh_z_values[x][y]) ? Color_Grey : (                                                              // gray if undefined
            (mesh_z_values[x][y] < 0 ? 
              (uint16_t)round(0b11111  * -mesh_z_values[x][y] / (!viewer_asymmetric_range ? range : v_min)) << 11 : // red if mesh point value is negative
              (uint16_t)round(0b111111 *  mesh_z_values[x][y] / (!viewer_asymmetric_range ? range : v_max)) << 5) | // green if mesh point value is positive
                min(0b11111, (((uint8_t)abs(mesh_z_values[x][y]) / 10) * 4))),                                     // + blue stepping for every mm
          start_x_px, start_y_px, end_x_px, end_y_px);
        while (LCD_SERIAL.availableForWrite() < 32) { // wait for serial to be available without blocking and resetting the MCU 
          gcode.process_subcommands_now_P("G4 P10");
          planner.synchronize();
        } 
        // Draw value text on 
        if (viewer_print_value) { 
          gcode.process_subcommands_now_P("G4 P10");  // still fails without additional delay...
          planner.synchronize();
          int8_t offset_x, offset_y = cell_height_px / 2 - 6;
          if (isnan(mesh_z_values[x][y])) {  // undefined
            DWIN_Draw_String(false, false, font6x12, Color_White, Color_Bg_Blue, start_x_px + cell_width_px / 2 - 5, start_y_px + offset_y, F("X"));
          }
          else {                          // has value
            if (GRID_MAX_POINTS_X < 10) {
              sprintf(buf, "%s", dtostrf(abs(mesh_z_values[x][y]), 1, 2, str_1));
            }
            else {
              sprintf(buf, "%02i", (uint16_t)(abs(mesh_z_values[x][y] - (int16_t)mesh_z_values[x][y]) * 100));
            }
            offset_x = cell_width_px / 2 - 3 * (strlen(buf)) - 2;
            if (!(GRID_MAX_POINTS_X < 10))
              DWIN_Draw_String(false, false, font6x12, Color_White, Color_Bg_Blue, start_x_px-2 + offset_x, start_y_px + offset_y /*+ square / 2 - 6*/, F("."));
            DWIN_Draw_String(false, false, font6x12, Color_White, Color_Bg_Blue, start_x_px+1 + offset_x, start_y_px + offset_y /*+ square / 2 - 6*/, buf);
          }
        }
      }
    }

    void Set_Mesh_Viewer_Status() { // TODO: draw gradient with values as a legend instead
      float v_max = abs(get_max_value()), v_min = abs(get_min_value()), range = max(v_min, v_max);
      if (v_min > 3e+10F) v_min = 0.0000001;
      if (v_max > 3e+10F) v_max = 0.0000001;
      if (range > 3e+10F) range = 0.0000001;
      char msg[32];
      if (viewer_asymmetric_range) {
        sprintf(msg, "Red %s..0..%s Green", dtostrf(-v_min, 1, 3, str_1), dtostrf(v_max, 1, 3, str_2));
      }
      else {
        sprintf(msg, "Red %s..0..%s Green", dtostrf(-range, 1, 3, str_1), dtostrf(range, 1, 3, str_2));
      }
      CrealityDWIN.Update_Status(msg);
      drawing_mesh = false;
    }

  };
  Mesh_Settings mesh_conf;
#endif

/* General Display Functions */

// Clear a part of the screen
//  4=Entire screen
//  3=Title bar and Menu area (default)
//  2=Menu area
//  1=Title bar
void CrealityDWINClass::Clear_Screen(uint8_t e/*=3*/) {
  if (e==1||e==3||e==4) DWIN_Draw_Rectangle(1, GetColor(eeprom_settings.menu_top_bg, Color_Bg_Blue, false), 0, 0, DWIN_WIDTH, TITLE_HEIGHT); // Clear Title Bar
  if (e==2||e==3) DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, 31, DWIN_WIDTH, STATUS_Y); // Clear Menu Area
  if (e==4) DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, 31, DWIN_WIDTH, DWIN_HEIGHT); // Clear Popup Area
}

void CrealityDWINClass::Draw_Float(float value, uint8_t row, bool selected/*=false*/, uint8_t minunit/*=10*/) {
  const uint8_t digits = (uint8_t)floor(log10(abs(value))) + log10(minunit) + (minunit > 1);
  const uint16_t bColor = (selected) ? Select_Color : Color_Bg_Black;
  const uint16_t xpos = 240 - (digits * 8);
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 194, MBASE(row), 234 - (digits * 8), MBASE(row)+16);
  if (isnan(value)) {
    DWIN_Draw_String(false, true, DWIN_FONT_MENU, Color_White, bColor, xpos - 8, MBASE(row), F(" NaN"));
  } else if (value < 0) {
    DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_MENU, Color_White, bColor, digits-log10(minunit)+1, log10(minunit), xpos, MBASE(row), -value * minunit);
    DWIN_Draw_String(false, true, DWIN_FONT_MENU, Color_White, bColor, xpos - 8, MBASE(row), F("-"));
  }
  else {
    DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_MENU, Color_White, bColor, digits-log10(minunit)+1, log10(minunit), xpos, MBASE(row), value * minunit);
    DWIN_Draw_String(false, true, DWIN_FONT_MENU, Color_White, bColor, xpos - 8, MBASE(row), F(" "));
  }
}

void CrealityDWINClass::Draw_Option(uint8_t value, const char * const * options, uint8_t row, bool selected/*=false*/, bool color/*=false*/) {
  uint16_t bColor = (selected) ? Select_Color : Color_Bg_Black;
  uint16_t tColor = (color) ? GetColor(value, Color_White, false) : Color_White;
  DWIN_Draw_Rectangle(1, bColor, 202, MBASE(row) + 14, 258, MBASE(row) - 2);
  DWIN_Draw_String(false, false, DWIN_FONT_MENU, tColor, bColor, 202, MBASE(row) - 1, options[value]);
}

uint16_t CrealityDWINClass::GetColor(uint8_t color, uint16_t original, bool light/*=false*/) {
  switch (color){
    case Default:
      return original;
      break;
    case White:
      return (light) ? Color_Light_White : Color_White;
      break;
    case Green:
      return (light) ? Color_Light_Green : Color_Green;
      break;
    case Cyan:
      return (light) ? Color_Light_Cyan : Color_Cyan;
      break; 
    case Blue:
      return (light) ? Color_Light_Blue : Color_Blue;
      break;
    case Magenta:
      return (light) ? Color_Light_Magenta : Color_Magenta;
      break;
    case Red:
      return (light) ? Color_Light_Red : Color_Red;
      break;
    case Orange:
      return (light) ? Color_Light_Orange : Color_Orange;
      break;  
    case Yellow:
      return (light) ? Color_Light_Yellow : Color_Yellow;
      break;
    case Brown:
      return (light) ? Color_Light_Brown : Color_Brown;
      break;          
    case Black:
      return Color_Black;
      break;                             
  }
  return Color_White;
}

void CrealityDWINClass::Draw_Title(const char * title) {
  DWIN_Draw_String(false, false, DWIN_FONT_HEAD, GetColor(eeprom_settings.menu_top_txt, Color_White, false), Color_Bg_Blue, (DWIN_WIDTH - strlen(title) * STAT_CHR_W) / 2, 5, title);
}

void CrealityDWINClass::Draw_Menu_Item(uint8_t row, uint8_t icon/*=0*/, const char * label1, const char * label2, bool more/*=false*/, bool centered/*=false*/) {
  const uint8_t label_offset_y = !(label1 && label2) ? 0 : MENU_CHR_H * 3 / 5;
  const uint8_t label1_offset_x = !centered ? LBLX : LBLX * 4/5 + max(LBLX * 1U/5, (DWIN_WIDTH - LBLX - (label1 ? strlen(label1) : 0) * MENU_CHR_W) / 2);
  const uint8_t label2_offset_x = !centered ? LBLX : LBLX * 4/5 + max(LBLX * 1U/5, (DWIN_WIDTH - LBLX - (label2 ? strlen(label2) : 0) * MENU_CHR_W) / 2);
  if (label1) DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Black, label1_offset_x, MBASE(row) - 1 - label_offset_y, label1); // Draw Label
  if (label2) DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Black, label2_offset_x, MBASE(row) - 1 + label_offset_y, label2); // Draw Label
  if (icon) DWIN_ICON_Show(ICON, icon, 26, MBASE(row) - 3);   //Draw Menu Icon
  if (more) DWIN_ICON_Show(ICON, ICON_More, 226, MBASE(row) - 3); // Draw More Arrow
  DWIN_Draw_Line(GetColor(eeprom_settings.menu_split_line, Line_Color, true), 16, MBASE(row) + 33, 256, MBASE(row) + 33); // Draw Menu Line
}

void CrealityDWINClass::Draw_Checkbox(uint8_t row, bool value) {
  #if ENABLED(DWIN_CREALITY_LCD_CUSTOM_ICONS) // Draw appropriate checkbox icon
    DWIN_ICON_Show(ICON, (value ? ICON_Checkbox_T : ICON_Checkbox_F), 226, MBASE(row) - 3); 
  #else                                         // Draw a basic checkbox using rectangles and lines
    DWIN_Draw_Rectangle(1, Color_Bg_Black, 226, MBASE(row) - 3, 226 + 20, MBASE(row) - 3 + 20);
    DWIN_Draw_Rectangle(0, Color_White, 226, MBASE(row) - 3, 226 + 20, MBASE(row) - 3 + 20);
    if (value) {
      DWIN_Draw_Line(Check_Color, 227, MBASE(row) - 3 + 11, 226 + 8, MBASE(row) - 3 + 17);
      DWIN_Draw_Line(Check_Color, 227 + 8, MBASE(row) - 3 + 17, 226 + 19, MBASE(row) - 3 + 1);
      DWIN_Draw_Line(Check_Color, 227, MBASE(row) - 3 + 12, 226 + 8, MBASE(row) - 3 + 18);
      DWIN_Draw_Line(Check_Color, 227 + 8, MBASE(row) - 3 + 18, 226 + 19, MBASE(row) - 3 + 2);
      DWIN_Draw_Line(Check_Color, 227, MBASE(row) - 3 + 13, 226 + 8, MBASE(row) - 3 + 19);
      DWIN_Draw_Line(Check_Color, 227 + 8, MBASE(row) - 3 + 19, 226 + 19, MBASE(row) - 3 + 3);
    }
  #endif
}

void CrealityDWINClass::Draw_Menu(uint8_t menu, uint8_t select/*=0*/, uint8_t scroll/*=0*/) {
  if (active_menu!=menu) {
    last_menu = active_menu;
    if (process == Menu) last_selection = selection;
  }
  selection = min(select, Get_Menu_Size(menu));
  scrollpos = scroll;
  if (selection-scrollpos > MROWS)
    scrollpos = selection - MROWS;
  process = Menu;
  active_menu = menu;
  Clear_Screen();
  Draw_Title(Get_Menu_Title(menu));
  LOOP_L_N(i, TROWS) Menu_Item_Handler(menu, i + scrollpos);
  DWIN_Draw_Rectangle(1, GetColor(eeprom_settings.cursor_color, Rectangle_Color), 0, MBASE(selection-scrollpos) - 18, 14, MBASE(selection-scrollpos) + 33);
}

void CrealityDWINClass::Redraw_Menu(bool lastprocess/*=true*/, bool lastselection/*=false*/, bool lastmenu/*=false*/) {
  switch((lastprocess) ? last_process : process) {
    case Menu:
      Draw_Menu((lastmenu) ? last_menu : active_menu, (lastselection) ? last_selection : selection, (lastmenu) ? 0 : scrollpos);
      break;
    case Main:
      Draw_Main_Menu((lastselection) ? last_selection : selection);
      break;
    case Print:
      Draw_Print_Screen();
      break;
    case File:
      Draw_SD_List();
      break;
    default:
      break;
  }
}

void CrealityDWINClass::Redraw_Screen() {
  Redraw_Menu(false);
  Draw_Status_Area(true);
  Update_Status_Bar(true);
}

/* Primary Menus and Screen Elements */

void CrealityDWINClass::Main_Menu_Icons() {
  if (selection == 0) {
    DWIN_ICON_Show(ICON, ICON_Print_1, 17, 130);
    DWIN_Draw_Rectangle(0, GetColor(eeprom_settings.highlight_box, Color_White), 17, 130, 126, 229);
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 52, 200, F("Print"));
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Print_0, 17, 130);
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 52, 200, F("Print"));
  }
  if (selection == 1) {
    DWIN_ICON_Show(ICON, ICON_Prepare_1, 145, 130);
    DWIN_Draw_Rectangle(0, GetColor(eeprom_settings.highlight_box, Color_White), 145, 130, 254, 229);
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 170, 200, F("Prepare"));
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Prepare_0, 145, 130);
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 170, 200, F("Prepare"));
  }
  if (selection == 2) {
    DWIN_ICON_Show(ICON, ICON_Control_1, 17, 246);
    DWIN_Draw_Rectangle(0, GetColor(eeprom_settings.highlight_box, Color_White), 17, 246, 126, 345);
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 43, 317, F("Control"));
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Control_0, 17, 246);
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 43, 317, F("Control"));
  }
  #if ANY(HAS_ONESTEP_LEVELING, AUTO_BED_LEVELING_UBL, PROBE_MANUALLY) 
    if (selection == 3) {
      DWIN_ICON_Show(ICON, ICON_Leveling_1, 145, 246);
      DWIN_Draw_Rectangle(0, GetColor(eeprom_settings.highlight_box, Color_White), 145, 246, 254, 345);
      DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 179, 317, F("Level"));
    }
    else {
      DWIN_ICON_Show(ICON, ICON_Leveling_0, 145, 246);
      DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 179, 317, F("Level"));
    }
  #else
    if (selection == 3) {
      DWIN_ICON_Show(ICON, ICON_Info_1, 145, 246);
      DWIN_Draw_Rectangle(0, GetColor(eeprom_settings.highlight_box, Color_White), 145, 246, 254, 345);
      DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 181, 317, F("Info"));
    }
    else {
      DWIN_ICON_Show(ICON, ICON_Info_0, 145, 246);
      DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 181, 317, F("Info"));
      //DWIN_Frame_AreaCopy(1, 132, 423, 159, 435, 186, 318);
    }
  #endif
}

void CrealityDWINClass::Draw_Main_Menu(uint8_t select/*=0*/) {
  process = Main;
  active_menu = MainMenu;
  selection = select;
  Clear_Screen();
  Draw_Title(Get_Menu_Title(MainMenu));
  SERIAL_ECHOPGM("\nDWIN handshake ");
  DWIN_ICON_Show(ICON, ICON_LOGO, 71, 72);
  Main_Menu_Icons();
}

void CrealityDWINClass::Print_Screen_Icons() {
  if (selection == 0) {
    DWIN_ICON_Show(ICON, ICON_Setup_1, 8, 252);
    DWIN_Draw_Rectangle(0, GetColor(eeprom_settings.highlight_box, Color_White), 8, 252, 87, 351);
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 30, 322, F("Tune"));
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Setup_0, 8, 252);
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 30, 322, F("Tune"));
  }
  if (selection == 2) {
    DWIN_ICON_Show(ICON, ICON_Stop_1, 184, 252);
    DWIN_Draw_Rectangle(0, GetColor(eeprom_settings.highlight_box, Color_White), 184, 252, 263, 351);
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 205, 322, F("Stop"));
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Stop_0, 184, 252);
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 205, 322, F("Stop"));
  }
  if (paused) {
    if (selection == 1) {
      DWIN_ICON_Show(ICON, ICON_Continue_1, 96, 252);
      DWIN_Draw_Rectangle(0, GetColor(eeprom_settings.highlight_box, Color_White), 96, 252, 175, 351);
      DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 114, 322, F("Print"));
    }
    else {
      DWIN_ICON_Show(ICON, ICON_Continue_0, 96, 252);
      DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 114, 322, F("Print"));
    }
  }
  else {
    if (selection == 1) {
      DWIN_ICON_Show(ICON, ICON_Pause_1, 96, 252);
      DWIN_Draw_Rectangle(0, GetColor(eeprom_settings.highlight_box, Color_White), 96, 252, 175, 351);
      DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 114, 322, F("Pause"));
    }
    else {
      DWIN_ICON_Show(ICON, ICON_Pause_0, 96, 252);
      DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Blue, 114, 322, F("Pause"));
    }
  }
}

void CrealityDWINClass::Draw_Print_Screen() {
  process = Print;
  selection = 0;
  Clear_Screen();
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 8, 352, DWIN_WIDTH-8, 376);
  Draw_Title("Printing...");
  Print_Screen_Icons();
  DWIN_ICON_Show(ICON, ICON_PrintTime, 14, 171);
  DWIN_ICON_Show(ICON, ICON_RemainTime, 147, 169);
  DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Black, 41, 163, "Elapsed");
  DWIN_Draw_String(false, false, DWIN_FONT_MENU,  Color_White, Color_Bg_Black, 176, 163, "Remaining");
  Update_Status_Bar(true);
  Draw_Print_ProgressBar();
  Draw_Print_ProgressElapsed();
  Draw_Print_ProgressRemain();
  Draw_Print_Filename(true);
}

void CrealityDWINClass::Draw_Print_Filename(bool reset/*=false*/) {
  static uint8_t namescrl = 0;
  if (reset) namescrl = 0;
  if (process == Print) {
    size_t len = strlen(filename);
    int8_t pos = len;
    if (pos > 30) {
      pos -= namescrl;
      len = pos;
      if (len > 30)
        len = 30;
      char dispname[len+1];
      if (pos >= 0) {
        LOOP_L_N(i, len) dispname[i] = filename[i+namescrl];
      }
      else {
        LOOP_L_N(i, 30+pos) dispname[i] = ' ';
        LOOP_S_L_N(i, 30+pos, 30) dispname[i] = filename[i-(30+pos)];
      }
      dispname[len] = '\0';
      DWIN_Draw_Rectangle(1, Color_Bg_Black, 8, 50, DWIN_WIDTH-8, 80);
      const int8_t npos = (DWIN_WIDTH - 30 * MENU_CHR_W) / 2;
      DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Black, npos, 60, dispname);
      if (-pos >= 30)
        namescrl = 0;
      namescrl++;
    }
    else {
      DWIN_Draw_Rectangle(1, Color_Bg_Black, 8, 50, DWIN_WIDTH-8, 80);
      const int8_t npos = (DWIN_WIDTH - strlen(filename) * MENU_CHR_W) / 2;
      DWIN_Draw_String(false, false, DWIN_FONT_MENU, Color_White, Color_Bg_Black, npos, 60, filename);
    }
  }
}

void CrealityDWINClass::Draw_Print_ProgressBar() {
  uint8_t printpercent = sdprint ? card.percentDone() : (ui._get_progress()/100);
  DWIN_ICON_Show(ICON, ICON_Bar, 15, 93);
  DWIN_Draw_Rectangle(1, BarFill_Color, 16 + printpercent * 240 / 100, 93, 256, 113);
  DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_MENU, GetColor(eeprom_settings.progress_percent, Percent_Color), Color_Bg_Black, 3, 109, 133, printpercent);
  DWIN_Draw_String(false, false, DWIN_FONT_MENU, GetColor(eeprom_settings.progress_percent, Percent_Color), Color_Bg_Black, 133, 133, "%");
}

void CrealityDWINClass::Draw_Print_ProgressRemain() {
  uint16_t remainingtime = ui.get_remaining_time();
  DWIN_Draw_IntValue(true, true, 1, DWIN_FONT_MENU, GetColor(eeprom_settings.progress_time, Color_White), Color_Bg_Black, 2, 176, 187, remainingtime / 3600);
  DWIN_Draw_IntValue(true, true, 1, DWIN_FONT_MENU, GetColor(eeprom_settings.progress_time, Color_White), Color_Bg_Black, 2, 200, 187, (remainingtime % 3600) / 60);
  if (eeprom_settings.time_format_textual) {
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, GetColor(eeprom_settings.progress_time, Color_White), Color_Bg_Black, 192, 187, "h");
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, GetColor(eeprom_settings.progress_time, Color_White), Color_Bg_Black, 216, 187, "m");
  }
  else {
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, GetColor(eeprom_settings.progress_time, Color_White), Color_Bg_Black, 192, 187, ":");
  }
}

void CrealityDWINClass::Draw_Print_ProgressElapsed() {
  duration_t elapsed = print_job_timer.duration();
  DWIN_Draw_IntValue(true, true, 1, DWIN_FONT_MENU, GetColor(eeprom_settings.progress_time, Color_White), Color_Bg_Black, 2, 42, 187, elapsed.value / 3600);
  DWIN_Draw_IntValue(true, true, 1, DWIN_FONT_MENU, GetColor(eeprom_settings.progress_time, Color_White), Color_Bg_Black, 2, 66, 187, (elapsed.value % 3600) / 60);
  if (eeprom_settings.time_format_textual) {
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, GetColor(eeprom_settings.progress_time, Color_White), Color_Bg_Black, 58, 187, "h");
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, GetColor(eeprom_settings.progress_time, Color_White), Color_Bg_Black, 82, 187, "m");
  }
  else {
    DWIN_Draw_String(false, false, DWIN_FONT_MENU, GetColor(eeprom_settings.progress_time, Color_White), Color_Bg_Black, 58, 187, ":");
  }
}

void CrealityDWINClass::Draw_Print_confirm() {
  Draw_Print_Screen();
  process = Confirm;
  popup = Complete;
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 8, 252, 263, 351);
  DWIN_ICON_Show(ICON, ICON_Confirm_E, 87, 283);
  DWIN_Draw_Rectangle(0, GetColor(eeprom_settings.highlight_box, Color_White), 86, 282, 187, 321);
  DWIN_Draw_Rectangle(0, GetColor(eeprom_settings.highlight_box, Color_White), 85, 281, 188, 322);
}

void CrealityDWINClass::Draw_SD_Item(uint8_t item, uint8_t row) {
  if (item == 0) {
    if (card.flag.workDirIsRoot)
      Draw_Menu_Item(0, ICON_Back, "Back");
    else
      Draw_Menu_Item(0, ICON_Back, "..");
  }
  else {
    card.getfilename_sorted(SD_ORDER(item-1, card.get_num_Files()));
    char * const filename = card.longest_filename();
    size_t max = MENU_CHAR_LIMIT;
    size_t pos = strlen(filename), len = pos;
    if (!card.flag.filenameIsDir)
      while (pos && filename[pos] != '.') pos--;
    len = pos;
    if (len > max) len = max;
    char name[len+1];
    LOOP_L_N(i, len) name[i] = filename[i];
    if (pos > max)
      LOOP_S_L_N(i, len-3, len) name[i] = '.';
    name[len] = '\0';
    Draw_Menu_Item(row, card.flag.filenameIsDir ? ICON_More : ICON_File, name);
  }
}

void CrealityDWINClass::Draw_SD_List(bool removed/*=false*/) {
  Clear_Screen();
  Draw_Title("Select File");
  selection = 0;
  scrollpos = 0;
  process = File;
  if (card.isMounted() && !removed) {
    LOOP_L_N(i, _MIN(card.get_num_Files()+1, TROWS))
      Draw_SD_Item(i, i);
  }
  else {
    Draw_Menu_Item(0, ICON_Back, "Back");
    DWIN_Draw_Rectangle(1, Color_Bg_Red, 10, MBASE(3) - 10, DWIN_WIDTH - 10, MBASE(4));
    DWIN_Draw_String(false, false, font16x32, Color_Yellow, Color_Bg_Red, ((DWIN_WIDTH) - 8 * 16) / 2, MBASE(3), "No Media");
  }
  DWIN_Draw_Rectangle(1, GetColor(eeprom_settings.cursor_color, Rectangle_Color), 0, MBASE(0) - 18, 14, MBASE(0) + 33);
}

void CrealityDWINClass::Draw_Status_Area(bool icons/*=false*/) {

  if(icons) DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, STATUS_Y, DWIN_WIDTH, DWIN_HEIGHT - 1);

  #if HAS_HOTEND
    static float hotend = -1;
    static int16_t hotendtarget = -1;
    static int16_t flow = -1;
    if (icons) { 
      hotend = -1;
      hotendtarget = -1;
      DWIN_ICON_Show(ICON, ICON_HotendTemp, 10, 383);
      DWIN_Draw_String(false, false, DWIN_FONT_STAT, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 25 + 3 * STAT_CHR_W + 5, 384, F("/"));
    }
    if (thermalManager.temp_hotend[0].celsius != hotend) {
      hotend = thermalManager.temp_hotend[0].celsius;
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 3, 28, 384, thermalManager.temp_hotend[0].celsius);
      DWIN_Draw_DegreeSymbol(GetColor(eeprom_settings.status_area_text, Color_White), 25 + 3 * STAT_CHR_W + 5, 386);
    }
    if (thermalManager.temp_hotend[0].target != hotendtarget) {
      hotendtarget = thermalManager.temp_hotend[0].target;
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 3, 25 + 4 * STAT_CHR_W + 6, 384, thermalManager.temp_hotend[0].target);
      DWIN_Draw_DegreeSymbol(GetColor(eeprom_settings.status_area_text, Color_White), 25 + 4 * STAT_CHR_W + 39, 386);
    }
    if (icons) {
      flow = -1;
      DWIN_ICON_Show(ICON, ICON_StepE, 112, 417);
      DWIN_Draw_String(false, false, DWIN_FONT_STAT, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 116 + 5 * STAT_CHR_W + 2, 417, F("%"));
    } 
    if (planner.flow_percentage[0] != flow) {
      flow = planner.flow_percentage[0];
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 3, 116 + 2 * STAT_CHR_W, 417, planner.flow_percentage[0]);
    }
  #endif

  #if HAS_HEATED_BED
    static float bed = -1;
    static int16_t bedtarget = -1;
    if (icons) {
      bed = -1;
      bedtarget = -1;
      DWIN_ICON_Show(ICON, ICON_BedTemp, 10, 416);
      DWIN_Draw_String(false, false, DWIN_FONT_STAT, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 25 + 3 * STAT_CHR_W + 5, 417, F("/"));
    }
    if (thermalManager.temp_bed.celsius != bed) {
      bed = thermalManager.temp_bed.celsius;
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 3, 28, 417, thermalManager.temp_bed.celsius);
      DWIN_Draw_DegreeSymbol(GetColor(eeprom_settings.status_area_text, Color_White), 25 + 3 * STAT_CHR_W + 5, 419);
    }
    if (thermalManager.temp_bed.target != bedtarget) {
      bedtarget = thermalManager.temp_bed.target;
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 3, 25 + 4 * STAT_CHR_W + 6, 417, thermalManager.temp_bed.target);
      DWIN_Draw_DegreeSymbol(GetColor(eeprom_settings.status_area_text, Color_White), 25 + 4 * STAT_CHR_W + 39, 419);
    }
  #endif

  #if HAS_FAN
    static uint8_t fan = -1;
    if (icons) {
      fan = -1;
      DWIN_ICON_Show(ICON, ICON_FanSpeed, 187, 383);
    }
    if (thermalManager.fan_speed[0] != fan) {
      fan = thermalManager.fan_speed[0];
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 3, 195 + 2 * STAT_CHR_W, 384, thermalManager.fan_speed[0]);
    }
  #endif

  #if HAS_ZOFFSET_ITEM
    static float offset = -1;

    if (icons) {
      offset = -1;
      DWIN_ICON_Show(ICON, ICON_Zoffset, 187, 416);
    }
    if (zoffsetvalue != offset) {
      offset = zoffsetvalue;
      if (zoffsetvalue < 0) {
        DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 2, 2, 207, 417, -zoffsetvalue * 100);
        DWIN_Draw_String(false, true, DWIN_FONT_MENU, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 205, 419, "-");
      }
      else {
        DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 2, 2, 207, 417, zoffsetvalue* 100);
        DWIN_Draw_String(false, true, DWIN_FONT_MENU, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 205, 419, " ");
      }
    }
  #endif

  static int16_t feedrate = -1;
  if (icons) {
    feedrate = -1;
    DWIN_ICON_Show(ICON, ICON_Speed, 113, 383);
    DWIN_Draw_String(false, false, DWIN_FONT_STAT, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 116 + 5 * STAT_CHR_W + 2, 384, F("%"));
  }
  if (feedrate_percentage != feedrate) {
    feedrate = feedrate_percentage;
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, GetColor(eeprom_settings.status_area_text, Color_White), Color_Bg_Black, 3, 116 + 2 * STAT_CHR_W, 384, feedrate_percentage);
  }

  static float x = -1;
  static float y = -1;
  static float z = -1;
  static bool update_x = false;
  static bool update_y = false;
  static bool update_z = false;
  update_x = (current_position.x != x || axis_should_home(X_AXIS) || update_x);
  update_y = (current_position.y != y || axis_should_home(Y_AXIS) || update_y);
  update_z = (current_position.z != z || axis_should_home(Z_AXIS) || update_z);
  if (icons) {
    x = -1;
    y = -1;
    z = -1;
    DWIN_Draw_Line(GetColor(eeprom_settings.coordinates_split_line, Line_Color, true), 16, 450, 256, 450);
    DWIN_ICON_Show(ICON, ICON_MaxSpeedX,   10, 456);
    DWIN_ICON_Show(ICON, ICON_MaxSpeedY,   95, 456);
    DWIN_ICON_Show(ICON, ICON_MaxSpeedZ,   180, 456);
  }
  if (update_x) {
    x = current_position.x;
    if ((update_x = axis_should_home(X_AXIS) && ui.get_blink()))
      DWIN_Draw_String(false, true, DWIN_FONT_MENU, GetColor(eeprom_settings.coordinates_text, Color_White), Color_Bg_Black, 35, 459, "  -?-  ");
    else
      DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_MENU, GetColor(eeprom_settings.coordinates_text, Color_White), Color_Bg_Black, 3, 1, 35, 459, current_position.x * 10);
  }
  if (update_y) {
    y = current_position.y;
    if ((update_y = axis_should_home(Y_AXIS) && ui.get_blink()))
      DWIN_Draw_String(false, true, DWIN_FONT_MENU, GetColor(eeprom_settings.coordinates_text, Color_White), Color_Bg_Black, 120, 459, "  -?-  ");
    else
      DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_MENU, GetColor(eeprom_settings.coordinates_text, Color_White), Color_Bg_Black, 3, 1, 120, 459, current_position.y * 10);
  }
  if (update_z) {
    z = current_position.z;
    if ((update_z = axis_should_home(Z_AXIS) && ui.get_blink()))
      DWIN_Draw_String(false, true, DWIN_FONT_MENU, GetColor(eeprom_settings.coordinates_text, Color_White), Color_Bg_Black, 205, 459, "  -?-  ");
        else
      DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_MENU, GetColor(eeprom_settings.coordinates_text, Color_White), Color_Bg_Black, 3, 2, 205, 459, (current_position.z>=0) ? current_position.z * 100 : 0);
  }
  DWIN_UpdateLCD();
}

void CrealityDWINClass::Draw_Popup(const char *line1, const char *line2,const char *line3, uint8_t mode, uint8_t icon/*=0*/) {
  if (process != Confirm && process != Popup && process != Wait) last_process = process;
  if ((process == Menu || process == Wait) && mode == Popup) last_selection = selection;
  process = mode;
  Clear_Screen();
  DWIN_Draw_Rectangle(0, Color_White, 13, 59, 259, 351);
  DWIN_Draw_Rectangle(1, Color_Bg_Window, 14, 60, 258, 350);
  uint8_t ypos;
  if (mode == Popup || mode == Confirm)
    ypos = 150;
  else
    ypos = 230;
  if (icon > 0) 
    DWIN_ICON_Show(ICON, icon, 101, 105);
  DWIN_Draw_String(false, true, DWIN_FONT_MENU, Popup_Text_Color, Color_Bg_Window, (272 - 8 * strlen(line1)) / 2, ypos, F(line1));
  DWIN_Draw_String(false, true, DWIN_FONT_MENU, Popup_Text_Color, Color_Bg_Window, (272 - 8 * strlen(line2)) / 2, ypos+30, F(line2));
  DWIN_Draw_String(false, true, DWIN_FONT_MENU, Popup_Text_Color, Color_Bg_Window, (272 - 8 * strlen(line3)) / 2, ypos+60, F(line3));
  if (mode == Popup) {
    selection = 0;
    DWIN_Draw_Rectangle(1, Confirm_Color, 26, 280, 125, 317);
    DWIN_Draw_Rectangle(1, Cancel_Color, 146, 280, 245, 317);
    DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Window, 39, 290, "Confirm");
    DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Window, 165, 290, "Cancel");
    Popup_Select();
  }
  else if (mode == Confirm) {
    DWIN_Draw_Rectangle(1, Confirm_Color, 87, 280, 186, 317);
    DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Window, 96, 290, "Continue");
  }
}

void CrealityDWINClass::Popup_Select() {
  const uint16_t c1 = (selection==0) ? GetColor(eeprom_settings.highlight_box, Color_White) : Color_Bg_Window,
                 c2 = (selection==0) ? Color_Bg_Window : GetColor(eeprom_settings.highlight_box, Color_White);
  DWIN_Draw_Rectangle(0, c1, 25, 279, 126, 318);
  DWIN_Draw_Rectangle(0, c1, 24, 278, 127, 319);
  DWIN_Draw_Rectangle(0, c2, 145, 279, 246, 318);
  DWIN_Draw_Rectangle(0, c2, 144, 278, 247, 319);
}

void CrealityDWINClass::Update_Status_Bar(bool refresh/*=false*/) {
  static bool new_msg;
  static uint8_t msgscrl = 0;
  static char lastmsg[64];
  if (strcmp_P(lastmsg, statusmsg) != 0 || refresh) {
    strcpy_P(lastmsg, statusmsg);
    msgscrl = 0;
    new_msg = true;
  }
  size_t len = strlen(statusmsg);
  int8_t pos = len;
  if (pos > 30) {
    pos -= msgscrl;
    len = pos;
    if (len > 30)
      len = 30;
    char dispmsg[len+1];
    if (pos >= 0) {
      LOOP_L_N(i, len) dispmsg[i] = statusmsg[i+msgscrl];
    }
    else {
      LOOP_L_N(i, 30+pos) dispmsg[i] = ' ';
      LOOP_S_L_N(i, 30+pos, 30) dispmsg[i] = statusmsg[i-(30+pos)];
    }
    dispmsg[len] = '\0';
    if (process == Print) {
      DWIN_Draw_Rectangle(1, Color_Grey, 8, 214, DWIN_WIDTH-8, 238);
      const int8_t npos = (DWIN_WIDTH - 30 * MENU_CHR_W) / 2;
      DWIN_Draw_String(false, false, DWIN_FONT_MENU, GetColor(eeprom_settings.status_bar_text, Color_White), Color_Bg_Black, npos, 219, dispmsg);
    }
    else {
      DWIN_Draw_Rectangle(1, Color_Bg_Black, 8, 352, DWIN_WIDTH-8, 376);
      const int8_t npos = (DWIN_WIDTH - 30 * MENU_CHR_W) / 2;
      DWIN_Draw_String(false, false, DWIN_FONT_MENU, GetColor(eeprom_settings.status_bar_text, Color_White), Color_Bg_Black, npos, 357, dispmsg);
    }
    if (-pos >= 30)
      msgscrl = 0;
    msgscrl++;
  }
  else {
    if (new_msg) {
      new_msg = false;
      if (process == Print) {
        DWIN_Draw_Rectangle(1, Color_Grey, 8, 214, DWIN_WIDTH-8, 238);
        const int8_t npos = (DWIN_WIDTH - strlen(statusmsg) * MENU_CHR_W) / 2;
        DWIN_Draw_String(false, false, DWIN_FONT_MENU, GetColor(eeprom_settings.status_bar_text, Color_White), Color_Bg_Black, npos, 219, statusmsg);
      }
      else {
        DWIN_Draw_Rectangle(1, Color_Bg_Black, 8, 352, DWIN_WIDTH-8, 376);
        const int8_t npos = (DWIN_WIDTH - strlen(statusmsg) * MENU_CHR_W) / 2;
        DWIN_Draw_String(false, false, DWIN_FONT_MENU, GetColor(eeprom_settings.status_bar_text, Color_White), Color_Bg_Black, npos, 357, statusmsg);
      }
    }
  }
}

/* Menu Item Config */

void CrealityDWINClass::Menu_Item_Handler(uint8_t menu, uint8_t item, bool draw/*=true*/) {
  uint8_t row = item - scrollpos;
  #if HAS_LEVELING
    static bool level_state;
  #endif
  switch (menu) {
    case Prepare:

      #define PREPARE_BACK 0
      #define PREPARE_MOVE (PREPARE_BACK + 1)
      #define PREPARE_DISABLE (PREPARE_MOVE + 1)
      #define PREPARE_HOME (PREPARE_DISABLE + 1)
      #define PREPARE_MANUALLEVEL (PREPARE_HOME + 1)
      #define PREPARE_ZOFFSET (PREPARE_MANUALLEVEL + ENABLED(HAS_ZOFFSET_ITEM))
      #define PREPARE_PREHEAT (PREPARE_ZOFFSET + ENABLED(HAS_PREHEAT))
      #define PREPARE_COOLDOWN (PREPARE_PREHEAT + ENABLED(HAS_PREHEAT))
      #define PREPARE_CHANGEFIL (PREPARE_COOLDOWN + ENABLED(ADVANCED_PAUSE_FEATURE))
      #define PREPARE_TOTAL PREPARE_CHANGEFIL

      switch (item) {
        case PREPARE_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            Draw_Main_Menu(1);
          }
          break;
        case PREPARE_MOVE:
          if (draw) {
            Draw_Menu_Item(row, ICON_Axis, "Move", NULL, true);
          }
          else {
            Draw_Menu(Move);
          }
          break;
        case PREPARE_DISABLE:
          if (draw) {
            Draw_Menu_Item(row, ICON_CloseMotor, "Disable Stepper");
          }
          else {
            queue.inject_P(PSTR("M84"));
          }
          break;
        case PREPARE_HOME:
          if (draw) {
            Draw_Menu_Item(row, ICON_SetHome, "Homing", NULL, true);
          }
          else {
            Draw_Menu(HomeMenu);
          }
          break;
        case PREPARE_MANUALLEVEL:
          if (draw) {
            Draw_Menu_Item(row, ICON_PrintSize, "Manual Leveling", NULL, true);
          }
          else {
            if (axes_should_home()) {
              Popup_Handler(Home);
              gcode.home_all_axes(true);
            }
            #if HAS_LEVELING
              level_state = planner.leveling_active;
              set_bed_leveling_enabled(false);
            #endif
            Draw_Menu(ManualLevel);
          }
          break;
        #if HAS_ZOFFSET_ITEM
          case PREPARE_ZOFFSET:
            if (draw) {
              Draw_Menu_Item(row, ICON_Zoffset, "Z-Offset", NULL, true);
            }
            else {
              #if HAS_LEVELING
                level_state = planner.leveling_active;
                set_bed_leveling_enabled(false);
              #endif
              Draw_Menu(ZOffset);
            }
            break;
        #endif
        #if HAS_PREHEAT
          case PREPARE_PREHEAT:
            if (draw) {
              Draw_Menu_Item(row, ICON_Temperature, "Preheat", NULL, true);
            }
            else {
              Draw_Menu(Preheat);
            }
            break;
          case PREPARE_COOLDOWN:
            if (draw) {
              Draw_Menu_Item(row, ICON_Cool, "Cooldown");
            } 
            else {
              thermalManager.zero_fan_speeds();
              thermalManager.disable_all_heaters();
            }
            break;
        #endif
        #if ENABLED(ADVANCED_PAUSE_FEATURE)
          case PREPARE_CHANGEFIL:
            if (draw) {
              #if ENABLED(FILAMENT_LOAD_UNLOAD_GCODES)
                Draw_Menu_Item(row, ICON_ResumeEEPROM, "Change Filament", NULL, true);
              #else
                Draw_Menu_Item(row, ICON_ResumeEEPROM, "Change Filament");
              #endif
            }
            else {
              #if ENABLED(FILAMENT_LOAD_UNLOAD_GCODES)
                Draw_Menu(ChangeFilament);
              #else
                if (thermalManager.temp_hotend[0].target < thermalManager.extrude_min_temp) {
                  Popup_Handler(ETemp);
                }
                else {
                  if (thermalManager.temp_hotend[0].celsius < thermalManager.temp_hotend[0].target-2) {
                    Popup_Handler(Heating);
                    thermalManager.wait_for_hotend(0);
                  }
                  Popup_Handler(FilChange);
                  sprintf_P(cmd, PSTR("M600 B1 R%i"), thermalManager.temp_hotend[0].target);
                  gcode.process_subcommands_now_P(cmd);
                }
              #endif
            }
            break;
        #endif
      }
      break;

    case HomeMenu:

      #define HOME_BACK 0
      #define HOME_ALL (HOME_BACK + 1)
      #define HOME_X (HOME_ALL + 1)
      #define HOME_Y (HOME_X + 1)
      #define HOME_Z (HOME_Y + 1)
      #define HOME_SET (HOME_Z + 1)
      #define HOME_TOTAL HOME_SET

      switch(item) {
        case HOME_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            Draw_Menu(Prepare, PREPARE_HOME);
          }
          break;
        case HOME_ALL:
          if (draw) {
            Draw_Menu_Item(row, ICON_Homing, "Home All");
          }
          else {
            Popup_Handler(Home);
            gcode.home_all_axes(true);
            Redraw_Menu();
          }
          break;
        case HOME_X:
          if (draw) {
            Draw_Menu_Item(row, ICON_MoveX, "Home X");
          }
          else {
            Popup_Handler(Home);
            gcode.process_subcommands_now_P(PSTR("G28 X"));
            planner.synchronize();
            Redraw_Menu();
          }
          break;
        case HOME_Y:
          if (draw) {
            Draw_Menu_Item(row, ICON_MoveY, "Home Y");
          }
          else {
            Popup_Handler(Home);
            gcode.process_subcommands_now_P(PSTR("G28 Y"));
            planner.synchronize();
            Redraw_Menu();
          }
          break;
        case HOME_Z:
          if (draw) {
            Draw_Menu_Item(row, ICON_MoveZ,"Home Z");
          }
          else {
            Popup_Handler(Home);
            gcode.process_subcommands_now_P(PSTR("G28 Z"));
            planner.synchronize();
            Redraw_Menu();
          }
          break;
        case HOME_SET:
          if (draw) {
            Draw_Menu_Item(row, ICON_SetHome, "Set Home Position");
          }
          else {
            gcode.process_subcommands_now_P(PSTR("G92 X0 Y0 Z0"));
            AudioFeedback();
          }
          break;
      }
      break;
    case Move:

      #define MOVE_BACK 0
      #define MOVE_X (MOVE_BACK + 1)
      #define MOVE_Y (MOVE_X + 1)
      #define MOVE_Z (MOVE_Y + 1)
      #define MOVE_E (MOVE_Z + ENABLED(HAS_HOTEND))
      #define MOVE_P (MOVE_E + ENABLED(HAS_BED_PROBE))
      #define MOVE_LIVE (MOVE_P + 1)
      #define MOVE_TOTAL MOVE_LIVE

      switch (item) {
        case MOVE_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            #if HAS_BED_PROBE
              probe_deployed = false;
              probe.set_deployed(probe_deployed);
            #endif
            Draw_Menu(Prepare, PREPARE_MOVE);
          }
          break;
        case MOVE_X:
          if (draw) {
            Draw_Menu_Item(row, ICON_MoveX, "Move X");
            Draw_Float(current_position.x, row, false);
          }
          else {
            Modify_Value(current_position.x, X_MIN_POS, X_MAX_POS, 10);
          }
          break;
        case MOVE_Y:
          if (draw) {
            Draw_Menu_Item(row, ICON_MoveY, "Move Y");
            Draw_Float(current_position.y, row);
          }
          else {
            Modify_Value(current_position.y, Y_MIN_POS, Y_MAX_POS, 10);
          }
          break;
        case MOVE_Z:
          if (draw) {
            Draw_Menu_Item(row, ICON_MoveZ, "Move Z");
            Draw_Float(current_position.z, row);
          }
          else {
            Modify_Value(current_position.z, Z_MIN_POS, Z_MAX_POS, 10);
          }
          break;
        #if HAS_HOTEND
          case MOVE_E:
            if (draw) {
              Draw_Menu_Item(row, ICON_Extruder, "Extruder");
              current_position.e = 0;
              sync_plan_position();
              Draw_Float(current_position.e, row);
            }
            else {
              if (thermalManager.temp_hotend[0].target < thermalManager.extrude_min_temp) {
                Popup_Handler(ETemp);
              }
              else {
                if (thermalManager.temp_hotend[0].celsius < thermalManager.temp_hotend[0].target-2) {
                  Popup_Handler(Heating);
                  thermalManager.wait_for_hotend(0);
                  Redraw_Menu();
                }
                current_position.e = 0;
                sync_plan_position();
                Modify_Value(current_position.e, -500, 500, 10);
              }
            }
          break;
        #endif
        #if HAS_BED_PROBE
          case MOVE_P:
            if (draw) {
              Draw_Menu_Item(row, ICON_StockConfiguraton, "Probe");
              Draw_Checkbox(row, probe_deployed);
            }
            else {
              probe_deployed = !probe_deployed;
              probe.set_deployed(probe_deployed);
              Draw_Checkbox(row, probe_deployed);
            }
            break;
        #endif
        case MOVE_LIVE:
          if (draw) {
            Draw_Menu_Item(row, ICON_Axis, "Live Movement");
            Draw_Checkbox(row, livemove);
          }
          else {
            livemove = !livemove;
            Draw_Checkbox(row, livemove);
          }
          break;
      }
      break;
    case ManualLevel:

      #define MLEVEL_BACK 0
      #define MLEVEL_PROBE (MLEVEL_BACK + ENABLED(HAS_BED_PROBE))
      #define MLEVEL_BL (MLEVEL_PROBE + 1)
      #define MLEVEL_TL (MLEVEL_BL + 1)
      #define MLEVEL_TR (MLEVEL_TL + 1)
      #define MLEVEL_BR (MLEVEL_TR + 1)
      #define MLEVEL_C (MLEVEL_BR + 1)
      #define MLEVEL_ZPOS (MLEVEL_C + 1)
      #define MLEVEL_TOTAL MLEVEL_ZPOS

      static float mlev_z_pos = 0;
      static bool use_probe = false;

      switch (item) {
        case MLEVEL_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            #if HAS_LEVELING
              set_bed_leveling_enabled(level_state);
            #endif
            Draw_Menu(Prepare, PREPARE_MANUALLEVEL);
          }
          break;
        #if HAS_BED_PROBE
          case MLEVEL_PROBE:
            if (draw) {
              Draw_Menu_Item(row, ICON_Zoffset, "Use Probe");
              Draw_Checkbox(row, use_probe);
            }
            else {
              use_probe = !use_probe;
              Draw_Checkbox(row, use_probe);
              if (use_probe) {
                Popup_Handler(Level);
                corner_avg = 0;
                #define PROBE_X_MIN _MAX(0 + corner_pos, X_MIN_POS + probe.offset.x, X_MIN_POS + PROBING_MARGIN) - probe.offset.x
                #define PROBE_X_MAX _MIN((X_BED_SIZE + X_MIN_POS) - corner_pos, X_MAX_POS + probe.offset.x, X_MAX_POS - PROBING_MARGIN) - probe.offset.x
                #define PROBE_Y_MIN _MAX(0 + corner_pos, Y_MIN_POS + probe.offset.y, Y_MIN_POS + PROBING_MARGIN) - probe.offset.y
                #define PROBE_Y_MAX _MIN((Y_BED_SIZE + Y_MIN_POS) - corner_pos, Y_MAX_POS + probe.offset.y, Y_MAX_POS - PROBING_MARGIN) - probe.offset.y
                corner_avg += probe.probe_at_point(PROBE_X_MIN, PROBE_Y_MIN, PROBE_PT_RAISE, 0, false);
                corner_avg += probe.probe_at_point(PROBE_X_MIN, PROBE_Y_MAX, PROBE_PT_RAISE, 0, false);
                corner_avg += probe.probe_at_point(PROBE_X_MAX, PROBE_Y_MAX, PROBE_PT_RAISE, 0, false);
                corner_avg += probe.probe_at_point(PROBE_X_MAX, PROBE_Y_MIN, PROBE_PT_STOW, 0, false);
                corner_avg /= 4;
                Redraw_Menu();
              }
            }
            break;
        #endif
        case MLEVEL_BL:
          if (draw) {
            Draw_Menu_Item(row, ICON_AxisBL, "Bottom Left");
          }
          else {
            Popup_Handler(MoveWait);
            if (use_probe) {
              #if HAS_BED_PROBE
                sprintf_P(cmd, PSTR("G0 F4000\nG0 Z10\nG0 X%s Y%s"), dtostrf(PROBE_X_MIN, 1, 3, str_1), dtostrf(PROBE_Y_MIN, 1, 3, str_2));
                gcode.process_subcommands_now_P(cmd);
                planner.synchronize();
                Popup_Handler(ManualProbing);
              #endif
            }
            else {
              sprintf_P(cmd, PSTR("G0 F4000\nG0 Z10\nG0 X%s Y%s\nG0 F300 Z%s"), dtostrf(corner_pos, 1, 3, str_1), dtostrf(corner_pos, 1, 3, str_2), dtostrf(mlev_z_pos, 1, 3, str_3));
              gcode.process_subcommands_now_P(cmd);
              planner.synchronize();
              Redraw_Menu();
            }
          }
          break;
        case MLEVEL_TL:
          if (draw) {
            Draw_Menu_Item(row, ICON_AxisTL, "Top Left");
          }
          else {
            Popup_Handler(MoveWait);
            if (use_probe) {
              #if HAS_BED_PROBE
                sprintf_P(cmd, PSTR("G0 F4000\nG0 Z10\nG0 X%s Y%s"), dtostrf(PROBE_X_MIN, 1, 3, str_1), dtostrf(PROBE_Y_MAX, 1, 3, str_2));
                gcode.process_subcommands_now_P(cmd);
                planner.synchronize();
                Popup_Handler(ManualProbing);
              #endif
            }
            else {
              sprintf_P(cmd, PSTR("G0 F4000\nG0 Z10\nG0 X%s Y%s\nG0 F300 Z%s"), dtostrf(corner_pos, 1, 3, str_1), dtostrf((Y_BED_SIZE + Y_MIN_POS) - corner_pos, 1, 3, str_2), dtostrf(mlev_z_pos, 1, 3, str_3));
              gcode.process_subcommands_now_P(cmd);
              planner.synchronize();
              Redraw_Menu();
            }
          }
          break;
        case MLEVEL_TR:
          if (draw) {
            Draw_Menu_Item(row, ICON_AxisTR, "Top Right");
          }
          else {
            Popup_Handler(MoveWait);
            if (use_probe) {
              #if HAS_BED_PROBE
                sprintf_P(cmd, PSTR("G0 F4000\nG0 Z10\nG0 X%s Y%s"), dtostrf(PROBE_X_MAX, 1, 3, str_1), dtostrf(PROBE_Y_MAX, 1, 3, str_2));
                gcode.process_subcommands_now_P(cmd);
                planner.synchronize();
                Popup_Handler(ManualProbing);
              #endif
            }
            else {
              sprintf_P(cmd, PSTR("G0 F4000\nG0 Z10\nG0 X%s Y%s\nG0 F300 Z%s"), dtostrf((X_BED_SIZE + X_MIN_POS) - corner_pos, 1, 3, str_1), dtostrf((Y_BED_SIZE + Y_MIN_POS) - corner_pos, 1, 3, str_2), dtostrf(mlev_z_pos, 1, 3, str_3));
              gcode.process_subcommands_now_P(cmd);
              planner.synchronize();
              Redraw_Menu();
            }
          }
          break;
        case MLEVEL_BR:
          if (draw) {
            Draw_Menu_Item(row, ICON_AxisBR, "Bottom Right");
          }
          else {
            Popup_Handler(MoveWait);
            if (use_probe) {
              #if HAS_BED_PROBE
                sprintf_P(cmd, PSTR("G0 F4000\nG0 Z10\nG0 X%s Y%s"), dtostrf(PROBE_X_MAX, 1, 3, str_1), dtostrf(PROBE_Y_MIN, 1, 3, str_2));
                gcode.process_subcommands_now_P(cmd);
                planner.synchronize();
                Popup_Handler(ManualProbing);
              #endif
            }
            else {
              sprintf_P(cmd, PSTR("G0 F4000\nG0 Z10\nG0 X%s Y%s\nG0 F300 Z%s"), dtostrf((X_BED_SIZE + X_MIN_POS) - corner_pos, 1, 3, str_1), dtostrf(corner_pos, 1, 3, str_2), dtostrf(mlev_z_pos, 1, 3, str_3));
              gcode.process_subcommands_now_P(cmd);
              planner.synchronize();
              Redraw_Menu();
            }
          }
          break;
        case MLEVEL_C:
          if (draw) {
            Draw_Menu_Item(row, ICON_AxisC, "Center");
          }
          else {
            Popup_Handler(MoveWait);
            if (use_probe) {
              #if HAS_BED_PROBE
                sprintf_P(cmd, PSTR("G0 F4000\nG0 Z10\nG0 X%s Y%s"), dtostrf(X_MAX_POS/2.0f - probe.offset.x, 1, 3, str_1), dtostrf(Y_MAX_POS/2.0f - probe.offset.y, 1, 3, str_2));
                gcode.process_subcommands_now_P(cmd);
                planner.synchronize();
                Popup_Handler(ManualProbing);
              #endif
            }
            else {
              sprintf_P(cmd, PSTR("G0 F4000\nG0 Z10\nG0 X%s Y%s\nG0 F300 Z%s"), dtostrf((X_BED_SIZE + X_MIN_POS)/2.0f, 1, 3, str_1), dtostrf((Y_BED_SIZE + Y_MIN_POS)/2.0f, 1, 3, str_2), dtostrf(mlev_z_pos, 1, 3, str_3));
              gcode.process_subcommands_now_P(cmd);
              planner.synchronize();
              Redraw_Menu();
            }
          }
          break;
        case MLEVEL_ZPOS:
          if (draw) {
            Draw_Menu_Item(row, ICON_SetZOffset, "Z Position");
            Draw_Float(mlev_z_pos, row, false, 100);
          }
          else {
            Modify_Value(mlev_z_pos, 0, MAX_Z_OFFSET, 100);
          }
          break;
      }
      break;
    #if HAS_ZOFFSET_ITEM
      case ZOffset:

        #define ZOFFSET_BACK 0
        #define ZOFFSET_HOME (ZOFFSET_BACK + 1)
        #define ZOFFSET_MODE (ZOFFSET_HOME + 1)
        #define ZOFFSET_OFFSET (ZOFFSET_MODE + 1)
        #define ZOFFSET_UP (ZOFFSET_OFFSET + 1)
        #define ZOFFSET_DOWN (ZOFFSET_UP + 1)
        #define ZOFFSET_SAVE (ZOFFSET_DOWN + ENABLED(EEPROM_SETTINGS))
        #define ZOFFSET_TOTAL ZOFFSET_SAVE

        switch (item) {
          case ZOFFSET_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              liveadjust = false;
              #if HAS_LEVELING
                set_bed_leveling_enabled(level_state);
              #endif
              Draw_Menu(Prepare, PREPARE_ZOFFSET);
            }
            break;
          case ZOFFSET_HOME:
            if (draw) {
              Draw_Menu_Item(row, ICON_Homing, "Home Z Axis");
            }
            else {
              Popup_Handler(Home);
              gcode.process_subcommands_now_P(PSTR("G28 Z"));
              Popup_Handler(MoveWait);
              #if ENABLED(Z_SAFE_HOMING)
                planner.synchronize();
                sprintf_P(cmd, PSTR("G0 F4000 X%s Y%s"), dtostrf(Z_SAFE_HOMING_X_POINT, 1, 3, str_1), dtostrf(Z_SAFE_HOMING_Y_POINT, 1, 3, str_2));
                gcode.process_subcommands_now_P(cmd);
              #else
                gcode.process_subcommands_now_P(PSTR("G0 F4000 X117.5 Y117.5"));
              #endif
              gcode.process_subcommands_now_P(PSTR("G0 F300 Z0"));
              planner.synchronize();
              Redraw_Menu();
            }
            break;
          case ZOFFSET_MODE:
            if (draw) {
              Draw_Menu_Item(row, ICON_Zoffset, "Live Adjustment");
              Draw_Checkbox(row, liveadjust);
            }
            else {
              if (!liveadjust) {
                if (axes_should_home()) {
                  Popup_Handler(Home);
                  gcode.home_all_axes(true);
                }
                Popup_Handler(MoveWait);
                #if ENABLED(Z_SAFE_HOMING)
                  planner.synchronize();
                  sprintf_P(cmd, PSTR("G0 F4000 X%s Y%s"), dtostrf(Z_SAFE_HOMING_X_POINT, 1, 3, str_1), dtostrf(Z_SAFE_HOMING_Y_POINT, 1, 3, str_2));
                  gcode.process_subcommands_now_P(cmd);
                #else
                  gcode.process_subcommands_now_P(PSTR("G0 F4000 X117.5 Y117.5"));
                #endif
                gcode.process_subcommands_now_P(PSTR("G0 F300 Z0"));
                planner.synchronize();
                Redraw_Menu();
              }
              liveadjust = !liveadjust;
              Draw_Checkbox(row, liveadjust);
            }
            break;
          case ZOFFSET_OFFSET:
            if (draw) {
              Draw_Menu_Item(row, ICON_SetZOffset, "Z Offset");
              Draw_Float(zoffsetvalue, row, false, 100);
            }
            else {
              Modify_Value(zoffsetvalue, MIN_Z_OFFSET, MAX_Z_OFFSET, 100);
            }
            break;
          case ZOFFSET_UP:
            if (draw) {
              Draw_Menu_Item(row, ICON_Axis, "Microstep Up");
            }
            else {
              if (zoffsetvalue < MAX_Z_OFFSET) {
                if(liveadjust) {
                  gcode.process_subcommands_now_P(PSTR("M290 Z0.01"));
                  planner.synchronize();
                }
                zoffsetvalue += 0.01;
                Draw_Float(zoffsetvalue, row-1, false, 100);
              }
            }
            break;
          case ZOFFSET_DOWN:
            if (draw) {
              Draw_Menu_Item(row, ICON_AxisD, "Microstep Down");
            }
            else {
              if (zoffsetvalue > MIN_Z_OFFSET) {
                if(liveadjust) {
                  gcode.process_subcommands_now_P(PSTR("M290 Z-0.01"));
                  planner.synchronize();
                }
                zoffsetvalue -= 0.01;
                Draw_Float(zoffsetvalue, row-2, false, 100);
              }
            }
            break;
          #if ENABLED(EEPROM_SETTINGS)
            case ZOFFSET_SAVE:
              if (draw) {
                Draw_Menu_Item(row, ICON_WriteEEPROM, "Save");
              }
              else {
                AudioFeedback(settings.save());
              }
              break;
          #endif
        }
        break;
    #endif
    #if HAS_PREHEAT
      case Preheat:

        #define PREHEAT_BACK 0
        #define PREHEAT_MODE (PREHEAT_BACK + 1)
        #define PREHEAT_1 (PREHEAT_MODE + (PREHEAT_COUNT >= 1))
        #define PREHEAT_2 (PREHEAT_1 + (PREHEAT_COUNT >= 2))
        #define PREHEAT_3 (PREHEAT_2 + (PREHEAT_COUNT >= 3))
        #define PREHEAT_4 (PREHEAT_3 + (PREHEAT_COUNT >= 4))
        #define PREHEAT_5 (PREHEAT_4 + (PREHEAT_COUNT >= 5))
        #define PREHEAT_TOTAL PREHEAT_5

        switch (item) {
          case PREHEAT_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Menu(Prepare, PREPARE_PREHEAT);
            }
            break;
          case PREHEAT_MODE:
           if (draw) {
            Draw_Menu_Item(row, ICON_Homing, "Preheat Mode");
            Draw_Option(preheatmode, preheat_modes, row);
          }
          else {
            Modify_Option(preheatmode, preheat_modes, 2);
          }
          break;
          #if (PREHEAT_COUNT >= 1)
            case PREHEAT_1:
              if (draw) {
                Draw_Menu_Item(row, ICON_Temperature, PREHEAT_1_LABEL);
              }
              else {
                thermalManager.disable_all_heaters();
                thermalManager.zero_fan_speeds();
                if (preheatmode == 0 || preheatmode == 1) {
                  thermalManager.setTargetHotend(ui.material_preset[0].hotend_temp, 0);
                  thermalManager.set_fan_speed(0, ui.material_preset[0].fan_speed);
                }
                if (preheatmode == 0 || preheatmode == 2) thermalManager.setTargetBed(ui.material_preset[0].bed_temp);
              }
              break;
          #endif
          #if (PREHEAT_COUNT >= 2)
            case PREHEAT_2:
              if (draw) {
                Draw_Menu_Item(row, ICON_Temperature, PREHEAT_2_LABEL);
              }
              else {
                thermalManager.disable_all_heaters();
                thermalManager.zero_fan_speeds();
                if (preheatmode == 0 || preheatmode == 1) {
                  thermalManager.setTargetHotend(ui.material_preset[1].hotend_temp, 0);
                  thermalManager.set_fan_speed(0, ui.material_preset[1].fan_speed);
                }
                if (preheatmode == 0 || preheatmode == 2) thermalManager.setTargetBed(ui.material_preset[1].bed_temp);
              }
              break;
          #endif
          #if (PREHEAT_COUNT >= 3)
            case PREHEAT_3:
              if (draw) {
                Draw_Menu_Item(row, ICON_Temperature, PREHEAT_3_LABEL);
              }
              else {
                thermalManager.disable_all_heaters();
                thermalManager.zero_fan_speeds();
                if (preheatmode == 0 || preheatmode == 1) {
                  thermalManager.setTargetHotend(ui.material_preset[2].hotend_temp, 0);
                  thermalManager.set_fan_speed(0, ui.material_preset[2].fan_speed);
                }
                if (preheatmode == 0 || preheatmode == 2) thermalManager.setTargetBed(ui.material_preset[2].bed_temp);
              }
              break;
          #endif
          #if (PREHEAT_COUNT >= 4)
            case PREHEAT_4:
              if (draw) {
                Draw_Menu_Item(row, ICON_Temperature, PREHEAT_4_LABEL);
              }
              else {
                thermalManager.disable_all_heaters();
                thermalManager.zero_fan_speeds();
                if (preheatmode == 0 || preheatmode == 1) {
                  thermalManager.setTargetHotend(ui.material_preset[3].hotend_temp, 0);
                  thermalManager.set_fan_speed(0, ui.material_preset[3].fan_speed);
                }
                if (preheatmode == 0 || preheatmode == 2) thermalManager.setTargetBed(ui.material_preset[3].bed_temp);
              }
              break;
          #endif
          #if (PREHEAT_COUNT >= 5)
            case PREHEAT_5:
              if (draw) {
                Draw_Menu_Item(row, ICON_Temperature, PREHEAT_5_LABEL);
              }
              else {
                thermalManager.disable_all_heaters();
                thermalManager.zero_fan_speeds();
                if (preheatmode == 0 || preheatmode == 1) {
                  thermalManager.setTargetHotend(ui.material_preset[4].hotend_temp, 0);
                  thermalManager.set_fan_speed(0, ui.material_preset[4].fan_speed);
                }
                if (preheatmode == 0 || preheatmode == 2) thermalManager.setTargetBed(ui.material_preset[4].bed_temp);
              }
              break;
          #endif
        }
        break;
    #endif
    #if ENABLED(FILAMENT_LOAD_UNLOAD_GCODES)
      case ChangeFilament:

        #define CHANGEFIL_BACK 0
        #define CHANGEFIL_LOAD (CHANGEFIL_BACK + 1)
        #define CHANGEFIL_UNLOAD (CHANGEFIL_LOAD + 1)
        #define CHANGEFIL_CHANGE (CHANGEFIL_UNLOAD + 1)
        #define CHANGEFIL_TOTAL CHANGEFIL_CHANGE

        switch (item) {
          case CHANGEFIL_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Menu(Prepare, PREPARE_CHANGEFIL);
            }
            break;
          case CHANGEFIL_LOAD:
            if (draw) {
              Draw_Menu_Item(row, ICON_WriteEEPROM, "Load Filament");
            }
            else {
              if (thermalManager.temp_hotend[0].target < thermalManager.extrude_min_temp) {
                Popup_Handler(ETemp);
              }
              else {
                if (thermalManager.temp_hotend[0].celsius < thermalManager.temp_hotend[0].target-2) {
                  Popup_Handler(Heating);
                  thermalManager.wait_for_hotend(0);
                }
                Popup_Handler(FilLoad);
                gcode.process_subcommands_now_P(PSTR("M701"));
                planner.synchronize();
                Redraw_Menu();
              }
            }
            break;
          case CHANGEFIL_UNLOAD:
            if (draw) {
              Draw_Menu_Item(row, ICON_ReadEEPROM, "Unload Filament");
            }
            else {
              if (thermalManager.temp_hotend[0].target < thermalManager.extrude_min_temp) {
                Popup_Handler(ETemp);
              }
              else {
                if (thermalManager.temp_hotend[0].celsius < thermalManager.temp_hotend[0].target-2) {
                  Popup_Handler(Heating);
                  thermalManager.wait_for_hotend(0);
                }
                Popup_Handler(FilLoad, true);
                gcode.process_subcommands_now_P(PSTR("M702"));
                planner.synchronize();
                Redraw_Menu();
              }
            }
            break;
          case CHANGEFIL_CHANGE:
            if (draw) {
              Draw_Menu_Item(row, ICON_ResumeEEPROM, "Change Filament");
            }
            else {
              if (thermalManager.temp_hotend[0].target < thermalManager.extrude_min_temp) {
                Popup_Handler(ETemp);
              }
              else {
                if (thermalManager.temp_hotend[0].celsius < thermalManager.temp_hotend[0].target-2) {
                  Popup_Handler(Heating);
                  thermalManager.wait_for_hotend(0);
                }
                Popup_Handler(FilChange);
                sprintf_P(cmd, PSTR("M600 B1 R%i"), thermalManager.temp_hotend[0].target);
                gcode.process_subcommands_now_P(cmd);
              }
            }
            break;
        }
        break;
    #endif
    case Control:

      #define CONTROL_BACK 0
      #define CONTROL_TEMP (CONTROL_BACK + 1)
      #define CONTROL_MOTION (CONTROL_TEMP + 1)
      #define CONTROL_VISUAL (CONTROL_MOTION + 1)
      #define CONTROL_ADVANCED (CONTROL_VISUAL + 1)
      #define CONTROL_SAVE (CONTROL_ADVANCED + ENABLED(EEPROM_SETTINGS))
      #define CONTROL_RESTORE (CONTROL_SAVE + ENABLED(EEPROM_SETTINGS))
      #define CONTROL_RESET (CONTROL_RESTORE + ENABLED(EEPROM_SETTINGS))
      #define CONTROL_INFO (CONTROL_RESET + 1)
      #define CONTROL_TOTAL CONTROL_INFO

      switch (item) {
        case CONTROL_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            Draw_Main_Menu(2);
          }
          break;
        case CONTROL_TEMP:
          if (draw) {
            Draw_Menu_Item(row, ICON_Temperature, "Temperature", NULL, true);
          }
          else {
            Draw_Menu(TempMenu);
          }
          break;
        case CONTROL_MOTION:
          if (draw) {
            Draw_Menu_Item(row, ICON_Motion, "Motion", NULL, true);
          }
          else {
            Draw_Menu(Motion);
          }
          break;
        case CONTROL_VISUAL:
          if (draw) {
            Draw_Menu_Item(row, ICON_PrintSize, "Visual", NULL, true);
          }
          else {
            Draw_Menu(Visual);
          }
          break;
        case CONTROL_ADVANCED:
          if (draw) {
            Draw_Menu_Item(row, ICON_Version, "Advanced", NULL, true);
          }
          else {
            Draw_Menu(Advanced);
          }
          break;
        #if ENABLED(EEPROM_SETTINGS)
          case CONTROL_SAVE:
            if (draw) {
              Draw_Menu_Item(row, ICON_WriteEEPROM, "Store Settings");
            }
            else {
              AudioFeedback(settings.save());
            }
            break;
          case CONTROL_RESTORE:
            if (draw) {
              Draw_Menu_Item(row, ICON_ReadEEPROM, "Restore Settings");
            }
            else {
              AudioFeedback(settings.load());
            }
            break;
          case CONTROL_RESET:
            if (draw) {
              Draw_Menu_Item(row, ICON_Temperature, "Reset to Defaults");
            }
            else {
              settings.reset();
              AudioFeedback();
            }
            break;
        #endif
        case CONTROL_INFO:
          if (draw) {
            Draw_Menu_Item(row, ICON_Info, "Info");
          }
          else {
            Draw_Menu(Info);
          }
          break;
      }
      break;
    case TempMenu:

      #define TEMP_BACK 0
      #define TEMP_HOTEND (TEMP_BACK + ENABLED(HAS_HOTEND))
      #define TEMP_BED (TEMP_HOTEND + ENABLED(HAS_HEATED_BED))
      #define TEMP_FAN (TEMP_BED + ENABLED(HAS_FAN))
      #define TEMP_PID (TEMP_FAN + ANY(HAS_HOTEND, HAS_HEATED_BED))
      #define TEMP_PREHEAT1 (TEMP_PID + (PREHEAT_COUNT >= 1))
      #define TEMP_PREHEAT2 (TEMP_PREHEAT1 + (PREHEAT_COUNT >= 2))
      #define TEMP_PREHEAT3 (TEMP_PREHEAT2 + (PREHEAT_COUNT >= 3))
      #define TEMP_PREHEAT4 (TEMP_PREHEAT3 + (PREHEAT_COUNT >= 4))
      #define TEMP_PREHEAT5 (TEMP_PREHEAT4 + (PREHEAT_COUNT >= 5))
      #define TEMP_TOTAL TEMP_PREHEAT5

      switch (item) {
        case TEMP_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            Draw_Menu(Control, CONTROL_TEMP);
          }
          break;
        #if HAS_HOTEND
          case TEMP_HOTEND:
            if (draw) {
              Draw_Menu_Item(row, ICON_SetEndTemp, "Hotend");
              Draw_Float(thermalManager.temp_hotend[0].target, row, false, 1);
            }
            else {
              Modify_Value(thermalManager.temp_hotend[0].target, MIN_E_TEMP, MAX_E_TEMP, 1);
            }
            break;
        #endif
        #if HAS_HEATED_BED
          case TEMP_BED:
            if (draw) {
              Draw_Menu_Item(row, ICON_SetBedTemp, "Bed");
              Draw_Float(thermalManager.temp_bed.target, row, false, 1);
            }
            else {
              Modify_Value(thermalManager.temp_bed.target, MIN_BED_TEMP, MAX_BED_TEMP, 1);
            }
            break;
        #endif
        #if HAS_FAN
          case TEMP_FAN:
            if (draw) {
              Draw_Menu_Item(row, ICON_FanSpeed, "Fan");
              Draw_Float(thermalManager.fan_speed[0], row, false, 1);
            }
            else {
              Modify_Value(thermalManager.fan_speed[0], MIN_FAN_SPEED, MAX_FAN_SPEED, 1);
            }
            break;
        #endif
        #if ANY(HAS_HOTEND, HAS_HEATED_BED)
          case TEMP_PID:
            if (draw) {
              Draw_Menu_Item(row, ICON_Step, "PID", NULL, true);
            }
            else {
              Draw_Menu(PID);
            }
            break;
        #endif
        #if (PREHEAT_COUNT >= 1)
          case TEMP_PREHEAT1:
            if (draw) {
              Draw_Menu_Item(row, ICON_Step, PREHEAT_1_LABEL, NULL, true);
            }
            else {
              Draw_Menu(Preheat1);
            }
            break;
        #endif
        #if (PREHEAT_COUNT >= 2)
          case TEMP_PREHEAT2:
            if (draw) {
              Draw_Menu_Item(row, ICON_Step, PREHEAT_2_LABEL, NULL, true);
            }
            else {
              Draw_Menu(Preheat2);
            }
            break;
        #endif
        #if (PREHEAT_COUNT >= 3)
          case TEMP_PREHEAT3:
            if (draw) {
              Draw_Menu_Item(row, ICON_Step, PREHEAT_3_LABEL, NULL, true);
            }
            else {
              Draw_Menu(Preheat3);
            }
            break;
        #endif
        #if (PREHEAT_COUNT >= 4)
          case TEMP_PREHEAT4:
            if (draw) {
              Draw_Menu_Item(row, ICON_Step, PREHEAT_4_LABEL, NULL, true);
            }
            else {
              Draw_Menu(Preheat4);
            }
            break;
        #endif
        #if (PREHEAT_COUNT >= 5)
          case TEMP_PREHEAT5:
            if (draw) {
              Draw_Menu_Item(row, ICON_Step, PREHEAT_5_LABEL, NULL, true);
            }
            else {
              Draw_Menu(Preheat5);
            }
            break;
        #endif
      }
      break;
    #if ANY(HAS_HOTEND, HAS_HEATED_BED)
      case PID:

        #define PID_BACK 0
        #define PID_HOTEND (PID_BACK + ENABLED(HAS_HOTEND))
        #define PID_BED (PID_HOTEND + ENABLED(HAS_HEATED_BED))
        #define PID_CYCLES (PID_BED + 1)
        #define PID_TOTAL PID_CYCLES

        static uint8_t PID_cycles = 5;

        switch (item) {
          case PID_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Menu(TempMenu, TEMP_PID);
            }
            break;
          #if HAS_HOTEND
            case PID_HOTEND:
              if (draw) {
                Draw_Menu_Item(row, ICON_HotendTemp, "Hotend", NULL, true);
              }
              else {
                Draw_Menu(HotendPID);
              }
              break;
          #endif
          #if HAS_HEATED_BED
            case PID_BED:
              if (draw) {
                Draw_Menu_Item(row, ICON_BedTemp, "Bed", NULL, true);
              }
              else {
                Draw_Menu(BedPID);
              }
              break;
          #endif
          case PID_CYCLES:
            if (draw) {
              Draw_Menu_Item(row, ICON_FanSpeed, "Cycles");
              Draw_Float(PID_cycles, row, false, 1);
            }
            else {
              Modify_Value(PID_cycles, 3, 50, 1);
            }
            break;
        }
        break;
    #endif
    #if HAS_HOTEND
      case HotendPID:

        #define HOTENDPID_BACK 0
        #define HOTENDPID_TUNE (HOTENDPID_BACK + 1)
        #define HOTENDPID_TEMP (HOTENDPID_TUNE + 1)
        #define HOTENDPID_KP (HOTENDPID_TEMP + 1)
        #define HOTENDPID_KI (HOTENDPID_KP + 1)
        #define HOTENDPID_KD (HOTENDPID_KI + 1)
        #define HOTENDPID_TOTAL HOTENDPID_KD

        static uint16_t PID_e_temp = 180;

        switch (item) {
          case HOTENDPID_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Menu(PID, PID_HOTEND);
            }
            break;
          case HOTENDPID_TUNE:
            if (draw) {
              Draw_Menu_Item(row, ICON_HotendTemp, "Autotune");
            }
            else {
              Popup_Handler(PIDWait);
              sprintf_P(cmd, PSTR("M303 E0 C%i S%i U1"), PID_cycles, PID_e_temp);
              gcode.process_subcommands_now_P(cmd);
              planner.synchronize();
              Redraw_Menu();
            }
            break;
          case HOTENDPID_TEMP:
            if (draw) {
              Draw_Menu_Item(row, ICON_Temperature, "Temperature");
              Draw_Float(PID_e_temp, row, false, 1);
            }
            else {
              Modify_Value(PID_e_temp, MIN_E_TEMP, MAX_E_TEMP, 1);
            }
            break;
          case HOTENDPID_KP:
            if (draw) {
              Draw_Menu_Item(row, ICON_Version, "Kp Value");
              Draw_Float(thermalManager.temp_hotend[0].pid.Kp, row, false, 100);
            }
            else {
              Modify_Value(thermalManager.temp_hotend[0].pid.Kp, 0, 5000, 100, thermalManager.updatePID);
            }
            break;
          case HOTENDPID_KI:
            if (draw) {
              Draw_Menu_Item(row, ICON_Version, "Ki Value");
              Draw_Float(unscalePID_i(thermalManager.temp_hotend[0].pid.Ki), row, false, 100);
            }
            else {
              Modify_Value(thermalManager.temp_hotend[0].pid.Ki, 0, 5000, 100, thermalManager.updatePID);
            }
            break;
          case HOTENDPID_KD:
            if (draw) {
              Draw_Menu_Item(row, ICON_Version, "Kd Value");
              Draw_Float(unscalePID_d(thermalManager.temp_hotend[0].pid.Kd), row, false, 100);
            }
            else {
              Modify_Value(thermalManager.temp_hotend[0].pid.Kd, 0, 5000, 100, thermalManager.updatePID);
            }
            break;
        }
        break;
    #endif
    #if HAS_HEATED_BED
      case BedPID:

        #define BEDPID_BACK 0
        #define BEDPID_TUNE (BEDPID_BACK + 1)
        #define BEDPID_TEMP (BEDPID_TUNE + 1)
        #define BEDPID_KP (BEDPID_TEMP + 1)
        #define BEDPID_KI (BEDPID_KP + 1)
        #define BEDPID_KD (BEDPID_KI + 1)
        #define BEDPID_TOTAL BEDPID_KD

        static uint16_t PID_bed_temp = 60;

        switch (item) {
          case BEDPID_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Menu(PID, PID_BED);
            }
            break;
          case BEDPID_TUNE:
            if (draw) {
              Draw_Menu_Item(row, ICON_HotendTemp, "Autotune");
            }
            else {
              Popup_Handler(PIDWait);
              sprintf_P(cmd, PSTR("M303 E-1 C%i S%i U1"), PID_cycles, PID_bed_temp);
              gcode.process_subcommands_now_P(cmd);
              planner.synchronize();
              Redraw_Menu();
            }
            break;
          case BEDPID_TEMP:
            if (draw) {
              Draw_Menu_Item(row, ICON_Temperature, "Temperature");
              Draw_Float(PID_bed_temp, row, false, 1);
            }
            else {
              Modify_Value(PID_bed_temp, MIN_BED_TEMP, MAX_BED_TEMP, 1);
            }
            break;
          case BEDPID_KP:
            if (draw) {
              Draw_Menu_Item(row, ICON_Version, "Kp Value");
              Draw_Float(thermalManager.temp_bed.pid.Kp, row, false, 100);
            }
            else {
              Modify_Value(thermalManager.temp_bed.pid.Kp, 0, 5000, 100, thermalManager.updatePID);
            }
            break;
          case BEDPID_KI:
            if (draw) {
              Draw_Menu_Item(row, ICON_Version, "Ki Value");
              Draw_Float(unscalePID_i(thermalManager.temp_bed.pid.Ki), row, false, 100);
            }
            else {
              Modify_Value(thermalManager.temp_bed.pid.Ki, 0, 5000, 100, thermalManager.updatePID);
            }
            break;
          case BEDPID_KD:
            if (draw) {
              Draw_Menu_Item(row, ICON_Version, "Kd Value");
              Draw_Float(unscalePID_d(thermalManager.temp_bed.pid.Kd), row, false, 100);
            }
            else {
              Modify_Value(thermalManager.temp_bed.pid.Kd, 0, 5000, 100, thermalManager.updatePID);
            }
            break;
        }
        break;
    #endif
    #if (PREHEAT_COUNT >= 1)
      case Preheat1:

        #define PREHEAT1_BACK 0
        #define PREHEAT1_HOTEND (PREHEAT1_BACK + ENABLED(HAS_HOTEND))
        #define PREHEAT1_BED (PREHEAT1_HOTEND + ENABLED(HAS_HEATED_BED))
        #define PREHEAT1_FAN (PREHEAT1_BED + ENABLED(HAS_FAN))
        #define PREHEAT1_TOTAL PREHEAT1_FAN

        switch (item) {
          case PREHEAT1_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Menu(TempMenu, TEMP_PREHEAT1);
            }
            break;
          #if HAS_HOTEND
            case PREHEAT1_HOTEND:
              if (draw) {
                Draw_Menu_Item(row, ICON_SetEndTemp, "Hotend");
                Draw_Float(ui.material_preset[0].hotend_temp, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[0].hotend_temp, MIN_E_TEMP, MAX_E_TEMP, 1);
              }
              break;
          #endif
          #if HAS_HEATED_BED
            case PREHEAT1_BED:
              if (draw) {
                Draw_Menu_Item(row, ICON_SetBedTemp, "Bed");
                Draw_Float(ui.material_preset[0].bed_temp, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[0].bed_temp, MIN_BED_TEMP, MAX_BED_TEMP, 1);
              }
              break;
          #endif
          #if HAS_FAN
            case PREHEAT1_FAN:
              if (draw) {
                Draw_Menu_Item(row, ICON_FanSpeed, "Fan");
                Draw_Float(ui.material_preset[0].fan_speed, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[0].fan_speed, MIN_FAN_SPEED, MAX_FAN_SPEED, 1);
              }
              break;
          #endif
        }
        break;
    #endif
    #if (PREHEAT_COUNT >= 2)
      case Preheat2:

        #define PREHEAT2_BACK 0
        #define PREHEAT2_HOTEND (PREHEAT2_BACK + ENABLED(HAS_HOTEND))
        #define PREHEAT2_BED (PREHEAT2_HOTEND + ENABLED(HAS_HEATED_BED))
        #define PREHEAT2_FAN (PREHEAT2_BED + ENABLED(HAS_FAN))
        #define PREHEAT2_TOTAL PREHEAT2_FAN

        switch (item) {
          case PREHEAT2_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Menu(TempMenu, TEMP_PREHEAT2);
            }
            break;
          #if HAS_HOTEND
            case PREHEAT2_HOTEND:
              if (draw) {
                Draw_Menu_Item(row, ICON_SetEndTemp, "Hotend");
                Draw_Float(ui.material_preset[1].hotend_temp, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[1].hotend_temp, MIN_E_TEMP, MAX_E_TEMP, 1);
              }
              break;
          #endif
          #if HAS_HEATED_BED
            case PREHEAT2_BED:
              if (draw) {
                Draw_Menu_Item(row, ICON_SetBedTemp, "Bed");
                Draw_Float(ui.material_preset[1].bed_temp, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[1].bed_temp, MIN_BED_TEMP, MAX_BED_TEMP, 1);
              }
              break;
          #endif
          #if HAS_FAN
            case PREHEAT2_FAN:
              if (draw) {
                Draw_Menu_Item(row, ICON_FanSpeed, "Fan");
                Draw_Float(ui.material_preset[1].fan_speed, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[1].fan_speed, MIN_FAN_SPEED, MAX_FAN_SPEED, 1);
              }
              break;
          #endif
        }
        break;
    #endif
    #if (PREHEAT_COUNT >= 3)
      case Preheat3:

        #define PREHEAT3_BACK 0
        #define PREHEAT3_HOTEND (PREHEAT3_BACK + ENABLED(HAS_HOTEND))
        #define PREHEAT3_BED (PREHEAT3_HOTEND + ENABLED(HAS_HEATED_BED))
        #define PREHEAT3_FAN (PREHEAT3_BED + ENABLED(HAS_FAN))
        #define PREHEAT3_TOTAL PREHEAT3_FAN

        switch (item) {
          case PREHEAT3_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Menu(TempMenu, TEMP_PREHEAT3);
            }
            break;
          #if HAS_HOTEND
            case PREHEAT3_HOTEND:
              if (draw) {
                Draw_Menu_Item(row, ICON_SetEndTemp, "Hotend");
                Draw_Float(ui.material_preset[2].hotend_temp, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[2].hotend_temp, MIN_E_TEMP, MAX_E_TEMP, 1);
              }
              break;
          #endif
          #if HAS_HEATED_BED
            case PREHEAT3_BED:
              if (draw) {
                Draw_Menu_Item(row, ICON_SetBedTemp, "Bed");
                Draw_Float(ui.material_preset[2].bed_temp, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[2].bed_temp, MIN_BED_TEMP, MAX_BED_TEMP, 1);
              }
              break;
          #endif
          #if HAS_FAN
            case PREHEAT3_FAN:
              if (draw) {
                Draw_Menu_Item(row, ICON_FanSpeed, "Fan");
                Draw_Float(ui.material_preset[2].fan_speed, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[2].fan_speed, MIN_FAN_SPEED, MAX_FAN_SPEED, 1);
              }
              break;
          #endif
        }
        break;
    #endif
    #if (PREHEAT_COUNT >= 4)
      case Preheat4:

        #define PREHEAT4_BACK 0
        #define PREHEAT4_HOTEND (PREHEAT4_BACK + ENABLED(HAS_HOTEND))
        #define PREHEAT4_BED (PREHEAT4_HOTEND + ENABLED(HAS_HEATED_BED))
        #define PREHEAT4_FAN (PREHEAT4_BED + ENABLED(HAS_FAN))
        #define PREHEAT4_TOTAL PREHEAT4_FAN

        switch (item) {
          case PREHEAT4_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Menu(TempMenu, TEMP_PREHEAT4);
            }
            break;
          #if HAS_HOTEND
            case PREHEAT4_HOTEND:
              if (draw) {
                Draw_Menu_Item(row, ICON_SetEndTemp, "Hotend");
                Draw_Float(ui.material_preset[3].hotend_temp, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[3].hotend_temp, MIN_E_TEMP, MAX_E_TEMP, 1);
              }
              break;
          #endif
          #if HAS_HEATED_BED
            case PREHEAT4_BED:
              if (draw) {
                Draw_Menu_Item(row, ICON_SetBedTemp, "Bed");
                Draw_Float(ui.material_preset[3].bed_temp, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[3].bed_temp, MIN_BED_TEMP, MAX_BED_TEMP, 1);
              }
              break;
          #endif
          #if HAS_FAN
            case PREHEAT4_FAN:
              if (draw) {
                Draw_Menu_Item(row, ICON_FanSpeed, "Fan");
                Draw_Float(ui.material_preset[3].fan_speed, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[3].fan_speed, MIN_FAN_SPEED, MAX_FAN_SPEED, 1);
              }
              break;
          #endif
        }
        break;
    #endif
    #if (PREHEAT_COUNT >= 5)
      case Preheat5:

        #define PREHEAT5_BACK 0
        #define PREHEAT5_HOTEND (PREHEAT5_BACK + ENABLED(HAS_HOTEND))
        #define PREHEAT5_BED (PREHEAT5_HOTEND + ENABLED(HAS_HEATED_BED))
        #define PREHEAT5_FAN (PREHEAT5_BED + ENABLED(HAS_FAN))
        #define PREHEAT5_TOTAL PREHEAT5_FAN

        switch (item) {
          case PREHEAT5_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Menu(TempMenu, TEMP_PREHEAT5);
            }
            break;
          #if HAS_HOTEND
            case PREHEAT5_HOTEND:
              if (draw) {
                Draw_Menu_Item(row, ICON_SetEndTemp, "Hotend");
                Draw_Float(ui.material_preset[4].hotend_temp, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[4].hotend_temp, MIN_E_TEMP, MAX_E_TEMP, 1);
              }
              break;
          #endif
          #if HAS_HEATED_BED
            case PREHEAT5_BED:
              if (draw) {
                Draw_Menu_Item(row, ICON_SetBedTemp, "Bed");
                Draw_Float(ui.material_preset[4].bed_temp, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[4].bed_temp, MIN_BED_TEMP, MAX_BED_TEMP, 1);
              }
              break;
          #endif
          #if HAS_FAN
            case PREHEAT5_FAN:
              if (draw) {
                Draw_Menu_Item(row, ICON_FanSpeed, "Fan");
                Draw_Float(ui.material_preset[4].fan_speed, row, false, 1);
              }
              else {
                Modify_Value(ui.material_preset[4].fan_speed, MIN_FAN_SPEED, MAX_FAN_SPEED, 1);
              }
              break;
          #endif
        }
        break;
    #endif
    case Motion:

      #define MOTION_BACK 0
      #define MOTION_HOMEOFFSETS (MOTION_BACK + 1)
      #define MOTION_SPEED (MOTION_HOMEOFFSETS + 1)
      #define MOTION_ACCEL (MOTION_SPEED + 1)
      #define MOTION_JERK (MOTION_ACCEL + ENABLED(HAS_CLASSIC_JERK))
      #define MOTION_STEPS (MOTION_JERK + 1)
      #define MOTION_FLOW (MOTION_STEPS + ENABLED(HAS_HOTEND))
      #define MOTION_TOTAL MOTION_FLOW

      switch (item) {
        case MOTION_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            Draw_Menu(Control, CONTROL_MOTION);
          }
          break;
        case MOTION_HOMEOFFSETS:
          if (draw) {
            Draw_Menu_Item(row, ICON_SetHome, "Home Offsets", NULL, true);
          }
          else {
            Draw_Menu(HomeOffsets);
          }
          break;
        case MOTION_SPEED:
          if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeed, "Max Speed", NULL, true);
          }
          else {
            Draw_Menu(MaxSpeed);
          }
          break;
        case MOTION_ACCEL:
          if (draw) {
            Draw_Menu_Item(row, ICON_MaxAccelerated, "Max Acceleration", NULL, true);
          }
          else {
            Draw_Menu(MaxAcceleration);
          }
          break;
        #if HAS_CLASSIC_JERK
          case MOTION_JERK:
            if (draw) {
              Draw_Menu_Item(row, ICON_MaxJerk, "Max Jerk", NULL, true);
            }
            else {
              Draw_Menu(MaxJerk);
            }
            break;
        #endif
        case MOTION_STEPS:
          if (draw) {
            Draw_Menu_Item(row, ICON_Step, "Steps/mm", NULL, true);
          }
          else {
            Draw_Menu(Steps);
          }
          break;
        #if HAS_HOTEND
          case MOTION_FLOW:
            if (draw) {
              Draw_Menu_Item(row, ICON_Speed, "Flow Rate");
              Draw_Float(planner.flow_percentage[0], row, false, 1);
            }
            else {
              Modify_Value(planner.flow_percentage[0], MIN_FLOW_RATE, MAX_FLOW_RATE, 1);
            }
            break;
        #endif
      }
      break;
    case HomeOffsets:

      #define HOMEOFFSETS_BACK 0
      #define HOMEOFFSETS_XOFFSET (HOMEOFFSETS_BACK + 1)
      #define HOMEOFFSETS_YOFFSET (HOMEOFFSETS_XOFFSET + 1)
      #define HOMEOFFSETS_TOTAL HOMEOFFSETS_YOFFSET

      switch (item) {
        case HOMEOFFSETS_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            Draw_Menu(Motion, MOTION_HOMEOFFSETS);
          }
          break;
        case HOMEOFFSETS_XOFFSET:
          if (draw) {
            Draw_Menu_Item(row, ICON_StepX, "X Offset");
            Draw_Float(home_offset.x, row, false, 100);
          }
          else {
            Modify_Value(home_offset.x, -MAX_XY_OFFSET, MAX_XY_OFFSET, 100);
          }
          break;
        case HOMEOFFSETS_YOFFSET:
          if (draw) {
            Draw_Menu_Item(row, ICON_StepY, "Y Offset");
            Draw_Float(home_offset.y, row, false, 100);
          }
          else {
            Modify_Value(home_offset.y, -MAX_XY_OFFSET, MAX_XY_OFFSET, 100);
          }
          break;
      }
      break;
    case MaxSpeed:

      #define SPEED_BACK 0
      #define SPEED_X (SPEED_BACK + 1)
      #define SPEED_Y (SPEED_X + 1)
      #define SPEED_Z (SPEED_Y + 1)
      #define SPEED_E (SPEED_Z + ENABLED(HAS_HOTEND))
      #define SPEED_TOTAL SPEED_E

      switch (item) {
        case SPEED_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            Draw_Menu(Motion, MOTION_SPEED);
          }
          break;
        case SPEED_X:
          if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeedX, "X Axis");
            Draw_Float(planner.settings.max_feedrate_mm_s[X_AXIS], row, false, 1);
          }
          else {
            Modify_Value(planner.settings.max_feedrate_mm_s[X_AXIS], 0, default_max_feedrate[X_AXIS]*2, 1);
          }
          break;
        case SPEED_Y:
          if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeedY, "Y Axis");
            Draw_Float(planner.settings.max_feedrate_mm_s[Y_AXIS], row, false, 1);
          }
          else {
            Modify_Value(planner.settings.max_feedrate_mm_s[Y_AXIS], 0, default_max_feedrate[Y_AXIS]*2, 1);
          }
          break;
        case SPEED_Z:
          if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeedZ, "Z Axis");
            Draw_Float(planner.settings.max_feedrate_mm_s[Z_AXIS], row, false, 1);
          }
          else {
            Modify_Value(planner.settings.max_feedrate_mm_s[Z_AXIS], 0, default_max_feedrate[Z_AXIS]*2, 1);
          }
          break;
        #if HAS_HOTEND
          case SPEED_E:
            if (draw) {
              Draw_Menu_Item(row, ICON_MaxSpeedE, "Extruder");
              Draw_Float(planner.settings.max_feedrate_mm_s[E_AXIS], row, false, 1);
            }
            else {
              Modify_Value(planner.settings.max_feedrate_mm_s[E_AXIS], 0, default_max_feedrate[E_AXIS]*2, 1);
            }
            break;
        #endif
      }
      break;
    case MaxAcceleration:

      #define ACCEL_BACK 0
      #define ACCEL_X (ACCEL_BACK + 1)
      #define ACCEL_Y (ACCEL_X + 1)
      #define ACCEL_Z (ACCEL_Y + 1)
      #define ACCEL_E (ACCEL_Z + ENABLED(HAS_HOTEND))
      #define ACCEL_TOTAL ACCEL_E

      switch (item) {
        case ACCEL_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            Draw_Menu(Motion, MOTION_ACCEL);
          }
          break;
        case ACCEL_X:
          if (draw) {
            Draw_Menu_Item(row, ICON_MaxAccX, "X Axis");
            Draw_Float(planner.settings.max_acceleration_mm_per_s2[X_AXIS], row, false, 1);
          }
          else {
            Modify_Value(planner.settings.max_acceleration_mm_per_s2[X_AXIS], 0, default_max_acceleration[X_AXIS]*2, 1);
          }
          break;
        case ACCEL_Y:
          if (draw) {
            Draw_Menu_Item(row, ICON_MaxAccY, "Y Axis");
            Draw_Float(planner.settings.max_acceleration_mm_per_s2[Y_AXIS], row, false, 1);
          }
          else {
            Modify_Value(planner.settings.max_acceleration_mm_per_s2[Y_AXIS], 0, default_max_acceleration[Y_AXIS]*2, 1);
          }
          break;
        case ACCEL_Z:
          if (draw) {
            Draw_Menu_Item(row, ICON_MaxAccZ, "Z Axis");
            Draw_Float(planner.settings.max_acceleration_mm_per_s2[Z_AXIS], row, false, 1);
          }
          else {
            Modify_Value(planner.settings.max_acceleration_mm_per_s2[Z_AXIS], 0, default_max_acceleration[Z_AXIS]*2, 1);
          }
          break;
        #if HAS_HOTEND
          case ACCEL_E:
            if (draw) {
              Draw_Menu_Item(row, ICON_MaxAccE, "Extruder");
              Draw_Float(planner.settings.max_acceleration_mm_per_s2[E_AXIS], row, false, 1);
            }
            else {
              Modify_Value(planner.settings.max_acceleration_mm_per_s2[E_AXIS], 0, default_max_acceleration[E_AXIS]*2, 1);
            }
            break;
        #endif
      }
      break;
    #if HAS_CLASSIC_JERK
      case MaxJerk:

        #define JERK_BACK 0
        #define JERK_X (JERK_BACK + 1)
        #define JERK_Y (JERK_X + 1)
        #define JERK_Z (JERK_Y + 1)
        #define JERK_E (JERK_Z + ENABLED(HAS_HOTEND))
        #define JERK_TOTAL JERK_E

        switch (item) {
          case JERK_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Menu(Motion, MOTION_JERK);
            }
            break;
          case JERK_X:
            if (draw) {
              Draw_Menu_Item(row, ICON_MaxSpeedJerkX, "X Axis");
              Draw_Float(planner.max_jerk[X_AXIS], row, false, 10);
            }
            else {
              Modify_Value(planner.max_jerk[X_AXIS], 0, default_max_jerk[X_AXIS]*2, 10);
            }
            break;
          case JERK_Y:
            if (draw) {
              Draw_Menu_Item(row, ICON_MaxSpeedJerkY, "Y Axis");
              Draw_Float(planner.max_jerk[Y_AXIS], row, false, 10);
            }
            else {
              Modify_Value(planner.max_jerk[Y_AXIS], 0, default_max_jerk[Y_AXIS]*2, 10);
            }
            break;
          case JERK_Z:
            if (draw) {
              Draw_Menu_Item(row, ICON_MaxSpeedJerkZ, "Z Axis");
              Draw_Float(planner.max_jerk[Z_AXIS], row, false, 10);
            }
            else {
              Modify_Value(planner.max_jerk[Z_AXIS], 0, default_max_jerk[Z_AXIS]*2, 10);
            }
            break;
          #if HAS_HOTEND
            case JERK_E:
              if (draw) {
                Draw_Menu_Item(row, ICON_MaxSpeedJerkE, "Extruder");
                Draw_Float(planner.max_jerk[E_AXIS], row, false, 10);
              }
              else {
                Modify_Value(planner.max_jerk[E_AXIS], 0, default_max_jerk[E_AXIS]*2, 10);
              }
              break;
          #endif
        }
        break;
    #endif
    case Steps:

      #define STEPS_BACK 0
      #define STEPS_X (STEPS_BACK + 1)
      #define STEPS_Y (STEPS_X + 1)
      #define STEPS_Z (STEPS_Y + 1)
      #define STEPS_E (STEPS_Z + ENABLED(HAS_HOTEND))
      #define STEPS_TOTAL STEPS_E

      switch (item) {
        case STEPS_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            Draw_Menu(Motion, MOTION_STEPS);
          }
          break;
        case STEPS_X:
          if (draw) {
            Draw_Menu_Item(row, ICON_StepX, "X Axis");
            Draw_Float(planner.settings.axis_steps_per_mm[X_AXIS], row, false, 10);
          }
          else {
            Modify_Value(planner.settings.axis_steps_per_mm[X_AXIS], 0, default_steps[X_AXIS]*2, 10);
          }
          break;
        case STEPS_Y:
          if (draw) {
            Draw_Menu_Item(row, ICON_StepY, "Y Axis");
            Draw_Float(planner.settings.axis_steps_per_mm[Y_AXIS], row, false, 10);
          }
          else {
            Modify_Value(planner.settings.axis_steps_per_mm[Y_AXIS], 0, default_steps[Y_AXIS]*2, 10);
          }
          break;
        case STEPS_Z:
          if (draw) {
            Draw_Menu_Item(row, ICON_StepZ, "Z Axis");
            Draw_Float(planner.settings.axis_steps_per_mm[Z_AXIS], row, false, 10);
          }
          else {
            Modify_Value(planner.settings.axis_steps_per_mm[Z_AXIS], 0, default_steps[Z_AXIS]*2, 10);
          }
          break;
        #if HAS_HOTEND
          case STEPS_E:
            if (draw) {
              Draw_Menu_Item(row, ICON_StepE, "Extruder");
              Draw_Float(planner.settings.axis_steps_per_mm[E_AXIS], row, false, 10);
            }
            else {
              Modify_Value(planner.settings.axis_steps_per_mm[E_AXIS], 0, 1000, 10);
            }
            break;
        #endif
      }
      break;

    case Visual:

      #define VISUAL_BACK 0
      #define VISUAL_BACKLIGHT (VISUAL_BACK + 1)
      #define VISUAL_BRIGHTNESS (VISUAL_BACKLIGHT + 1)
      #define VISUAL_TIME_FORMAT (VISUAL_BRIGHTNESS + 1)
      #define VISUAL_COLOR_THEMES (VISUAL_TIME_FORMAT + 1)
      #define VISUAL_TOTAL VISUAL_COLOR_THEMES

      switch (item) {
        case VISUAL_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            Draw_Menu(Control, CONTROL_VISUAL);
          }
          break;
        case VISUAL_BACKLIGHT:
          if (draw) {
            Draw_Menu_Item(row, ICON_Brightness, "Display Off");
          }
          else {
            ui.set_brightness(0);
          }
          break;
        case VISUAL_BRIGHTNESS:
          if (draw) {
            Draw_Menu_Item(row, ICON_Brightness, "LCD Brightness");
            Draw_Float(ui.brightness, row, false, 1);
          }
          else {
            Modify_Value(ui.brightness, MIN_LCD_BRIGHTNESS, MAX_LCD_BRIGHTNESS, 1, ui.refresh_brightness);
          }
          break;
        case VISUAL_TIME_FORMAT:
          if (draw) {
            Draw_Menu_Item(row, ICON_PrintTime, "Progress as __h__m");
            Draw_Checkbox(row, eeprom_settings.time_format_textual);
          }
          else {
            eeprom_settings.time_format_textual = !eeprom_settings.time_format_textual;
            Draw_Checkbox(row, eeprom_settings.time_format_textual);
          }
          break;
        case VISUAL_COLOR_THEMES:
        if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeed, "UI Color Settings", NULL, true);
          }
          else {
            Draw_Menu(ColorSettings);
          }
        break;  
      }
      break;
    case ColorSettings:

        #define COLORSETTINGS_BACK 0
        #define COLORSETTINGS_CURSOR (COLORSETTINGS_BACK + 1)
        #define COLORSETTINGS_SPLIT_LINE (COLORSETTINGS_CURSOR + 1)
        #define COLORSETTINGS_MENU_TOP_TXT (COLORSETTINGS_SPLIT_LINE + 1)
        #define COLORSETTINGS_MENU_TOP_BG (COLORSETTINGS_MENU_TOP_TXT + 1)
        #define COLORSETTINGS_HIGHLIGHT_BORDER (COLORSETTINGS_MENU_TOP_BG + 1)
        #define COLORSETTINGS_PROGRESS_PERCENT (COLORSETTINGS_HIGHLIGHT_BORDER + 1)
        #define COLORSETTINGS_PROGRESS_TIME (COLORSETTINGS_PROGRESS_PERCENT + 1)
        #define COLORSETTINGS_PROGRESS_STATUS_BAR (COLORSETTINGS_PROGRESS_TIME + 1)
        #define COLORSETTINGS_PROGRESS_STATUS_AREA (COLORSETTINGS_PROGRESS_STATUS_BAR + 1)
        #define COLORSETTINGS_PROGRESS_COORDINATES (COLORSETTINGS_PROGRESS_STATUS_AREA + 1)
        #define COLORSETTINGS_PROGRESS_COORDINATES_LINE (COLORSETTINGS_PROGRESS_COORDINATES + 1)
        #define COLORSETTINGS_TOTAL COLORSETTINGS_PROGRESS_COORDINATES_LINE

        switch (item) {
        case COLORSETTINGS_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            Draw_Menu(Visual, VISUAL_COLOR_THEMES);
          }
          break;
        case COLORSETTINGS_CURSOR:
           if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeed, "Cursor");
            Draw_Option(eeprom_settings.cursor_color, color_names, row, false, true);
          }
          else {
            Modify_Option(eeprom_settings.cursor_color, color_names, Custom_Colors);
          }
          break;
          case COLORSETTINGS_SPLIT_LINE:
           if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeed, "Menu Split Line");
            Draw_Option(eeprom_settings.menu_split_line, color_names, row, false, true);
          }
          else {
            Modify_Option(eeprom_settings.menu_split_line, color_names, Custom_Colors);
          }
          break;
        case COLORSETTINGS_MENU_TOP_TXT:
           if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeed, "Menu Header Text");
            Draw_Option(eeprom_settings.menu_top_txt, color_names, row, false, true);
          }
          else {
            Modify_Option(eeprom_settings.menu_top_txt, color_names, Custom_Colors);
          }
          break;
         case COLORSETTINGS_MENU_TOP_BG:
           if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeed, "Menu Header Bg");
            Draw_Option(eeprom_settings.menu_top_bg, color_names, row, false, true);
          }
          else {
            Modify_Option(eeprom_settings.menu_top_bg, color_names, Custom_Colors);
          }
          break;  
          case COLORSETTINGS_HIGHLIGHT_BORDER:
           if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeed, "Highlight Box");
            Draw_Option(eeprom_settings.highlight_box, color_names, row, false, true);
          }
          else {
            Modify_Option(eeprom_settings.highlight_box, color_names, Custom_Colors);
          }
          break; 
          case COLORSETTINGS_PROGRESS_PERCENT:
           if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeed, "Progress Percent");
            Draw_Option(eeprom_settings.progress_percent, color_names, row, false, true);
          }
          else {
            Modify_Option(eeprom_settings.progress_percent, color_names, Custom_Colors);
          }
          break;  
          case COLORSETTINGS_PROGRESS_TIME:
           if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeed, "Progress Time");
            Draw_Option(eeprom_settings.progress_time, color_names, row, false, true);
          }
          else {
            Modify_Option(eeprom_settings.progress_time, color_names, Custom_Colors);
          }
          break;           
          case COLORSETTINGS_PROGRESS_STATUS_BAR:
           if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeed, "Status Bar Text");
            Draw_Option(eeprom_settings.status_bar_text, color_names, row, false, true);
          }
          else {
            Modify_Option(eeprom_settings.status_bar_text, color_names, Custom_Colors);
          }
          break;  
          case COLORSETTINGS_PROGRESS_STATUS_AREA:
           if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeed, "Status Area Text");
            Draw_Option(eeprom_settings.status_area_text, color_names, row, false, true);
          }
          else {
            Modify_Option(eeprom_settings.status_area_text, color_names, Custom_Colors);
          }
          break;  
          case COLORSETTINGS_PROGRESS_COORDINATES:
           if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeed, "Coordinates Text");
            Draw_Option(eeprom_settings.coordinates_text, color_names, row, false, true);
          }
          else {
            Modify_Option(eeprom_settings.coordinates_text, color_names, Custom_Colors);
          }
          break;     
          case COLORSETTINGS_PROGRESS_COORDINATES_LINE:
           if (draw) {
            Draw_Menu_Item(row, ICON_MaxSpeed, "Coordinates Line");
            Draw_Option(eeprom_settings.coordinates_split_line, color_names, row, false, true);
          }
          else {
            Modify_Option(eeprom_settings.coordinates_split_line, color_names, Custom_Colors);
          }
          break;                                              
        }
        break; 
    case Advanced:

      #define ADVANCED_BACK 0
      #define ADVANCED_BEEPER (ADVANCED_BACK + 1)
      #define ADVANCED_PROBE (ADVANCED_BEEPER + ENABLED(HAS_BED_PROBE))
      #define ADVANCED_CORNER (ADVANCED_PROBE + 1)
      #define ADVANCED_LA (ADVANCED_CORNER + ENABLED(LIN_ADVANCE))
      #define ADVANCED_LOAD (ADVANCED_LA + ENABLED(ADVANCED_PAUSE_FEATURE))
      #define ADVANCED_UNLOAD (ADVANCED_LOAD + ENABLED(ADVANCED_PAUSE_FEATURE))
      #define ADVANCED_COLD_EXTRUDE  (ADVANCED_UNLOAD + ENABLED(PREVENT_COLD_EXTRUSION))
      #define ADVANCED_FILSENSORENABLED (ADVANCED_COLD_EXTRUDE + ENABLED(FILAMENT_RUNOUT_SENSOR))
      #define ADVANCED_FILSENSORDISTANCE (ADVANCED_FILSENSORENABLED + ENABLED(HAS_FILAMENT_RUNOUT_DISTANCE))
      #define ADVANCED_POWER_LOSS (ADVANCED_FILSENSORDISTANCE + ENABLED(POWER_LOSS_RECOVERY))
      #define ADVANCED_TOTAL ADVANCED_POWER_LOSS

      switch (item) {
        case ADVANCED_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            Draw_Menu(Control, CONTROL_ADVANCED);
          }
          break;
        case ADVANCED_BEEPER:
          if (draw) {
            Draw_Menu_Item(row, ICON_Version, "LCD Beeper");
            Draw_Checkbox(row, eeprom_settings.beeperenable);
          }
          else {
            eeprom_settings.beeperenable = !eeprom_settings.beeperenable;
            Draw_Checkbox(row, eeprom_settings.beeperenable);
          }
          break;
        #if HAS_BED_PROBE
          case ADVANCED_PROBE:
            if (draw) {
              Draw_Menu_Item(row, ICON_StepX, "Probe", NULL, true);
            }
            else {
              Draw_Menu(ProbeMenu);
            }
            break;
        #endif
        case ADVANCED_CORNER:
          if (draw) {
            Draw_Menu_Item(row, ICON_MaxAccelerated, "Bed Screw Inset");
            Draw_Float(corner_pos, row, false, 10);
          }
          else {
            Modify_Value(corner_pos, 1, 100, 10);
          }
          break;
        #if ENABLED(LIN_ADVANCE)
          case ADVANCED_LA:
            if (draw) {
              Draw_Menu_Item(row, ICON_MaxAccelerated, "Lin Advance Kp");
              Draw_Float(planner.extruder_advance_K[0], row, false, 100);
            }
            else {
              Modify_Value(planner.extruder_advance_K[0], 0, 10, 100);
            }
            break;
        #endif
        #if ENABLED(ADVANCED_PAUSE_FEATURE)
          case ADVANCED_LOAD:
            if (draw) {
              Draw_Menu_Item(row, ICON_WriteEEPROM, "Load Length");
              Draw_Float(fc_settings[0].load_length, row, false, 1);
            }
            else {
              Modify_Value(fc_settings[0].load_length, 0, EXTRUDE_MAXLENGTH, 1);
            }
            break;
          case ADVANCED_UNLOAD:
            if (draw) {
              Draw_Menu_Item(row, ICON_ReadEEPROM, "Unload Length");
              Draw_Float(fc_settings[0].unload_length, row, false, 1);
            }
            else {
              Modify_Value(fc_settings[0].unload_length, 0, EXTRUDE_MAXLENGTH, 1);
            }
            break;
        #endif
        #if ENABLED(PREVENT_COLD_EXTRUSION)
          case ADVANCED_COLD_EXTRUDE:
            if (draw) {
              Draw_Menu_Item(row, ICON_Cool, "Min Extrusion T");
              Draw_Float(thermalManager.extrude_min_temp, row, false, 1);
            }
            else {
              Modify_Value(thermalManager.extrude_min_temp, 0, MAX_E_TEMP, 1);
              thermalManager.allow_cold_extrude = (thermalManager.extrude_min_temp == 0);
            }
            break;
        #endif
        #if ENABLED(FILAMENT_RUNOUT_SENSOR)
          case ADVANCED_FILSENSORENABLED:
            if (draw) {
              Draw_Menu_Item(row, ICON_Extruder, "Filament Sensor");
              Draw_Checkbox(row, runout.enabled);
            }
            else {
              runout.enabled = !runout.enabled;
              Draw_Checkbox(row, runout.enabled);
            }
            break;
          #if ENABLED(HAS_FILAMENT_RUNOUT_DISTANCE)
            case ADVANCED_FILSENSORDISTANCE:
              if (draw) {
                Draw_Menu_Item(row, ICON_MaxAccE, "Runout Distance");
                Draw_Float(runout.runout_distance(), row, false, 10);
              }
              else {
                Modify_Value(runout.runout_distance(), 0, 999, 10);
              }
              break;
          #endif
        #endif
        #if ENABLED(POWER_LOSS_RECOVERY)
          case ADVANCED_POWER_LOSS:
            if (draw) {
              Draw_Menu_Item(row, ICON_Motion, "Power-loss recovery");
              Draw_Checkbox(row, recovery.enabled);
            }
            else {
              recovery.enable(!recovery.enabled);
              Draw_Checkbox(row, recovery.enabled);
            }
            break;
        #endif 
      }
      break;
    #if HAS_BED_PROBE
      case ProbeMenu:

        #define PROBE_BACK 0
        #define PROBE_XOFFSET (PROBE_BACK + 1)
        #define PROBE_YOFFSET (PROBE_XOFFSET + 1)
        #define PROBE_TEST (PROBE_YOFFSET + 1)
        #define PROBE_TEST_COUNT (PROBE_TEST + 1)
        #define PROBE_TOTAL PROBE_TEST_COUNT

        static uint8_t testcount = 4;

        switch (item) {
          case PROBE_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Menu(Advanced, ADVANCED_PROBE);
            }
            break;
          
            case PROBE_XOFFSET:
              if (draw) {
                Draw_Menu_Item(row, ICON_StepX, "Probe X Offset");
                Draw_Float(probe.offset.x, row, false, 10);
              }
              else {
                Modify_Value(probe.offset.x, -MAX_XY_OFFSET, MAX_XY_OFFSET, 10);
              }
              break;
            case PROBE_YOFFSET:
              if (draw) {
                Draw_Menu_Item(row, ICON_StepY, "Probe Y Offset");
                Draw_Float(probe.offset.y, row, false, 10);
              }
              else {
                Modify_Value(probe.offset.y, -MAX_XY_OFFSET, MAX_XY_OFFSET, 10);
              }
              break;
            case PROBE_TEST:
              if (draw) {
                Draw_Menu_Item(row, ICON_StepY, "M48 Probe Test");
              }
              else {
                sprintf_P(cmd, PSTR("G28O\nM48 X%s Y%s P%i"), dtostrf((X_BED_SIZE + X_MIN_POS)/2.0f, 1, 3, str_1), dtostrf((Y_BED_SIZE + Y_MIN_POS)/2.0f, 1, 3, str_2), testcount);
                gcode.process_subcommands_now_P(cmd);
              }
              break;
            case PROBE_TEST_COUNT:
              if (draw) {
                Draw_Menu_Item(row, ICON_StepY, "Probe Test Count");
                Draw_Float(testcount, row, false, 1);
              }
              else {
                Modify_Value(testcount, 4, 50, 1);
              }
              break;
        }
        break;
    #endif

    case InfoMain:
    case Info:

      #define INFO_BACK 0
      #define INFO_PRINTCOUNT (INFO_BACK + ENABLED(PRINTCOUNTER))
      #define INFO_PRINTTIME (INFO_PRINTCOUNT + ENABLED(PRINTCOUNTER))
      #define INFO_SIZE (INFO_PRINTTIME + 1)
      #define INFO_VERSION (INFO_SIZE + 1)
      #define INFO_CONTACT (INFO_VERSION + 1)
      #define INFO_TOTAL INFO_BACK

      switch (item) {
        case INFO_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
            
            #if ENABLED(PRINTCOUNTER)
              char row1[50], row2[50], buf[32];
              printStatistics ps = print_job_timer.getStats();

              sprintf(row1, "%i prints, %i finished", ps.totalPrints, ps.finishedPrints);
              sprintf(row2, "%s m filament used", dtostrf(ps.filamentUsed / 1000, 1, 2, str_1));
              Draw_Menu_Item(INFO_PRINTCOUNT, ICON_HotendTemp, row1, row2, false, true);

              duration_t(print_job_timer.getStats().printTime).toString(buf);
              sprintf(row1, "Printed: %s", buf);
              duration_t(print_job_timer.getStats().longestPrint).toString(buf);
              sprintf(row2, "Longest: %s", buf);
              Draw_Menu_Item(INFO_PRINTTIME, ICON_PrintTime, row1, row2, false, true);
            #endif
            
            Draw_Menu_Item(INFO_SIZE, ICON_PrintSize, MACHINE_SIZE, NULL, false, true);
            Draw_Menu_Item(INFO_VERSION, ICON_Version, SHORT_BUILD_VERSION, "Build Number: v" BUILD_NUMBER, false, true);
            Draw_Menu_Item(INFO_CONTACT, ICON_Contact, CORP_WEBSITE_E, NULL, false, true);
          }
          else {
            if (menu == Info)
              Draw_Menu(Control, CONTROL_INFO);
            else 
              Draw_Main_Menu(3);
          }
          break;
      }
      break;
    #if HAS_MESH
      case Leveling:

        #define LEVELING_BACK 0
        #define LEVELING_ACTIVE (LEVELING_BACK + 1)
        #define LEVELING_GET_TILT (LEVELING_ACTIVE + BOTH(HAS_BED_PROBE, AUTO_BED_LEVELING_UBL))
        #define LEVELING_GET_MESH (LEVELING_GET_TILT + 1)
        #define LEVELING_MANUAL (LEVELING_GET_MESH + 1)
        #define LEVELING_VIEW (LEVELING_MANUAL + 1)
        #define LEVELING_SETTINGS (LEVELING_VIEW + 1)
        #define LEVELING_SLOT (LEVELING_SETTINGS + ENABLED(AUTO_BED_LEVELING_UBL))
        #define LEVELING_LOAD (LEVELING_SLOT + ENABLED(AUTO_BED_LEVELING_UBL))
        #define LEVELING_SAVE (LEVELING_LOAD + ENABLED(AUTO_BED_LEVELING_UBL))
        #define LEVELING_TOTAL LEVELING_SAVE

        switch (item) {
          case LEVELING_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Main_Menu(3);
            }
            break;
          case LEVELING_ACTIVE:
            if (draw) {
              Draw_Menu_Item(row, ICON_StockConfiguraton, "Leveling Active");
              Draw_Checkbox(row, planner.leveling_active);
            }
            else {
              if (!planner.leveling_active) {
                set_bed_leveling_enabled(!planner.leveling_active);
                if (!planner.leveling_active) {
                  Confirm_Handler(LevelError);
                  break;
                }
              }
              else {
                set_bed_leveling_enabled(!planner.leveling_active);
              }
              Draw_Checkbox(row, planner.leveling_active);
            }
            break;
          #if BOTH(HAS_BED_PROBE, AUTO_BED_LEVELING_UBL)
            case LEVELING_GET_TILT:
              if (draw) {
                Draw_Menu_Item(row, ICON_Tilt, "Autotilt Current Mesh");
              }
              else {
                if (ubl.storage_slot < 0) {
                  Popup_Handler(MeshSlot);
                  break;
                }
                Popup_Handler(Home);
                gcode.home_all_axes(true);
                Popup_Handler(Level);
                if (mesh_conf.tilt_grid > 1) {
                  sprintf_P(cmd, PSTR("G29 J%i"), mesh_conf.tilt_grid);
                }
                else {
                  sprintf_P(cmd, PSTR("G29 J"));
                }
                gcode.process_subcommands_now_P(cmd);
                planner.synchronize();
                Redraw_Menu();
              }
              break;
          #endif
          case LEVELING_GET_MESH:
            if (draw) {
              Draw_Menu_Item(row, ICON_Mesh, "Create New Mesh");
            }
            else {
              Popup_Handler(Home);
              gcode.home_all_axes(true);
              #if ENABLED(AUTO_BED_LEVELING_UBL)
                #if ENABLED(PREHEAT_BEFORE_LEVELING)
                  Popup_Handler(Heating);
                  if (thermalManager.degTargetHotend(0) < LEVELING_NOZZLE_TEMP)
                    thermalManager.setTargetHotend(LEVELING_NOZZLE_TEMP, 0);
                  if (thermalManager.degTargetBed() < LEVELING_BED_TEMP)
                    thermalManager.setTargetBed(LEVELING_BED_TEMP);
                  thermalManager.wait_for_hotend(0);
                  thermalManager.wait_for_bed_heating();
                #endif
                #if HAS_BED_PROBE
                  Popup_Handler(Level);
                  gcode.process_subcommands_now_P(PSTR("G29 P1"));
                  gcode.process_subcommands_now_P(PSTR("G29 P3\nG29 P3\nG29 P3\nG29 P3\nG29 P3\nG29 P3\nG29 P3\nG29 P3\nG29 P3\nG29 P3\nG29 P3\nG29 P3\nG29 P3\nG29 P3\nG29 P3\nM420 S1"));
                  planner.synchronize();
                  Update_Status("Probed all reachable points");
                  Popup_Handler(SaveLevel);
                #else
                  level_state = planner.leveling_active;
                  set_bed_leveling_enabled(false);
                  mesh_conf.goto_mesh_value = true;
                  mesh_conf.mesh_x = mesh_conf.mesh_y = 0;
                  Popup_Handler(MoveWait);
                  mesh_conf.manual_move();;
                  Draw_Menu(UBLMesh);
                #endif
              #elif HAS_BED_PROBE
                Popup_Handler(Level);
                gcode.process_subcommands_now_P(PSTR("G29"));
                planner.synchronize();
                Popup_Handler(SaveLevel);
              #else
                level_state = planner.leveling_active;
                set_bed_leveling_enabled(false);
                gridpoint = 1;
                Popup_Handler(MoveWait);
                gcode.process_subcommands_now_P(PSTR("G29"));
                planner.synchronize();
                Draw_Menu(ManualMesh);
              #endif
            }
            break;
          case LEVELING_MANUAL:
            if (draw) {
              Draw_Menu_Item(row, ICON_Mesh, "Manual Tuning", NULL, true);
            }
            else {
              #if ENABLED(AUTO_BED_LEVELING_BILINEAR)
                if (!leveling_is_valid()) {
                  Confirm_Handler(InvalidMesh);
                  break;
                }
              #endif
              #if ENABLED(AUTO_BED_LEVELING_UBL)
                if (ubl.storage_slot < 0) {
                  Popup_Handler(MeshSlot);
                  break;
                }
              #endif
              if (axes_should_home()) {
                Popup_Handler(Home);
                gcode.home_all_axes(true);
              }
              level_state = planner.leveling_active;
              set_bed_leveling_enabled(false);
              mesh_conf.goto_mesh_value = false;
              #if ENABLED(PREHEAT_BEFORE_LEVELING)
                Popup_Handler(Heating);
                if (thermalManager.degTargetHotend(0) < LEVELING_NOZZLE_TEMP)
                  thermalManager.setTargetHotend(LEVELING_NOZZLE_TEMP, 0);
                if (thermalManager.degTargetBed() < LEVELING_BED_TEMP)
                  thermalManager.setTargetBed(LEVELING_BED_TEMP);
                thermalManager.wait_for_hotend(0);
                thermalManager.wait_for_bed_heating();
              #endif
              Popup_Handler(MoveWait);
              mesh_conf.manual_move();
              Draw_Menu(LevelManual);
            }
            break;
          case LEVELING_VIEW:
            if (draw) {
              Draw_Menu_Item(row, ICON_Mesh, "Mesh Viewer", NULL, true);
            }
            else {
              #if ENABLED(AUTO_BED_LEVELING_UBL)
                if (ubl.storage_slot < 0) {
                  Popup_Handler(MeshSlot);
                  break;
                }
              #endif
              Draw_Menu(LevelView);
            }
            break;
          case LEVELING_SETTINGS:
            if (draw) {
              Draw_Menu_Item(row, ICON_Step, "Leveling Settings", NULL, true);
            }
            else {
              Draw_Menu(LevelSettings);
            }
            break;
          #if ENABLED(AUTO_BED_LEVELING_UBL)
          case LEVELING_SLOT:
            if (draw) {
              Draw_Menu_Item(row, ICON_PrintSize, "Mesh Slot");
              Draw_Float(ubl.storage_slot, row, false, 1);
            }
            else {
              Modify_Value(ubl.storage_slot, 0, settings.calc_num_meshes()-1, 1);
            }
            break;
          case LEVELING_LOAD:
            if (draw) {
              Draw_Menu_Item(row, ICON_ReadEEPROM, "Load Mesh");
            }
            else {
              if (ubl.storage_slot < 0) {
                Popup_Handler(MeshSlot);
                break;
              }
              gcode.process_subcommands_now_P(PSTR("G29 L"));
              planner.synchronize();
              AudioFeedback(true);
            }
            break;
          case LEVELING_SAVE:
            if(draw) {
              Draw_Menu_Item(row, ICON_WriteEEPROM, "Save Mesh");
            }
            else {
              if (ubl.storage_slot < 0) {
                Popup_Handler(MeshSlot);
                break;
              }
              gcode.process_subcommands_now_P(PSTR("G29 S"));
              planner.synchronize();
              AudioFeedback(true);
            }
            break;
          #endif
        }
        break;
      case LevelView:

        #define LEVELING_VIEW_BACK 0
        #define LEVELING_VIEW_MESH (LEVELING_VIEW_BACK + 1)
        #define LEVELING_VIEW_TEXT (LEVELING_VIEW_MESH + 1)
        #define LEVELING_VIEW_ASYMMETRIC (LEVELING_VIEW_TEXT + 1)
        #define LEVELING_VIEW_TOTAL LEVELING_VIEW_ASYMMETRIC

        switch (item) {
          case LEVELING_VIEW_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Menu(Leveling, LEVELING_VIEW);
            }
            break;
          case LEVELING_VIEW_MESH:
            if (draw) {
              Draw_Menu_Item(row, ICON_PrintSize, "Mesh Viewer", NULL, true);
            }
            else {
              Draw_Menu(MeshViewer);
            }
            break;
          case LEVELING_VIEW_TEXT:
            if (draw) {
              Draw_Menu_Item(row, ICON_Contact, "Viewer Show Values");
              Draw_Checkbox(row, mesh_conf.viewer_print_value);
            }
            else {
              mesh_conf.viewer_print_value = !mesh_conf.viewer_print_value;
              Draw_Checkbox(row, mesh_conf.viewer_print_value);
            }
            break;
          case LEVELING_VIEW_ASYMMETRIC:
            if (draw) {
              Draw_Menu_Item(row, ICON_Axis, "Viewer Asymmetric");
              Draw_Checkbox(row, mesh_conf.viewer_asymmetric_range);
            }
            else {
              mesh_conf.viewer_asymmetric_range = !mesh_conf.viewer_asymmetric_range;
              Draw_Checkbox(row, mesh_conf.viewer_asymmetric_range);
            }
            break;
        }
        break;
      case LevelSettings:

        #define LEVELING_SETTINGS_BACK 0
        #define LEVELING_SETTINGS_FADE (LEVELING_SETTINGS_BACK + 1)
        #define LEVELING_SETTINGS_TILT (LEVELING_SETTINGS_FADE + ENABLED(AUTO_BED_LEVELING_UBL))
        #define LEVELING_SETTINGS_PLANE (LEVELING_SETTINGS_TILT + ENABLED(AUTO_BED_LEVELING_UBL))
        #define LEVELING_SETTINGS_ZERO (LEVELING_SETTINGS_PLANE + ENABLED(AUTO_BED_LEVELING_UBL))
        #define LEVELING_SETTINGS_UNDEF (LEVELING_SETTINGS_ZERO + ENABLED(AUTO_BED_LEVELING_UBL))
        #define LEVELING_SETTINGS_TOTAL LEVELING_SETTINGS_UNDEF

        switch (item) {
          case LEVELING_SETTINGS_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              Draw_Menu(Leveling, LEVELING_SETTINGS);
            }
            break;
          case LEVELING_SETTINGS_FADE:
              if (draw) {
                Draw_Menu_Item(row, ICON_Fade, "Fade Mesh within");
                Draw_Float(planner.z_fade_height, row, false, 1);
              }
              else {
                Modify_Value(planner.z_fade_height, 0, Z_MAX_POS, 1);
                planner.z_fade_height = -1;
                set_z_fade_height(planner.z_fade_height);
              }
              break;
          #if ENABLED(AUTO_BED_LEVELING_UBL)
            case LEVELING_SETTINGS_TILT:
                if (draw) {
                  Draw_Menu_Item(row, ICON_Tilt, "Tilting Grid Size");
                  Draw_Float(mesh_conf.tilt_grid, row, false, 1);
                }
                else {
                  Modify_Value(mesh_conf.tilt_grid, 1, 8, 1);
                }
                break;
            case LEVELING_SETTINGS_PLANE:
                if (draw) {
                  Draw_Menu_Item(row, ICON_ResumeEEPROM, "Convert Mesh to Plane");
                }
                else {
                  if (mesh_conf.create_plane_from_mesh()) {
                    break;
                  }
                  gcode.process_subcommands_now_P(PSTR("M420 S1"));
                  planner.synchronize();
                  AudioFeedback(true);
                }
                break;
            case LEVELING_SETTINGS_ZERO:
                if (draw) {
                  Draw_Menu_Item(row, ICON_Mesh, "Zero Current Mesh");
                }
                else {
                  ZERO(mesh_conf.mesh_z_values);
                }
                break;
              case LEVELING_SETTINGS_UNDEF:
                if (draw) {
                  Draw_Menu_Item(row, ICON_Mesh, "Clear Current Mesh");
                }
                else {
                  ubl.invalidate();
                }
                break;
            #endif
        }
        break;
      case MeshViewer:
        #define MESHVIEW_BACK 0
        #define MESHVIEW_TOTAL MESHVIEW_BACK
        
        switch (item) {
          case MESHVIEW_BACK:
            if (draw) {
              Draw_Menu_Item(0, ICON_Back, "Back");
              mesh_conf.Draw_Bed_Mesh();
              mesh_conf.Set_Mesh_Viewer_Status();
            }
            else {
              if (!mesh_conf.drawing_mesh) {
                Draw_Menu(LevelView, LEVELING_VIEW_MESH);
                Update_Status("");
              }
            }
            break;
        }
        break;
      case LevelManual:

        #define LEVELING_M_BACK 0
        #define LEVELING_M_X (LEVELING_M_BACK + 1)
        #define LEVELING_M_Y (LEVELING_M_X + 1)
        #define LEVELING_M_NEXT (LEVELING_M_Y + 1)
        #define LEVELING_M_OFFSET (LEVELING_M_NEXT + 1)
        #define LEVELING_M_UP (LEVELING_M_OFFSET + 1)
        #define LEVELING_M_DOWN (LEVELING_M_UP + 1)
        #define LEVELING_M_GOTO_VALUE (LEVELING_M_DOWN + 1)
        #define LEVELING_M_UNDEF (LEVELING_M_GOTO_VALUE + ENABLED(AUTO_BED_LEVELING_UBL))
        #define LEVELING_M_TOTAL LEVELING_M_UNDEF

        switch (item) {
          case LEVELING_M_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              set_bed_leveling_enabled(level_state);
              #if ENABLED(AUTO_BED_LEVELING_BILINEAR)
                refresh_bed_level();
              #endif
              Draw_Menu(Leveling, LEVELING_MANUAL);
            }
            break;
          case LEVELING_M_X:
            if (draw) {
              Draw_Menu_Item(row, ICON_MoveX, "Mesh Point X");
              Draw_Float(mesh_conf.mesh_x, row, 0, 1);
            }
            else {
              Modify_Value(mesh_conf.mesh_x, 0, GRID_MAX_POINTS_X - 1, 1);
            }
            break;
          case LEVELING_M_Y:
            if (draw) {
              Draw_Menu_Item(row, ICON_MoveY, "Mesh Point Y");
              Draw_Float(mesh_conf.mesh_y, row, 0, 1);
            }
            else {
              Modify_Value(mesh_conf.mesh_y, 0, GRID_MAX_POINTS_Y - 1, 1);
            }
            break;
          case LEVELING_M_NEXT:
            if (draw) {
              Draw_Menu_Item(row, ICON_More, "Next Point");
            }
            else {
              if (mesh_conf.mesh_x != (GRID_MAX_POINTS_X-1) || mesh_conf.mesh_y != (GRID_MAX_POINTS_Y-1)) {
                if ((mesh_conf.mesh_x == (GRID_MAX_POINTS_X-1) && mesh_conf.mesh_y % 2 == 0) || (mesh_conf.mesh_x == 0 && mesh_conf.mesh_y % 2 == 1)) {
                  mesh_conf.mesh_y++;
                }
                else if (mesh_conf.mesh_y % 2 == 0) {
                  mesh_conf.mesh_x++;
                }
                else {
                  mesh_conf.mesh_x--;
                }
                mesh_conf.manual_move();
              }
            }
            break;
          case LEVELING_M_OFFSET:
            if (draw) {
              Draw_Menu_Item(row, ICON_SetZOffset, "Point Z Offset");
              Draw_Float(mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y], row, false, 100);
            }
            else {
              if (isnan(mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y]))
                mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y] = 0;
              Modify_Value(mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y], MIN_Z_OFFSET, MAX_Z_OFFSET, 100);
            }
            break;
          case LEVELING_M_UP:
            if (draw) {
              Draw_Menu_Item(row, ICON_Axis, "Microstep Up");
            }
            else {
              if (mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y] < MAX_Z_OFFSET) {
                mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y] += 0.01;
                gcode.process_subcommands_now_P(PSTR("M290 Z0.01"));
                planner.synchronize();
                current_position.z += 0.01f;
                sync_plan_position();
                Draw_Float(mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y], row-1, false, 100);
              }
            }
            break;
          case LEVELING_M_DOWN:
            if (draw) {
              Draw_Menu_Item(row, ICON_AxisD, "Microstep Down");
            }
            else {
              if (mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y] > MIN_Z_OFFSET) {
                mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y] -= 0.01;
                gcode.process_subcommands_now_P(PSTR("M290 Z-0.01"));
                planner.synchronize();
                current_position.z -= 0.01f;
                sync_plan_position();
                Draw_Float(mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y], row-2, false, 100);
              }
            }
            break;
          case LEVELING_M_GOTO_VALUE: 
            if (draw) {
              Draw_Menu_Item(row, ICON_StockConfiguraton, "Go to Mesh Z Value");
              Draw_Checkbox(row, mesh_conf.goto_mesh_value);
            }
            else {
              mesh_conf.goto_mesh_value = !mesh_conf.goto_mesh_value;
              current_position.z = 0;
              mesh_conf.manual_move(true);
              Draw_Checkbox(row, mesh_conf.goto_mesh_value);
            }
            break;
          #if ENABLED(AUTO_BED_LEVELING_UBL)
          case LEVELING_M_UNDEF:
            if (draw) {
              Draw_Menu_Item(row, ICON_ResumeEEPROM, "Clear Point Value");
            }
            else {
              mesh_conf.manual_value_update(true);
              Redraw_Menu(false);
            }
            break;
          #endif
        }
        break;
    #endif
    #if ENABLED(AUTO_BED_LEVELING_UBL) && !HAS_BED_PROBE
      case UBLMesh:

        #define UBL_M_BACK 0
        #define UBL_M_NEXT (UBL_M_BACK + 1)
        #define UBL_M_PREV (UBL_M_NEXT + 1)
        #define UBL_M_OFFSET (UBL_M_PREV + 1)
        #define UBL_M_UP (UBL_M_OFFSET + 1)
        #define UBL_M_DOWN (UBL_M_UP + 1)
        #define UBL_M_TOTAL UBL_M_DOWN

        switch (item) {
          case UBL_M_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Back");
            }
            else {
              set_bed_leveling_enabled(level_state);
              Draw_Menu(Leveling, LEVELING_GET_MESH);
            }
            break;
          case UBL_M_NEXT:
            if (draw) {
              if (mesh_conf.mesh_x != (GRID_MAX_POINTS_X-1) || mesh_conf.mesh_y != (GRID_MAX_POINTS_Y-1))
                Draw_Menu_Item(row, ICON_More, "Next Point");
              else
                Draw_Menu_Item(row, ICON_More, "Save Mesh");
            }
            else {
              if (mesh_conf.mesh_x != (GRID_MAX_POINTS_X-1) || mesh_conf.mesh_y != (GRID_MAX_POINTS_Y-1)) {
                if ((mesh_conf.mesh_x == (GRID_MAX_POINTS_X-1) && mesh_conf.mesh_y % 2 == 0) || (mesh_conf.mesh_x == 0 && mesh_conf.mesh_y % 2 == 1)) {
                  mesh_conf.mesh_y++;
                }
                else if (mesh_conf.mesh_y % 2 == 0) {
                  mesh_conf.mesh_x++;
                }
                else {
                  mesh_conf.mesh_x--;
                }
                mesh_conf.manual_move();
              }
              else {
                gcode.process_subcommands_now_P(PSTR("G29 S"));
                planner.synchronize();
                AudioFeedback(true);
                Draw_Menu(Leveling, LEVELING_GET_MESH);
              }
            }
            break;
          case UBL_M_PREV:
            if (draw) {
              Draw_Menu_Item(row, ICON_More, "Previous Point");
            }
            else {
              if (mesh_conf.mesh_x != 0 || mesh_conf.mesh_y != 0) {
                if ((mesh_conf.mesh_x == (GRID_MAX_POINTS_X-1) && mesh_conf.mesh_y % 2 == 1) || (mesh_conf.mesh_x == 0 && mesh_conf.mesh_y % 2 == 0)) {
                  mesh_conf.mesh_y--;
                }
                else if (mesh_conf.mesh_y % 2 == 0) {
                  mesh_conf.mesh_x--;
                }
                else {
                  mesh_conf.mesh_x++;
                }
                mesh_conf.manual_move();
              }
            }
            break;
          case UBL_M_OFFSET:
            if (draw) {
              Draw_Menu_Item(row, ICON_SetZOffset, "Point Z Offset");
              Draw_Float(mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y], row, false, 100);
            }
            else {
              if (isnan(mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y]))
                mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y] = 0;
              Modify_Value(mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y], MIN_Z_OFFSET, MAX_Z_OFFSET, 100);
            }
            break;
          case UBL_M_UP:
            if (draw) {
              Draw_Menu_Item(row, ICON_Axis, "Microstep Up");
            }
            else {
              if (mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y] < MAX_Z_OFFSET) {
                mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y] += 0.01;
                gcode.process_subcommands_now_P(PSTR("M290 Z0.01"));
                planner.synchronize();
                current_position.z += 0.01f;
                sync_plan_position();
                Draw_Float(mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y], row-1, false, 100);
              }
            }
            break;
          case UBL_M_DOWN:
            if (draw) {
              Draw_Menu_Item(row, ICON_Axis, "Microstep Down");
            }
            else {
              if (mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y] > MIN_Z_OFFSET) {
                mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y] -= 0.01;
                gcode.process_subcommands_now_P(PSTR("M290 Z-0.01"));
                planner.synchronize();
                current_position.z -= 0.01f;
                sync_plan_position();
                Draw_Float(mesh_conf.mesh_z_values[mesh_conf.mesh_x][mesh_conf.mesh_y], row-2, false, 100);
              }
            }
            break;
        }
        break;
    #endif
    #if ENABLED(PROBE_MANUALLY)
      case ManualMesh:

        #define MMESH_BACK 0
        #define MMESH_NEXT (MMESH_BACK + 1)
        #define MMESH_OFFSET (MMESH_NEXT + 1)
        #define MMESH_UP (MMESH_OFFSET + 1)
        #define MMESH_DOWN (MMESH_UP + 1)
        #define MMESH_OLD (MMESH_DOWN + 1)
        #define MMESH_TOTAL MMESH_OLD

        switch (item) {
          case MMESH_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Cancel");
            }
            else {
              gcode.process_subcommands_now_P(PSTR("G29 A"));
              planner.synchronize();
              set_bed_leveling_enabled(level_state);
              Draw_Menu(Leveling, LEVELING_GET_MESH);
            }
            break;
          case MMESH_NEXT:
            if (draw) {
              if (gridpoint < GRID_MAX_POINTS)
                Draw_Menu_Item(row, ICON_More, "Next Point");
              else
                Draw_Menu_Item(row, ICON_More, "Save Mesh");
            }
            else {
              if (gridpoint < GRID_MAX_POINTS) {
                Popup_Handler(MoveWait);
                gcode.process_subcommands_now_P(PSTR("G29"));
                planner.synchronize();
                gridpoint++;
                Redraw_Menu();
              }
              else {
                gcode.process_subcommands_now_P(PSTR("G29"));
                planner.synchronize();
                AudioFeedback(settings.save());
                Draw_Menu(Leveling, LEVELING_GET_MESH);
              }
            }
            break;
          case MMESH_OFFSET:
            if (draw) {
              Draw_Menu_Item(row, ICON_SetZOffset, "Z Position");
              current_position.z = MANUAL_PROBE_START_Z;
              Draw_Float(current_position.z, row, false, 100);
            }
            else {
              Modify_Value(current_position.z, MIN_Z_OFFSET, MAX_Z_OFFSET, 100);
            }
            break;
          case MMESH_UP:
            if (draw) {
              Draw_Menu_Item(row, ICON_Axis, "Microstep Up");
            }
            else {
              if (current_position.z < MAX_Z_OFFSET) {
                gcode.process_subcommands_now_P(PSTR("M290 Z0.01"));
                planner.synchronize();
                current_position.z += 0.01f;
                sync_plan_position();
                Draw_Float(current_position.z, row-1, false, 100);
              }
            }
            break;
          case MMESH_DOWN:
            if (draw) {
              Draw_Menu_Item(row, ICON_AxisD, "Microstep Down");
            }
            else {
              if (current_position.z > MIN_Z_OFFSET) {
                gcode.process_subcommands_now_P(PSTR("M290 Z-0.01"));
                planner.synchronize();
                current_position.z -= 0.01f;
                sync_plan_position();
                Draw_Float(current_position.z, row-2, false, 100);
              }
            }
            break;
          case MMESH_OLD:
            uint8_t mesh_x, mesh_y;
            // 0,0 -> 1,0 -> 2,0 -> 2,1 -> 1,1 -> 0,1 -> 0,2 -> 1,2 -> 2,2
            mesh_y = (gridpoint - 1) / GRID_MAX_POINTS_Y;
            mesh_x = (gridpoint - 1) % GRID_MAX_POINTS_X;

            if (mesh_y % 2 == 1) {
              mesh_x = GRID_MAX_POINTS_X - mesh_x - 1;
            }

            const float currval = mesh_conf.mesh_z_values[mesh_x][mesh_y];

            if (draw) {
              Draw_Menu_Item(row, ICON_Zoffset, "Goto Mesh Value");
              Draw_Float(currval, row, false, 100);
            } else {
              if (!isnan(currval)) {
                current_position.z = currval;
                planner.synchronize();
                planner.buffer_line(current_position, homing_feedrate(Z_AXIS), active_extruder);
                planner.synchronize();
                Draw_Float(current_position.z, row-3, false, 100);
              }
            }
            break;
        }
        break;
    #endif
    case Tune:

      #define TUNE_BACK 0
      #define TUNE_SPEED (TUNE_BACK + 1)
      #define TUNE_FLOW (TUNE_SPEED + ENABLED(HAS_HOTEND))
      #define TUNE_HOTEND (TUNE_FLOW + ENABLED(HAS_HOTEND))
      #define TUNE_BED (TUNE_HOTEND + ENABLED(HAS_HEATED_BED))
      #define TUNE_FAN (TUNE_BED + ENABLED(HAS_FAN))
      #define TUNE_ZOFFSET (TUNE_FAN + ENABLED(HAS_ZOFFSET_ITEM))
      #define TUNE_ZUP (TUNE_ZOFFSET + ENABLED(HAS_ZOFFSET_ITEM))
      #define TUNE_ZDOWN (TUNE_ZUP + ENABLED(HAS_ZOFFSET_ITEM))
      #define TUNE_CHANGEFIL (TUNE_ZDOWN + ENABLED(FILAMENT_LOAD_UNLOAD_GCODES))
      #define TUNE_FILSENSORENABLED (TUNE_CHANGEFIL + ENABLED(FILAMENT_RUNOUT_SENSOR))
      #define TUNE_BACKLIGHT_OFF (TUNE_FILSENSORENABLED + 1)
      #define TUNE_BACKLIGHT (TUNE_BACKLIGHT_OFF + 1)
      #define TUNE_TOTAL TUNE_BACKLIGHT

      switch (item) {
        case TUNE_BACK:
          if (draw) {
            Draw_Menu_Item(row, ICON_Back, "Back");
          }
          else {
            Draw_Print_Screen();
          }
          break;
        case TUNE_SPEED:
          if (draw) {
            Draw_Menu_Item(row, ICON_Speed, "Print Speed");
            Draw_Float(feedrate_percentage, row, false, 1);
          }
          else {
            Modify_Value(feedrate_percentage, MIN_PRINT_SPEED, MAX_PRINT_SPEED, 1);
          }
          break;
        #if HAS_HOTEND
          case TUNE_FLOW:
            if (draw) {
              Draw_Menu_Item(row, ICON_Speed, "Flow Rate");
              Draw_Float(planner.flow_percentage[0], row, false, 1);
            }
            else {
              Modify_Value(planner.flow_percentage[0], MIN_FLOW_RATE, MAX_FLOW_RATE, 1);
            }
            break;
          case TUNE_HOTEND:
            if (draw) {
              Draw_Menu_Item(row, ICON_SetEndTemp, "Hotend");
              Draw_Float(thermalManager.temp_hotend[0].target, row, false, 1);
            }
            else {
              Modify_Value(thermalManager.temp_hotend[0].target, MIN_E_TEMP, MAX_E_TEMP, 1);
            }
            break;
        #endif
        #if HAS_HEATED_BED
          case TUNE_BED:
            if (draw) {
              Draw_Menu_Item(row, ICON_SetBedTemp, "Bed");
              Draw_Float(thermalManager.temp_bed.target, row, false, 1);
            }
            else {
              Modify_Value(thermalManager.temp_bed.target, MIN_BED_TEMP, MAX_BED_TEMP, 1);
            }
            break;
        #endif
        #if HAS_FAN
          case TUNE_FAN:
            if (draw) {
              Draw_Menu_Item(row, ICON_FanSpeed, "Fan");
              Draw_Float(thermalManager.fan_speed[0], row, false, 1);
            }
            else {
              Modify_Value(thermalManager.fan_speed[0], MIN_FAN_SPEED, MAX_FAN_SPEED, 1);
            }
            break;
        #endif
        #if HAS_ZOFFSET_ITEM
          case TUNE_ZOFFSET:
            if (draw) {
              Draw_Menu_Item(row, ICON_FanSpeed, "Z-Offset");
              Draw_Float(zoffsetvalue, row, false, 100);
            }
            else {
              Modify_Value(zoffsetvalue, MIN_Z_OFFSET, MAX_Z_OFFSET, 100);
            }
            break;
          case TUNE_ZUP:
            if (draw) {
              Draw_Menu_Item(row, ICON_Axis, "Z-Offset Up");
            }
            else {
              if (zoffsetvalue < MAX_Z_OFFSET) {
                gcode.process_subcommands_now_P(PSTR("M290 Z0.01"));
                zoffsetvalue += 0.01;
                Draw_Float(zoffsetvalue, row-1, false, 100);
              }
            }
            break;
          case TUNE_ZDOWN:
            if (draw) {
              Draw_Menu_Item(row, ICON_AxisD, "Z-Offset Down");
            }
            else {
              if (zoffsetvalue > MIN_Z_OFFSET) {
                gcode.process_subcommands_now_P(PSTR("M290 Z-0.01"));
                zoffsetvalue -= 0.01;
                Draw_Float(zoffsetvalue, row-2, false, 100);
              }
            }
            break;
        #endif
        #if ENABLED(FILAMENT_LOAD_UNLOAD_GCODES)
          case TUNE_CHANGEFIL:
            if (draw) {
              Draw_Menu_Item(row, ICON_ResumeEEPROM, "Change Filament");
            }
            else {
              Popup_Handler(ConfFilChange);
            }
            break;
        #endif
        #if ENABLED(FILAMENT_RUNOUT_SENSOR)
          case TUNE_FILSENSORENABLED:
            if (draw) {
              Draw_Menu_Item(row, ICON_Extruder, "Filament Sensor");
              Draw_Checkbox(row, runout.enabled);
            }
            else {
              runout.enabled = !runout.enabled;
              Draw_Checkbox(row, runout.enabled);
            }
            break;
        #endif
        case TUNE_BACKLIGHT_OFF:
          if (draw) {
            Draw_Menu_Item(row, ICON_Brightness, "Display Off");
          }
          else {
            ui.set_brightness(0);
          }
          break;
        case TUNE_BACKLIGHT:
          if (draw) {
            Draw_Menu_Item(row, ICON_Brightness, "LCD Brightness");
            Draw_Float(ui.brightness, row, false, 1);
          }
          else {
            Modify_Value(ui.brightness, MIN_LCD_BRIGHTNESS, MAX_LCD_BRIGHTNESS, 1, ui.refresh_brightness);
          }
          break;
      }
      break;
    case PreheatHotend:

        #define PREHEATHOTEND_BACK 0
        #define PREHEATHOTEND_CONTINUE (PREHEATHOTEND_BACK + 1)
        #define PREHEATHOTEND_1 (PREHEATHOTEND_CONTINUE + (PREHEAT_COUNT >= 1))
        #define PREHEATHOTEND_2 (PREHEATHOTEND_1 + (PREHEAT_COUNT >= 2))
        #define PREHEATHOTEND_3 (PREHEATHOTEND_2 + (PREHEAT_COUNT >= 3))
        #define PREHEATHOTEND_4 (PREHEATHOTEND_3 + (PREHEAT_COUNT >= 4))
        #define PREHEATHOTEND_5 (PREHEATHOTEND_4 + (PREHEAT_COUNT >= 5))
        #define PREHEATHOTEND_CUSTOM (PREHEATHOTEND_5 + 1)
        #define PREHEATHOTEND_TOTAL PREHEATHOTEND_CUSTOM

        switch (item) {
          case PREHEATHOTEND_BACK:
            if (draw) {
              Draw_Menu_Item(row, ICON_Back, "Cancel");
            }
            else {
              thermalManager.setTargetHotend(0, 0);
              thermalManager.set_fan_speed(0, 0);
              Redraw_Menu(false, true, true);
            }
            break;
          case PREHEATHOTEND_CONTINUE:
            if (draw) {
              Draw_Menu_Item(row, ICON_SetEndTemp, (char*)"Continue");
            }
            else {
              Popup_Handler(Heating);
              thermalManager.wait_for_hotend(0);
              switch (last_menu) {
                case Prepare:
                  Popup_Handler(FilChange);
                  sprintf_P(cmd, PSTR("M600 B1 R%i"), thermalManager.temp_hotend[0].target);
                  gcode.process_subcommands_now_P(cmd);
                  break;
                #if ENABLED(FILAMENT_LOAD_UNLOAD_GCODES)
                  case ChangeFilament:
                    switch (last_selection) {
                      case CHANGEFIL_LOAD:
                        Popup_Handler(FilLoad);
                        gcode.process_subcommands_now_P("M701");
                        planner.synchronize();
                        Redraw_Menu(true, true, true);
                        break;
                      case CHANGEFIL_UNLOAD:
                        Popup_Handler(FilLoad, true);
                        gcode.process_subcommands_now_P("M702");
                        planner.synchronize();
                        Redraw_Menu(true, true, true);
                        break;
                      case CHANGEFIL_CHANGE:
                        Popup_Handler(FilChange);
                        sprintf_P(cmd, PSTR("M600 B1 R%i"), thermalManager.temp_hotend[0].target);
                        gcode.process_subcommands_now_P(cmd);
                        break;
                    }
                    break;
                #endif
                default:
                  Redraw_Menu(true, true, true);
                  break;
              }
            }
            break;
          #if (PREHEAT_COUNT >= 1)
            case PREHEATHOTEND_1:
              if (draw) {
                Draw_Menu_Item(row, ICON_Temperature, PREHEAT_1_LABEL);
              }
              else {
                thermalManager.setTargetHotend(ui.material_preset[0].hotend_temp, 0);
                thermalManager.set_fan_speed(0, ui.material_preset[0].fan_speed);
              }
              break;
          #endif
          #if (PREHEAT_COUNT >= 2)
            case PREHEATHOTEND_2:
              if (draw) {
                Draw_Menu_Item(row, ICON_Temperature, PREHEAT_2_LABEL);
              }
              else {
                thermalManager.setTargetHotend(ui.material_preset[1].hotend_temp, 0);
                thermalManager.set_fan_speed(0, ui.material_preset[1].fan_speed);
              }
              break;
          #endif
          #if (PREHEAT_COUNT >= 3)
            case PREHEATHOTEND_3:
              if (draw) {
                Draw_Menu_Item(row, ICON_Temperature, PREHEAT_3_LABEL);
              }
              else {
                thermalManager.setTargetHotend(ui.material_preset[2].hotend_temp, 0);
                thermalManager.set_fan_speed(0, ui.material_preset[2].fan_speed);
              }
              break;
          #endif
          #if (PREHEAT_COUNT >= 4)
            case PREHEATHOTEND_4:
              if (draw) {
                Draw_Menu_Item(row, ICON_Temperature, PREHEAT_4_LABEL);
              }
              else {
                thermalManager.setTargetHotend(ui.material_preset[3].hotend_temp, 0);
                thermalManager.set_fan_speed(0, ui.material_preset[3].fan_speed);
              }
              break;
          #endif
          #if (PREHEAT_COUNT >= 5)
            case PREHEATHOTEND_5:
              if (draw) {
                Draw_Menu_Item(row, ICON_Temperature, PREHEAT_5_LABEL);
              }
              else {
                thermalManager.setTargetHotend(ui.material_preset[4].hotend_temp, 0);
                thermalManager.set_fan_speed(0, ui.material_preset[4].fan_speed);
              }
              break;
          #endif
          case PREHEATHOTEND_CUSTOM:
            if (draw) {
              Draw_Menu_Item(row, ICON_Temperature, "Custom");
              Draw_Float(thermalManager.temp_hotend[0].target, row, false, 1);
            }
            else {
              Modify_Value(thermalManager.temp_hotend[0].target, EXTRUDE_MINTEMP, MAX_E_TEMP, 1);
            }
            break;
        }
        break;   
  }
}

const char * CrealityDWINClass::Get_Menu_Title(uint8_t menu) {
  switch(menu) {
    case MainMenu:
      return "Main Menu";
    case Prepare:
      return "Prepare";
    case HomeMenu:
      return "Homing Menu";
    case Move:
      return "Move";
    case ManualLevel:
      return "Manual Leveling";
    #if HAS_ZOFFSET_ITEM
      case ZOffset:
        return "Z Offset";
    #endif
    #if HAS_PREHEAT
      case Preheat:
        return "Preheat";
    #endif
    #if ENABLED(FILAMENT_LOAD_UNLOAD_GCODES)
      case ChangeFilament:
        return "Change Filament";
    #endif
    case Control:
      return "Control";
    case TempMenu:
      return "Temperature";
    #if ANY(HAS_HOTEND, HAS_HEATED_BED)
      case PID:
        return "PID Menu";
    #endif
    #if HAS_HOTEND
      case HotendPID:
        return "Hotend PID Settings";
    #endif
    #if HAS_HEATED_BED
      case BedPID:
        return "Bed PID Settings";
    #endif
    #if (PREHEAT_COUNT >= 1)
      case Preheat1:
        return (PREHEAT_1_LABEL " Settings");
    #endif
    #if (PREHEAT_COUNT >= 2)
      case Preheat2:
        return (PREHEAT_2_LABEL " Settings");
    #endif
    #if (PREHEAT_COUNT >= 3) 
      case Preheat3:
        return (PREHEAT_3_LABEL " Settings");
    #endif
    #if (PREHEAT_COUNT >= 4)
      case Preheat4:
        return (PREHEAT_4_LABEL " Settings");
    #endif
    #if (PREHEAT_COUNT >= 5)
      case Preheat5:
        return (PREHEAT_5_LABEL " Settings");
    #endif
    case Motion:
      return "Motion Settings";
    case HomeOffsets:
      return "Home Offsets";
    case MaxSpeed:
      return "Max Speed";
    case MaxAcceleration:
      return "Max Acceleration";
    #if HAS_CLASSIC_JERK
      case MaxJerk:
        return "Max Jerk";
    #endif
    case Steps:
      return "Steps/mm";
    case Visual:
      return "Visual Settings";
    case Advanced:
      return "Advanced Settings";
    #if HAS_BED_PROBE
      case ProbeMenu:
        return "Probe Menu";
    #endif
    case ColorSettings:
      return "UI Color Settings";
    case Info:
      return "Info";
    case InfoMain:
      return "Info";
    #if HAS_MESH
      case Leveling:
        return "Leveling";
      case LevelView:
        return "Mesh View";
      case LevelSettings:
        return "Leveling Settings";
      case MeshViewer:
        return "Mesh Viewer";
      case LevelManual:
        return "Manual Tuning";
    #endif
    #if ENABLED(AUTO_BED_LEVELING_UBL) && !HAS_BED_PROBE
      case UBLMesh:
        return "UBL Bed Leveling";
    #endif
    #if ENABLED(PROBE_MANUALLY)
      case ManualMesh:
        return "Mesh Bed Leveling";
    #endif
    case Tune:
      return "Tune";
    case PreheatHotend:
      return "Preheat Hotend";
  }
  return "";
}

uint8_t CrealityDWINClass::Get_Menu_Size(uint8_t menu) {
  switch(menu) {
    case Prepare:
      return PREPARE_TOTAL;
    case HomeMenu:
      return HOME_TOTAL;
    case Move:
      return MOVE_TOTAL;
    case ManualLevel:
      return MLEVEL_TOTAL;
    #if HAS_ZOFFSET_ITEM
      case ZOffset:
        return ZOFFSET_TOTAL;
    #endif
    #if HAS_PREHEAT
      case Preheat:
        return PREHEAT_TOTAL;
    #endif
    #if ENABLED(FILAMENT_LOAD_UNLOAD_GCODES)
      case ChangeFilament:
        return CHANGEFIL_TOTAL;
    #endif
    case Control:
      return CONTROL_TOTAL;
    case TempMenu:
      return TEMP_TOTAL;
    #if ANY(HAS_HOTEND, HAS_HEATED_BED)
      case PID:
        return PID_TOTAL;
    #endif
    #if HAS_HOTEND
      case HotendPID:
        return HOTENDPID_TOTAL;
    #endif
    #if HAS_HEATED_BED
      case BedPID:
        return BEDPID_TOTAL;
    #endif
    #if (PREHEAT_COUNT >= 1)
      case Preheat1:
        return PREHEAT1_TOTAL;
    #endif
    #if (PREHEAT_COUNT >= 2)
      case Preheat2:
        return PREHEAT2_TOTAL;
    #endif
    #if (PREHEAT_COUNT >= 3)
      case Preheat3:
        return PREHEAT3_TOTAL;
    #endif
    #if (PREHEAT_COUNT >= 4)
      case Preheat4:
        return PREHEAT4_TOTAL;
    #endif
    #if (PREHEAT_COUNT >= 5)
      case Preheat5:
        return PREHEAT5_TOTAL;
    #endif
    case Motion:
      return MOTION_TOTAL;
    case HomeOffsets:
      return HOMEOFFSETS_TOTAL;
    case MaxSpeed:
      return SPEED_TOTAL;
    case MaxAcceleration:
      return ACCEL_TOTAL;
    #if HAS_CLASSIC_JERK
      case MaxJerk:
        return JERK_TOTAL;
    #endif
    case Steps:
      return STEPS_TOTAL;
    case Visual:
      return VISUAL_TOTAL;
    case Advanced:
      return ADVANCED_TOTAL;
    #if HAS_BED_PROBE
      case ProbeMenu:
        return PROBE_TOTAL;
    #endif
    case Info:
      return INFO_TOTAL;
    case InfoMain:
      return INFO_TOTAL;
    #if ENABLED(AUTO_BED_LEVELING_UBL) && !HAS_BED_PROBE
      case UBLMesh:
        return UBL_M_TOTAL;
    #endif
    #if ENABLED(PROBE_MANUALLY)
      case ManualMesh:
        return MMESH_TOTAL;
    #endif
    #if HAS_MESH
      case Leveling:
        return LEVELING_TOTAL;
      case LevelView:
        return LEVELING_VIEW_TOTAL;
      case LevelSettings:
        return LEVELING_SETTINGS_TOTAL;
      case MeshViewer:
        return MESHVIEW_TOTAL;
      case LevelManual:
        return LEVELING_M_TOTAL;
    #endif
    case Tune:
      return TUNE_TOTAL;
    case PreheatHotend:
      return PREHEATHOTEND_TOTAL;
    case ColorSettings:
      return COLORSETTINGS_TOTAL;  
  }
  return 0;
}

/* Popup Config */

void CrealityDWINClass::Popup_Handler(PopupID popupid, bool option/*=false*/) {
  popup = last_popup = popupid;
  switch (popupid) {
    case Pause:
      Draw_Popup("Pause Print", "", "", Popup);
      break;
    case Stop:
      Draw_Popup("Stop Print", "", "", Popup);
      break;
    case Resume:
      Draw_Popup("Resume Print?", "Looks Like the last", "print was interupted.", Popup);
      break;
    case ConfFilChange:
      Draw_Popup("Confirm Filament Change", "", "", Popup);
      break;
    case PurgeMore:
      Draw_Popup("Purge more filament?", "(Cancel to finish process)", "", Popup);
      break;
    case SaveLevel:
      Draw_Popup("Leveling Complete", "Save to EEPROM?", "", Popup);
      break;
    case MeshSlot:
      Draw_Popup("Mesh slot not selected", "(Confirm to select slot 0)", "", Popup);
      break;
    case ETemp:
      Draw_Popup("Nozzle is too cold", "Open Preheat Menu?", "", Popup);
      break;
    case ManualProbing:
      Draw_Popup("Manual Probing", "(Confirm to probe)", "(cancel to exit)", Popup);
      break;
    case Level:
      Draw_Popup("Auto Bed Leveling", "Please wait until done.", "", Wait, ICON_AutoLeveling);
      break;
    case Home:
      Draw_Popup(option ? "Parking" : "Homing", "Please wait until done.", "", Wait, ICON_BLTouch);
      break;
    case MoveWait:
      Draw_Popup("Moving to Point", "Please wait until done.", "", Wait, ICON_BLTouch);
      break;
    case Heating:
      Draw_Popup("Heating", "Please wait until done.", "", Wait, ICON_BLTouch);
      break;
    case FilLoad:
      Draw_Popup(option ? "Unloading Filament" : "Loading Filament", "Please wait until done.", "", Wait, ICON_BLTouch);
      break;
    case FilChange:
      Draw_Popup("Filament Change", "Please wait for prompt.", "", Wait, ICON_BLTouch);
      break;
    case TempWarn:
      Draw_Popup(option ? "Nozzle temp too low!" : "Nozzle temp too high!", "", "", Wait, option ? ICON_TempTooLow : ICON_TempTooHigh);
      break;
    case Runout:
      Draw_Popup("Filament Runout", "", "", Wait, ICON_BLTouch);
      break;
    case PIDWait:
      Draw_Popup("PID Autotune", "in process", "Please wait until done.", Wait, ICON_BLTouch);
      break;
    case Resuming:
      Draw_Popup("Resuming Print", "Please wait until done.", "", Wait, ICON_BLTouch);
      break;
    default:
      break;
  }
}

void CrealityDWINClass::Confirm_Handler(PopupID popupid) {
  popup = popupid;
  switch (popupid) {
    case FilInsert:
      Draw_Popup("Insert Filament", "Press to Continue", "", Confirm);
      break;
    case HeaterTime:
      Draw_Popup("Heater Timed Out", "Press to Reheat", "", Confirm);
      break;
    case UserInput:
      Draw_Popup("Waiting for Input", "Press to Continue", "", Confirm);
      break;
    case LevelError:
      Draw_Popup("Couldn't enable Leveling", "(Valid mesh must exist)", "", Confirm);
      break;
    case InvalidMesh:
      Draw_Popup("Valid mesh must exist", "before tuning can be", "performed", Confirm);
      break;
    default:
      break;
  }
}

/* Navigation and Control */

void CrealityDWINClass::Main_Menu_Control() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_CW && selection < 3) {
    selection++; // Select Down
    Main_Menu_Icons();
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW && selection > 0) {
    selection--; // Select Up
    Main_Menu_Icons();
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
    switch(selection) {
      case 0:
        card.mount();
        Draw_SD_List();
        break;
      case 1:
        Draw_Menu(Prepare);
        break;
      case 2:
        Draw_Menu(Control);
        break;
      case 3:
        #if HAS_MESH
          Draw_Menu(Leveling);
        #else
          Draw_Menu(InfoMain);
        #endif
        break;
    }
  DWIN_UpdateLCD();
}

void CrealityDWINClass::Menu_Control() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_CW && selection < Get_Menu_Size(active_menu)) {
    DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, MBASE(selection-scrollpos) - 18, 14, MBASE(selection-scrollpos) + 33);
    selection++; // Select Down
    if (selection > scrollpos+MROWS) {
      scrollpos++;
      DWIN_Frame_AreaMove(1, 2, MLINE, Color_Bg_Black, 0, 31, DWIN_WIDTH, 349);
      Menu_Item_Handler(active_menu, selection);
    }
    DWIN_Draw_Rectangle(1, GetColor(eeprom_settings.cursor_color, Rectangle_Color), 0, MBASE(selection-scrollpos) - 18, 14, MBASE(selection-scrollpos) + 33);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW && selection > 0) {
    DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, MBASE(selection-scrollpos) - 18, 14, MBASE(selection-scrollpos) + 33);
    selection--; // Select Up
    if (selection < scrollpos) {
      scrollpos--;
      DWIN_Frame_AreaMove(1, 3, MLINE, Color_Bg_Black, 0, 31, DWIN_WIDTH, 349);
      Menu_Item_Handler(active_menu, selection);
    }
    DWIN_Draw_Rectangle(1, GetColor(eeprom_settings.cursor_color, Rectangle_Color), 0, MBASE(selection-scrollpos) - 18, 14, MBASE(selection-scrollpos) + 33);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
    Menu_Item_Handler(active_menu, selection, false);
  DWIN_UpdateLCD();
}

void CrealityDWINClass::Value_Control() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_CW) {
    tempvalue += EncoderRate.encoderMoveValue;
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    tempvalue -= EncoderRate.encoderMoveValue;
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    process = Menu;
    EncoderRate.enabled = false;
    Draw_Float(tempvalue/valueunit, selection-scrollpos, false, valueunit);
    DWIN_UpdateLCD();
    if (active_menu == ZOffset && liveadjust) {
      planner.synchronize();
      current_position.z += (tempvalue/valueunit - zoffsetvalue);
      planner.buffer_line(current_position, homing_feedrate(Z_AXIS), active_extruder);
      current_position.z = 0;
      sync_plan_position();
    }
    else if (active_menu == Tune && selection == TUNE_ZOFFSET) {
      sprintf_P(cmd, PSTR("M290 Z%s"), dtostrf((tempvalue/valueunit - zoffsetvalue), 1, 3, str_1));
      gcode.process_subcommands_now_P(cmd);
    }
    if (valuepointer == &thermalManager.temp_hotend[0].pid.Ki || valuepointer == &thermalManager.temp_bed.pid.Ki) 
      tempvalue = scalePID_i(tempvalue);
    if (valuepointer == &thermalManager.temp_hotend[0].pid.Kd || valuepointer == &thermalManager.temp_bed.pid.Kd) 
      tempvalue = scalePID_d(tempvalue);
    switch (valuetype) {
      case 0: *(float*)valuepointer = tempvalue/valueunit; break;
      case 1: *(uint8_t*)valuepointer = tempvalue/valueunit; break;
      case 2: *(uint16_t*)valuepointer = tempvalue/valueunit; break;
      case 3: *(int16_t*)valuepointer = tempvalue/valueunit; break;
      case 4: *(uint32_t*)valuepointer = tempvalue/valueunit; break;
      case 5: *(int8_t*)valuepointer = tempvalue/valueunit; break;
    }
    switch (active_menu) {
      case Move:
        planner.synchronize();
        planner.buffer_line(current_position, manual_feedrate_mm_s[selection-1], active_extruder);
        break;
      #if HAS_MESH
        case ManualMesh:
          planner.synchronize();
          planner.buffer_line(current_position, homing_feedrate(Z_AXIS), active_extruder);
          planner.synchronize();
          break;
        case UBLMesh:
          mesh_conf.manual_move(true);
          break;
        case LevelManual:
          mesh_conf.manual_move(selection == LEVELING_M_OFFSET);
          break;
      #endif
    }
    if (valuepointer == &planner.flow_percentage[0]) {
      planner.refresh_e_factor(0);
    }
    if (funcpointer) funcpointer();
    return;
  }
  NOLESS(tempvalue, (valuemin * valueunit));
  NOMORE(tempvalue, (valuemax * valueunit));
  Draw_Float(tempvalue/valueunit, selection-scrollpos, true, valueunit);
  DWIN_UpdateLCD();
  if (active_menu == Move && livemove) {
    *(float*)valuepointer = tempvalue/valueunit;
    planner.buffer_line(current_position, manual_feedrate_mm_s[selection-1], active_extruder);
  }
}

void CrealityDWINClass::Option_Control() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_CW) {
    tempvalue += EncoderRate.encoderMoveValue;
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    tempvalue -= EncoderRate.encoderMoveValue;
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    process = Menu;
    EncoderRate.enabled = false;
    if (valuepointer == &color_names) {
      switch(selection) {
        case COLORSETTINGS_CURSOR: eeprom_settings.cursor_color = tempvalue; break;
        case COLORSETTINGS_SPLIT_LINE: eeprom_settings.menu_split_line = tempvalue; break;
        case COLORSETTINGS_MENU_TOP_BG: eeprom_settings.menu_top_bg = tempvalue; break;
        case COLORSETTINGS_MENU_TOP_TXT: eeprom_settings.menu_top_txt = tempvalue; break;
        case COLORSETTINGS_HIGHLIGHT_BORDER: eeprom_settings.highlight_box = tempvalue; break;
        case COLORSETTINGS_PROGRESS_PERCENT: eeprom_settings.progress_percent = tempvalue; break;
        case COLORSETTINGS_PROGRESS_TIME: eeprom_settings.progress_time = tempvalue; break;      
        case COLORSETTINGS_PROGRESS_STATUS_BAR: eeprom_settings.status_bar_text = tempvalue; break;
        case COLORSETTINGS_PROGRESS_STATUS_AREA: eeprom_settings.status_area_text = tempvalue; break;
        case COLORSETTINGS_PROGRESS_COORDINATES: eeprom_settings.coordinates_text = tempvalue; break;
        case COLORSETTINGS_PROGRESS_COORDINATES_LINE: eeprom_settings.coordinates_split_line = tempvalue; break;
      }
      Redraw_Screen();
    }
    else if (valuepointer == &preheat_modes) {
      preheatmode = tempvalue;
    }
    Draw_Option(tempvalue, static_cast<const char * const *>(valuepointer), selection-scrollpos, false, (valuepointer == &color_names));
    DWIN_UpdateLCD();
    return;
  }
  NOLESS(tempvalue, valuemin);
  NOMORE(tempvalue, valuemax);
  Draw_Option(tempvalue, static_cast<const char * const *>(valuepointer), selection-scrollpos, true);
  DWIN_UpdateLCD();
}

void CrealityDWINClass::File_Control() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  static uint8_t filescrl = 0;
  if (encoder_diffState == ENCODER_DIFF_NO) {
    if (selection > 0) {
      card.getfilename_sorted(SD_ORDER(selection-1, card.get_num_Files()));
      char * const filename = card.longest_filename();
      size_t len = strlen(filename);
      int8_t pos = len;
      if (!card.flag.filenameIsDir)
        while (pos && filename[pos] != '.') pos--;
      if (pos > MENU_CHAR_LIMIT) {
        static millis_t time = 0;
        if (PENDING(millis(), time)) return;
        time = millis() + 200;
        pos -= filescrl;
        len = pos;
        if (len > MENU_CHAR_LIMIT)
          len = MENU_CHAR_LIMIT;
        char name[len+1];
        if (pos >= 0) {
          LOOP_L_N(i, len) name[i] = filename[i+filescrl];
        }
        else {
          LOOP_L_N(i, MENU_CHAR_LIMIT+pos) name[i] = ' ';
          LOOP_S_L_N(i, MENU_CHAR_LIMIT+pos, MENU_CHAR_LIMIT) name[i] = filename[i-(MENU_CHAR_LIMIT+pos)];
        }
        name[len] = '\0';
        DWIN_Draw_Rectangle(1, Color_Bg_Black, LBLX, MBASE(selection-scrollpos) - 14, 271, MBASE(selection-scrollpos) + 28);
        Draw_Menu_Item(selection-scrollpos, card.flag.filenameIsDir ? ICON_More : ICON_File, name);
        if (-pos >= MENU_CHAR_LIMIT)
          filescrl = 0;
        filescrl++;
        DWIN_UpdateLCD();
      }
    }
    return;
  }
  if (encoder_diffState == ENCODER_DIFF_CW && selection < card.get_num_Files()) {
    DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, MBASE(selection-scrollpos) - 18, 14, MBASE(selection-scrollpos) + 33);
    if (selection > 0) {
      DWIN_Draw_Rectangle(1, Color_Bg_Black, LBLX, MBASE(selection-scrollpos) - 14, 271, MBASE(selection-scrollpos) + 28);
      Draw_SD_Item(selection, selection-scrollpos);
    }
    filescrl = 0;
    selection++; // Select Down
    if (selection > scrollpos+MROWS) {
      scrollpos++;
      DWIN_Frame_AreaMove(1, 2, MLINE, Color_Bg_Black, 0, 31, DWIN_WIDTH, 349);
      Draw_SD_Item(selection, selection-scrollpos);
    }
    DWIN_Draw_Rectangle(1, GetColor(eeprom_settings.cursor_color, Rectangle_Color), 0, MBASE(selection-scrollpos) - 18, 14, MBASE(selection-scrollpos) + 33);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW && selection > 0) {
    DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, MBASE(selection-scrollpos) - 18, 14, MBASE(selection-scrollpos) + 33);
    DWIN_Draw_Rectangle(1, Color_Bg_Black, LBLX, MBASE(selection-scrollpos) - 14, 271, MBASE(selection-scrollpos) + 28);
    Draw_SD_Item(selection, selection-scrollpos);
    filescrl = 0;
    selection--; // Select Up
    if (selection < scrollpos) {
      scrollpos--;
      DWIN_Frame_AreaMove(1, 3, MLINE, Color_Bg_Black, 0, 31, DWIN_WIDTH, 349);
      Draw_SD_Item(selection, selection-scrollpos);
    }
    DWIN_Draw_Rectangle(1, GetColor(eeprom_settings.cursor_color, Rectangle_Color), 0, MBASE(selection-scrollpos) - 18, 14, MBASE(selection-scrollpos) + 33);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    if (selection == 0) {
      if(card.flag.workDirIsRoot) {
        process = Main;
        Draw_Main_Menu();
      }
      else {
        card.cdup();
        Draw_SD_List();
      }
    }
    else {
      card.getfilename_sorted(SD_ORDER(selection-1, card.get_num_Files()));
      if (card.flag.filenameIsDir) {
        card.cd(card.filename);
        Draw_SD_List();
      }
      else {
        card.openAndPrintFile(card.filename);
      }
    }
  }
  DWIN_UpdateLCD();
}

void CrealityDWINClass::Print_Screen_Control() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_CW && selection < 2) {
    selection++; // Select Down
    Print_Screen_Icons();
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW && selection > 0) {
    selection--; // Select Up
    Print_Screen_Icons();
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
    switch(selection) {
      case 0:
        Draw_Menu(Tune);
        Update_Status_Bar(true);
        break;
      case 1:
        if (paused) {
          if (sdprint) {
            wait_for_user = false;
            #if ENABLED(PARK_HEAD_ON_PAUSE)
              card.startOrResumeFilePrinting();
              TERN_(POWER_LOSS_RECOVERY, recovery.prepare());
            #else
              char cmnd[20];
              cmnd[sprintf_P(cmnd, PSTR("M140 S%i"), pausebed)] = '\0';
              gcode.process_subcommands_now_P(PSTR(cmnd));
              cmnd[sprintf_P(cmnd, PSTR("M109 S%i"), pausetemp)] = '\0';
              gcode.process_subcommands_now_P(PSTR(cmnd));
              thermalManager.fan_speed[0] = pausefan;
              planner.synchronize();
              queue.inject_P(PSTR("M24"));
            #endif
          }
          else {
            #if ENABLED(HOST_ACTION_COMMANDS)
              host_action_resume();
            #endif
          }
          Draw_Print_Screen();
        }
        else
          Popup_Handler(Pause);
        break;
      case 2:
        Popup_Handler(Stop);
        break;
    }
  DWIN_UpdateLCD();
}

void CrealityDWINClass::Popup_Control() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_CW && selection < 1) {
    selection++;
    Popup_Select();
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW && selection > 0) {
    selection--;
    Popup_Select();
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
    switch(popup) {
      case Pause:
        if (selection==0) {
          if (sdprint) {
            #if ENABLED(POWER_LOSS_RECOVERY)
              if (recovery.enabled) recovery.save(true);
            #endif
            #if ENABLED(PARK_HEAD_ON_PAUSE)
              Popup_Handler(Home, true);
              #if ENABLED(SDSUPPORT)
                if (IS_SD_PRINTING()) card.pauseSDPrint();
              #endif
              planner.synchronize();
              queue.inject_P(PSTR("M125"));
              planner.synchronize();
            #else
              queue.inject_P(PSTR("M25"));
              pausetemp = thermalManager.temp_hotend[0].target;
              pausebed = thermalManager.temp_bed.target;
              pausefan = thermalManager.fan_speed[0];
              thermalManager.disable_all_heaters();
              thermalManager.zero_fan_speeds();
            #endif
          }
          else {
            #if ENABLED(HOST_ACTION_COMMANDS)
              host_action_pause();
            #endif
          }
        }
        Draw_Print_Screen();
        break;
      case Stop:
        if (selection==0) {
          if (sdprint) {
            ui.abort_print();
            thermalManager.zero_fan_speeds();
            thermalManager.disable_all_heaters();
          }
          else {
            #if ENABLED(HOST_ACTION_COMMANDS)
              host_action_cancel();
            #endif
          }
        }
        else {
          Draw_Print_Screen();
        }
        break;
      case Resume:
        if (selection==0) {
          queue.inject_P(PSTR("M1000"));
        }
        else {
          queue.inject_P(PSTR("M1000 C"));
          Draw_Main_Menu();
        }
        break;
      case ETemp:
        if (selection==0) {
          thermalManager.setTargetHotend(EXTRUDE_MINTEMP, 0);
          thermalManager.set_fan_speed(0, MAX_FAN_SPEED);
          Draw_Menu(PreheatHotend);
        }
        else {
          Redraw_Menu(true, true, false);
        }
        break;
      #if HAS_BED_PROBE
        case ManualProbing:
          if (selection==0) {
            char buf[80];
            const float dif = probe.probe_at_point(current_position.x, current_position.y, PROBE_PT_STOW, 0, false) - corner_avg;
            if (dif > 0)
              sprintf(buf, "Corner is %smm high", dtostrf(abs(dif), 1, 3, str_1));
            else
              sprintf(buf, "Corner is %smm low", dtostrf(abs(dif), 1, 3, str_1));
            Update_Status(buf);
          }
          else {
            Redraw_Menu(true, true, false);
            Update_Status("");
          }
          break;
      #endif
      #if ENABLED(ADVANCED_PAUSE_FEATURE)
        case ConfFilChange:
          if (selection==0) {
            if (thermalManager.temp_hotend[0].target < thermalManager.extrude_min_temp) {
              Popup_Handler(ETemp);
            }
            else {
              if (thermalManager.temp_hotend[0].celsius < thermalManager.temp_hotend[0].target-2) {
                Popup_Handler(Heating);
                thermalManager.wait_for_hotend(0);
              }
              Popup_Handler(FilChange);
              sprintf_P(cmd, PSTR("M600 B1 R%i"), thermalManager.temp_hotend[0].target);
              gcode.process_subcommands_now_P(cmd);
            }
          } else {
            Redraw_Menu(true, true, false);
          }
          break;
        case PurgeMore:
          if (selection==0) {
            pause_menu_response = PAUSE_RESPONSE_EXTRUDE_MORE;
            Popup_Handler(FilChange);
          } else {
            pause_menu_response = PAUSE_RESPONSE_RESUME_PRINT;
            if (printing) Popup_Handler(Resuming);
            else Redraw_Menu(true, true, (active_menu==PreheatHotend));
          }
          break;
      #endif
      #if HAS_MESH
        case SaveLevel:
          if (selection==0) {
            #if ENABLED(AUTO_BED_LEVELING_UBL)
              gcode.process_subcommands_now_P(PSTR("G29 S"));
              planner.synchronize();
              AudioFeedback(true);
            #else
              AudioFeedback(settings.save());
            #endif
          }
          Draw_Menu(Leveling, LEVELING_GET_MESH);
          break;
      #endif
      #if ENABLED(AUTO_BED_LEVELING_UBL)
        case MeshSlot:
          if (selection==0)
            ubl.storage_slot = 0;
          Redraw_Menu(true, true);
          break;
      #endif
      default:
        break;
    }
  DWIN_UpdateLCD();
}

void CrealityDWINClass::Confirm_Control() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch(popup) {
      case Complete:
        Draw_Main_Menu();
        break;
      case FilInsert:
        Popup_Handler(FilChange);
        wait_for_user = false;
        break;
      case HeaterTime:
        Popup_Handler(Heating);
        wait_for_user = false;
        break;
      default:
        Redraw_Menu(true, true, false);
        wait_for_user = false;
        break;
    }
  }
  DWIN_UpdateLCD();
}

/* In-Menu Value Modification */

void CrealityDWINClass::Setup_Value(float value, float min, float max, float unit, uint8_t type) {
  if (valuepointer == &thermalManager.temp_hotend[0].pid.Ki || valuepointer == &thermalManager.temp_bed.pid.Ki) 
    tempvalue = unscalePID_i(value) * unit;
  else if (valuepointer == &thermalManager.temp_hotend[0].pid.Kd || valuepointer == &thermalManager.temp_bed.pid.Kd) 
    tempvalue = unscalePID_d(value) * unit;
  else
    tempvalue = value * unit;
  valuemin = min;
  valuemax = max;
  valueunit = unit;
  valuetype = type;
  process = Value;
  EncoderRate.enabled = true;
  Draw_Float(tempvalue/unit, selection-scrollpos, true, valueunit);
}

void CrealityDWINClass::Modify_Value(float &value, float min, float max, float unit, void (*f)()/*=NULL*/) {
  valuepointer = &value;
  funcpointer = f;
  Setup_Value((float)value, min, max, unit, 0);
}
void CrealityDWINClass::Modify_Value(uint8_t &value, float min, float max, float unit, void (*f)()/*=NULL*/) {
  valuepointer = &value;
  funcpointer = f;
  Setup_Value((float)value, min, max, unit, 1);
}
void CrealityDWINClass::Modify_Value(uint16_t &value, float min, float max, float unit, void (*f)()/*=NULL*/) {
  valuepointer = &value;
  funcpointer = f;
  Setup_Value((float)value, min, max, unit, 2);
}
void CrealityDWINClass::Modify_Value(int16_t &value, float min, float max, float unit, void (*f)()/*=NULL*/) {
  valuepointer = &value;
  funcpointer = f;
  Setup_Value((float)value, min, max, unit, 3);
}
void CrealityDWINClass::Modify_Value(uint32_t &value, float min, float max, float unit, void (*f)()/*=NULL*/) {
  valuepointer = &value;
  funcpointer = f;
  Setup_Value((float)value, min, max, unit, 4);
}
void CrealityDWINClass::Modify_Value(int8_t &value, float min, float max, float unit, void (*f)()/*=NULL*/) {
  valuepointer = &value;
  funcpointer = f;
  Setup_Value((float)value, min, max, unit, 5);
}


void CrealityDWINClass::Modify_Option(uint8_t value, const char * const * options, uint8_t max) {
  tempvalue = value;
  valuepointer = const_cast<const char * *>(options);
  valuemin = 0;
  valuemax = max;
  process = Option;
  EncoderRate.enabled = true;
  Draw_Option(value, options, selection-scrollpos, true);
}

/* Main Functions */

void CrealityDWINClass::Update_Status(const char * const text) {
  char header[4];
  LOOP_L_N(i, 3) header[i] = text[i];
  header[3] = '\0';
  if (strcmp_P(header,"<F>")==0) {
    LOOP_L_N(i, _MIN((size_t)LONG_FILENAME_LENGTH, strlen(text))) filename[i] = text[i+3];
    filename[_MIN((size_t)LONG_FILENAME_LENGTH-1, strlen(text))] = '\0';
    Draw_Print_Filename(true);
  }
  else {
    LOOP_L_N(i, _MIN((size_t)64, strlen(text))) statusmsg[i] = text[i];
    statusmsg[_MIN((size_t)64, strlen(text))] = '\0';
  }
}

void CrealityDWINClass::Start_Print(bool sd) {
  sdprint = sd;
  if (!printing) {
    printing = true;
    statusmsg[0] = '\0';
    if (sd) {
      if (recovery.valid()) {
        SdFile *diveDir = nullptr;
        const char * const fname = card.diveToFile(true, diveDir, recovery.info.sd_filename);
        card.selectFileByName(fname);
      }
      strcpy_P(filename, card.longest_filename());
    }
    else
      strcpy_P(filename, "Host Print");
    ui.set_progress(0);
    ui.set_remaining_time(0);
    Draw_Print_Screen();
  }
}

void CrealityDWINClass::Stop_Print() {
  printing = false;
  sdprint = false;
  thermalManager.zero_fan_speeds();
  thermalManager.disable_all_heaters();
  ui.set_progress(100 * (PROGRESS_SCALE));
  ui.set_remaining_time(0);
  Draw_Print_confirm();
}

void CrealityDWINClass::Update() {
  State_Update();
  Screen_Update();

  switch(process) {
    case Main:
      Main_Menu_Control();
      break;
    case Menu:
      Menu_Control();
      break;
    case Value:
      Value_Control();
      break;
    case Option:
      Option_Control();
      break;
    case File:
      File_Control();
      break;
    case Print:
      Print_Screen_Control();
      break;
    case Popup:
      Popup_Control();
      break;
    case Confirm:
      Confirm_Control();
      break;
  }
}

void CrealityDWINClass::State_Update() {
  if ((print_job_timer.isRunning() || print_job_timer.isPaused()) != printing) {
    if (!printing) Start_Print((card.isFileOpen() || recovery.valid()));
    else Stop_Print();
  }
  if (print_job_timer.isPaused() != paused) {
    paused = print_job_timer.isPaused();
    if (process == Print) Print_Screen_Icons();
    if (process == Wait && !paused) Redraw_Menu(true, true);
  }
  if (wait_for_user && !(process == Confirm) && !print_job_timer.isPaused()) {
    Confirm_Handler(UserInput);
  }
  #if ENABLED(ADVANCED_PAUSE_FEATURE)
    if (process == Popup && popup == PurgeMore) {
      if (pause_menu_response == PAUSE_RESPONSE_EXTRUDE_MORE) {
        Popup_Handler(FilChange);
      }
      else if (pause_menu_response == PAUSE_RESPONSE_RESUME_PRINT) {
        if (printing) Popup_Handler(Resuming);
        else Redraw_Menu(true, true, (active_menu==PreheatHotend));
      }
    }
  #endif
  #if ENABLED(FILAMENT_RUNOUT_SENSOR)
    static bool ranout = false;
    if (runout.filament_ran_out != ranout) {
      ranout = runout.filament_ran_out;
      if (ranout) Popup_Handler(Runout);
    }
  #endif
}

void CrealityDWINClass::Screen_Update() {
  static millis_t scrltime = 0;
  if (ELAPSED(millis(), scrltime)) {
    scrltime = millis() + 200;
    Update_Status_Bar();
    if (process==Print)
      Draw_Print_Filename();
  }

  static millis_t statustime = 0;
  if (ELAPSED(millis(), statustime)) {
    statustime = millis() + 500;
    Draw_Status_Area();
  }

  static millis_t printtime = 0;
  if (ELAPSED(millis(), printtime)) {
    printtime = millis() + 1000;
    if (process == Print) {
      Draw_Print_ProgressBar();
      Draw_Print_ProgressElapsed();
      Draw_Print_ProgressRemain();
    }
  }

  static bool mounted = card.isMounted();
  if (mounted != card.isMounted()) {
    mounted = card.isMounted();
    if (process == File)
      Draw_SD_List();
  }

  #if HAS_HOTEND
    static int16_t hotendtarget = -1;
  #endif
  #if HAS_HEATED_BED
    static int16_t bedtarget = -1;
  #endif
  #if HAS_FAN
    static int16_t fanspeed = -1;
  #endif
  #if HAS_ZOFFSET_ITEM
    static float lastzoffset = zoffsetvalue;
    if (zoffsetvalue != lastzoffset && !printing) {
      lastzoffset = zoffsetvalue;
      #if HAS_BED_PROBE
        probe.offset.z = zoffsetvalue;
      #else
        set_home_offset(Z_AXIS, -zoffsetvalue);
      #endif
    }
    
    #if HAS_BED_PROBE
      if (probe.offset.z != lastzoffset) {
        zoffsetvalue = lastzoffset = probe.offset.z;
      }
    #else
      if (-home_offset.z != lastzoffset) {
        zoffsetvalue = lastzoffset = -home_offset.z;
      }
    #endif
  #endif

  if (process == Menu || process == Value) {
    switch(active_menu) {
      case TempMenu:
        #if HAS_HOTEND
          if (thermalManager.temp_hotend[0].target != hotendtarget) {
            hotendtarget = thermalManager.temp_hotend[0].target;
            if (scrollpos <= TEMP_HOTEND && TEMP_HOTEND <= scrollpos + MROWS) {
              if (process != Value || selection != TEMP_HOTEND-scrollpos)
                Draw_Float(thermalManager.temp_hotend[0].target, TEMP_HOTEND-scrollpos, false, 1);
            }
          }
        #endif
        #if HAS_HEATED_BED
          if (thermalManager.temp_bed.target != bedtarget) {
            bedtarget = thermalManager.temp_bed.target;
            if (scrollpos <= TEMP_BED && TEMP_BED <= scrollpos + MROWS) {
              if (process != Value || selection != TEMP_HOTEND-scrollpos)
                Draw_Float(thermalManager.temp_bed.target, TEMP_BED-scrollpos, false, 1);
            }
          }
        #endif
        #if HAS_FAN
          if (thermalManager.fan_speed[0] != fanspeed) {
            fanspeed = thermalManager.fan_speed[0];
            if (scrollpos <= TEMP_FAN && TEMP_FAN <= scrollpos + MROWS) {
              if (process != Value || selection != TEMP_HOTEND-scrollpos)
                Draw_Float(thermalManager.fan_speed[0], TEMP_FAN-scrollpos, false, 1);
            }
          }
        #endif
        break;
      case Tune:
        #if HAS_HOTEND
          if (thermalManager.temp_hotend[0].target != hotendtarget) {
            hotendtarget = thermalManager.temp_hotend[0].target;
            if (scrollpos <= TUNE_HOTEND && TUNE_HOTEND <= scrollpos + MROWS) {
              if (process != Value || selection != TEMP_HOTEND-scrollpos)
                Draw_Float(thermalManager.temp_hotend[0].target, TUNE_HOTEND-scrollpos, false, 1);
            }
          }
        #endif
        #if HAS_HEATED_BED
          if (thermalManager.temp_bed.target != bedtarget) {
            bedtarget = thermalManager.temp_bed.target;
            if (scrollpos <= TUNE_BED && TUNE_BED <= scrollpos + MROWS) {
              if (process != Value || selection != TEMP_HOTEND-scrollpos)
                Draw_Float(thermalManager.temp_bed.target, TUNE_BED-scrollpos, false, 1);
            }
          }
        #endif
        #if HAS_FAN
          if (thermalManager.fan_speed[0] != fanspeed) {
            fanspeed = thermalManager.fan_speed[0];
            if (scrollpos <= TUNE_FAN && TUNE_FAN <= scrollpos + MROWS) {
              if (process != Value || selection != TEMP_HOTEND-scrollpos)
                Draw_Float(thermalManager.fan_speed[0], TUNE_FAN-scrollpos, false, 1);
            }
          }
        #endif
        break;
    }
  }
}

void CrealityDWINClass::AudioFeedback(const bool success/*=true*/) {
  if (success) {
    if (eeprom_settings.beeperenable) {
      buzzer.tone(100, 659);
      buzzer.tone(10, 0);
      buzzer.tone(100, 698);
    }
    else Update_Status("Success");
  }
  else
    if (eeprom_settings.beeperenable)
      buzzer.tone(40, 440);
    else Update_Status("Failed");
}

void CrealityDWINClass::Save_Settings(char *buff) {
  #if ENABLED(AUTO_BED_LEVELING_UBL)
    eeprom_settings.tilt_grid_size = mesh_conf.tilt_grid-1;
  #endif
  eeprom_settings.corner_pos = corner_pos * 10;
  memcpy(buff, &eeprom_settings, min(sizeof(eeprom_settings), eeprom_data_size));
}

void CrealityDWINClass::Load_Settings(const char *buff) {
  memcpy(&eeprom_settings, buff, min(sizeof(eeprom_settings), eeprom_data_size));
  #if ENABLED(AUTO_BED_LEVELING_UBL)
    mesh_conf.tilt_grid = eeprom_settings.tilt_grid_size+1;
  #endif
  if (eeprom_settings.corner_pos == 0) eeprom_settings.corner_pos = 325;
  corner_pos = eeprom_settings.corner_pos / 10.0f;
  Redraw_Screen();
  static bool init = true;
  if (init) {
    init = false;
    queue.inject_P(PSTR("M1000 S"));
  }
}

void CrealityDWINClass::Reset_Settings() {
  eeprom_settings.time_format_textual = false;
  eeprom_settings.beeperenable = true;
  #if ENABLED(AUTO_BED_LEVELING_UBL)
    eeprom_settings.tilt_grid_size = 0;
  #endif
  eeprom_settings.corner_pos = 325;
  eeprom_settings.cursor_color = 0;
  eeprom_settings.menu_split_line = 0;
  eeprom_settings.menu_top_bg = 0;
  eeprom_settings.menu_top_txt = 0;
  eeprom_settings.highlight_box = 0;
  eeprom_settings.progress_percent = 0;
  eeprom_settings.progress_time = 0;
  eeprom_settings.status_bar_text = 0;
  eeprom_settings.status_area_text = 0;
  eeprom_settings.coordinates_text = 0;
  eeprom_settings.coordinates_split_line = 0;
  #if ENABLED(AUTO_BED_LEVELING_UBL)
    mesh_conf.tilt_grid = eeprom_settings.tilt_grid_size+1;
  #endif
  corner_pos = eeprom_settings.corner_pos / 10.0f;
  Redraw_Screen();
}

#endif
