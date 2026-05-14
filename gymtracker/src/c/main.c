#include <pebble.h>

#define MAX_EXERCISES 15
#define MAX_SLOTS 7
#define EXPORT_BUF_SIZE 1024

#define STORAGE_KEY_BASE 100 
#define SETTINGS_KEY_BASE 200
#define ACTIVE_STATE_KEY 300
#define ACTIVE_EX_BASE 400
#define ROUTINE_EX_BASE 1000 
#define V5_MIGRATION_KEY 500 
#define GHOST_KEY_BASE 800

typedef struct {
  char name[32];
  int total_exercises;
} RoutineHeader;

typedef struct {
  char routine_name[32];
  int total_exercises;
  int total_workout_sets;
  int curr_ex_idx;
  int workout_sec;
  int current_slot;
  int peak_hr;      
  int total_hr;     
  int hr_samples;   
} ActiveState;

// --- DATA STRUCTURES ---
typedef struct {
  char name[32];
  char comment[32];
  int target_weight;
  int actual_weight[10];
  int target_sets;
  int target_reps;
  int modifier;
  int current_set;
  int actual_reps[10];
} Exercise;

// --- APP STATE ---
static bool s_workout_active = false; 
static bool s_has_resume = false;     
static char s_routine_name[32] = "No Routine";
static Exercise s_exercises[MAX_EXERCISES];
static int s_total_exercises = 0;
static int s_total_workout_sets = 0; 
static char s_slot_names[MAX_SLOTS][32];
static int s_slot_counts[MAX_SLOTS];
static int s_active_slots = 0; 
static int s_progression_mode = -1;
static int s_weight_increment = 2;
static int s_current_slot = 0;
static bool s_is_paused = false; 
static int s_peak_hr = 0;
static int s_total_hr = 0;
static int s_hr_samples = 0;
static int s_last_workout_sec = 0;

// --- USER SETTINGS ---
static int s_set_rest_sec = 60; 
static int s_ex_rest_sec = 90; 
static int s_set_vibe = 1; 
static int s_ex_vibe = 3; 
static int s_rest_vibe = 2; 
static int s_theme_color_idx = 0; 
static int s_weight_unit_idx = 0; 
static int s_long_press_ms = 500; 
static int s_drop_set_pct = 20; 
static int s_super_rest_sec = 15; 
static int s_drop_rest_sec = 5; 
static int s_last_routine_slot = 0; 
static int s_dark_mode = 0; 
static int s_shortcut_up = 1; 
static int s_shortcut_down = 2; 
static int s_shortcut_select = 4;
static int s_dynamic_hr_target = 0; 
static int s_enable_ghost = 1;       // NEW: Ghost Toggle
static int s_enable_sensation = 1;   // NEW: Sensation Toggle

// --- THEME HELPERS ---
static bool is_dark_theme() {
  if (s_dark_mode == 1) return true;
  if (s_dark_mode == 2) {
    time_t temp = time(NULL);
    struct tm *t = localtime(&temp);
    if (t && (t->tm_hour >= 20 || t->tm_hour <= 7)) return true; 
  }
  return false;
}

static GColor get_bg_color() { return is_dark_theme() ? GColorBlack : GColorWhite; }
static GColor get_text_color() { return is_dark_theme() ? GColorWhite : GColorBlack; }

// --- WORKOUT TRACKING ---
static int s_curr_ex_idx = 0;
static int s_workout_sec = 0;
static int s_temp_reps = 0;
static int s_temp_weight = 0;
static int s_edit_mode = 0;
static bool s_is_timed_active = false;

// --- GEOMETRY & CACHING ---
static int s_line1_y, s_line2_y;
static int s_labels_y, s_actual_y, s_target_y; 
static int s_highlight_box_width = 0;

// --- UI ELEMENTS ---
static Window *s_main_window, *s_settings_window, *s_workout_window, *s_help_window, *s_confirm_window, *s_summary_window, *s_variation_window, *s_exit_window;
static Window *s_sensation_window;
static MenuLayer *s_menu_layer, *s_settings_menu_layer, *s_variation_menu_layer, *s_sensation_menu_layer; 
static Layer *s_main_header_bg, *s_settings_header_bg, *s_progress_layer, *s_workout_bg_layer, *s_summary_bg_layer, *s_rest_overlay_layer;
static Layer *s_highlight_layer; 
static Animation *s_box_anim = NULL;
static Animation *s_rest_anim = NULL;
static TextLayer *s_main_header_text, *s_settings_header_text, *s_timer_layer, *s_clock_layer, *s_exercise_layer; 
static TextLayer *s_next_exercise_layer, *s_set_layer, *s_label_reps_layer, *s_label_weight_layer, *s_target_reps_layer; 
static TextLayer *s_target_weight_layer, *s_actual_reps_layer, *s_actual_weight_layer, *s_help_text_layer;
#if !defined(PBL_ROUND)
static TextLayer *s_hr_layer;
#endif
static TextLayer *s_confirm_text_layer, *s_sum_title_layer, *s_sum_info_layer, *s_exit_text_layer;
static TextLayer *s_rest_title_layer, *s_rest_time_layer, *s_rest_skip_layer;
static TextLayer *s_beat_layer, *s_missed_layer, *s_accuracy_layer, *s_density_layer;

static Window *s_action_window, *s_inspector_window;
static SimpleMenuLayer *s_action_menu_layer;
static SimpleMenuSection s_action_menu_sections[1];
static SimpleMenuItem s_action_menu_items[3];
static MenuLayer *s_inspector_menu_layer;

static Window *s_mega_window;
static SimpleMenuLayer *s_mega_menu_layer;
static SimpleMenuSection s_mega_menu_sections[1];
static SimpleMenuItem s_mega_menu_items[7];

static Window *s_note_window;
static TextLayer *s_note_text_layer;

static int s_workout_sensation = 3; 
static const char *s_sensation_titles[] = {"Unstoppable", "Strong", "Normal", "Exhausted", "Struggled"};
static const char *s_sensation_subs[] = {"Felt amazing!", "Hit targets well", "Got work done", "Completely drained", "Felt weak/off"};

static const char *s_variations[] = {
  "None (Clear)", "(Seated)", "(Standing)", "(Dumbbell)", "(Barbell)", 
  "(Cable)", "(Machine)", "(Single Arm)", "(Single Leg)"
};
#define NUM_VARIATIONS 9

static int s_slot_to_edit = -1;
static int s_target_swap_slot = -1; 
static bool s_is_resting = false;
static int s_rest_seconds_remaining = 0;

static DictationSession *s_dictation_session = NULL; 

// --- FORWARD DECLARATIONS FOR LAZY LOADING ---
static void update_workout_ui(bool animate_box);
static void push_exit_window();
static void push_sensation_window();
static void settings_window_load(Window *window);
static void settings_window_unload(Window *window);
static void help_window_load(Window *window);
static void help_window_unload(Window *window);
static void confirm_window_load(Window *window);
static void confirm_window_unload(Window *window);
static void confirm_click_provider(void *context);
static void workout_window_load(Window *window);
static void workout_window_unload(Window *window);
static void wo_click_provider(void *context);
static void summary_window_load(Window *window);
static void summary_window_unload(Window *window);
static void summary_click_provider(void *context);

// --- CORE UTILITIES ---
static TextLayer* build_text_layer(GRect bounds, const char *font_key, GColor text_color, GTextAlignment alignment, Layer *parent) {
  TextLayer *text_layer = text_layer_create(bounds);
  text_layer_set_font(text_layer, fonts_get_system_font(font_key));
  
  if (is_dark_theme()) {
    if (gcolor_equal(text_color, GColorBlack)) text_color = GColorWhite;
    else if (gcolor_equal(text_color, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack))) text_color = PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite);
    else if (gcolor_equal(text_color, PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorBlack))) text_color = PBL_IF_COLOR_ELSE(GColorPictonBlue, GColorWhite);
  }
  
  text_layer_set_text_color(text_layer, text_color);
  text_layer_set_background_color(text_layer, GColorClear);
  text_layer_set_text_alignment(text_layer, alignment);
  text_layer_set_overflow_mode(text_layer, GTextOverflowModeTrailingEllipsis);
  
  if (parent) layer_add_child(parent, text_layer_get_layer(text_layer));
  return text_layer;
}

static GColor get_theme_color() {
  #if !defined(PBL_COLOR)
    return is_dark_theme() ? GColorWhite : GColorBlack;
  #else
    if (s_theme_color_idx >= 4) {
      uint8_t color_val = s_theme_color_idx - 4; 
      return (GColor){ .argb = 0b11000000 | color_val };
    }
    if (s_theme_color_idx == 1) return GColorCobaltBlue;
    if (s_theme_color_idx == 2) return GColorRed;
    if (s_theme_color_idx == 3) return GColorIslamicGreen;
    return GColorOrange; 
  #endif
}

static void play_vibe(int type) {
  if(type == 1) vibes_short_pulse();
  else if(type == 2) vibes_long_pulse();
  else if(type == 3) vibes_double_pulse();
}

static void load_settings() {
  if(persist_exists(SETTINGS_KEY_BASE + 0)) s_set_rest_sec = persist_read_int(SETTINGS_KEY_BASE + 0);
  if(persist_exists(SETTINGS_KEY_BASE + 1)) s_ex_rest_sec = persist_read_int(SETTINGS_KEY_BASE + 1);
  if(persist_exists(SETTINGS_KEY_BASE + 2)) s_set_vibe = persist_read_int(SETTINGS_KEY_BASE + 2);
  if(persist_exists(SETTINGS_KEY_BASE + 3)) s_ex_vibe = persist_read_int(SETTINGS_KEY_BASE + 3);
  if(persist_exists(SETTINGS_KEY_BASE + 4)) s_rest_vibe = persist_read_int(SETTINGS_KEY_BASE + 4);
  if(persist_exists(SETTINGS_KEY_BASE + 5)) s_theme_color_idx = persist_read_int(SETTINGS_KEY_BASE + 5);
  if(persist_exists(SETTINGS_KEY_BASE + 6)) s_weight_unit_idx = persist_read_int(SETTINGS_KEY_BASE + 6);
  if(persist_exists(SETTINGS_KEY_BASE + 7)) s_long_press_ms = persist_read_int(SETTINGS_KEY_BASE + 7);
  if(persist_exists(SETTINGS_KEY_BASE + 8)) s_drop_set_pct = persist_read_int(SETTINGS_KEY_BASE + 8);
  if(persist_exists(SETTINGS_KEY_BASE + 9)) s_super_rest_sec = persist_read_int(SETTINGS_KEY_BASE + 9);
  if(persist_exists(SETTINGS_KEY_BASE + 10)) s_progression_mode = persist_read_int(SETTINGS_KEY_BASE + 10);
  if(persist_exists(SETTINGS_KEY_BASE + 11)) s_weight_increment = persist_read_int(SETTINGS_KEY_BASE + 11);
  if(persist_exists(SETTINGS_KEY_BASE + 12)) s_drop_rest_sec = persist_read_int(SETTINGS_KEY_BASE + 12);
  if(persist_exists(SETTINGS_KEY_BASE + 13)) s_last_routine_slot = persist_read_int(SETTINGS_KEY_BASE + 13);
  if(persist_exists(SETTINGS_KEY_BASE + 14)) s_dark_mode = persist_read_int(SETTINGS_KEY_BASE + 14);
  if(persist_exists(SETTINGS_KEY_BASE + 15)) s_shortcut_up = persist_read_int(SETTINGS_KEY_BASE + 15);
  if(persist_exists(SETTINGS_KEY_BASE + 16)) s_shortcut_down = persist_read_int(SETTINGS_KEY_BASE + 16);
  if(persist_exists(SETTINGS_KEY_BASE + 17)) s_shortcut_select = persist_read_int(SETTINGS_KEY_BASE + 17);
  if(persist_exists(SETTINGS_KEY_BASE + 18)) s_dynamic_hr_target = persist_read_int(SETTINGS_KEY_BASE + 18); 
  if(persist_exists(SETTINGS_KEY_BASE + 19)) s_enable_ghost = persist_read_int(SETTINGS_KEY_BASE + 19);
  if(persist_exists(SETTINGS_KEY_BASE + 20)) s_enable_sensation = persist_read_int(SETTINGS_KEY_BASE + 20);
}

static void save_setting(int key_offset, int value) {
  persist_write_int(SETTINGS_KEY_BASE + key_offset, value);
}

static void parse_routine_string(const char *data) {
  s_total_exercises = 0;
  memset(&s_exercises, 0, sizeof(s_exercises)); 
  if (!data) return;

  int i = 0, token_count = 0, t_idx = 0;
  char temp[32];

  while (true) {
    if (data[i] == '|' || data[i] == '\0') {
      temp[t_idx] = '\0'; 
      if (token_count == 0) {
        snprintf(s_routine_name, sizeof(s_routine_name), "%s", temp);
      } else {
        int ex_idx = (token_count - 1) / 6; 
        int field = (token_count - 1) % 6;
        
        if (ex_idx < MAX_EXERCISES) {
          if (field == 0) snprintf(s_exercises[ex_idx].name, sizeof(s_exercises[ex_idx].name), "%s", temp);
          else if (field == 1) s_exercises[ex_idx].target_sets = atoi(temp);
          else if (field == 2) s_exercises[ex_idx].target_reps = atoi(temp);
          else if (field == 3) s_exercises[ex_idx].target_weight = atoi(temp);
          else if (field == 4) {
            s_exercises[ex_idx].modifier = atoi(temp);
            if (s_exercises[ex_idx].modifier == 4) {
                s_exercises[ex_idx].modifier = 0;
                s_exercises[ex_idx].target_weight = 0;
            }
            if (s_exercises[ex_idx].modifier == 6) {
                s_exercises[ex_idx].target_weight = 0;
            }
            if (s_exercises[ex_idx].modifier == 1) {
                s_exercises[ex_idx].target_sets *= 2;
                if (s_exercises[ex_idx].target_sets > 10) s_exercises[ex_idx].target_sets = 10; 
            }
            s_exercises[ex_idx].current_set = 1;
          }
          else if (field == 5) {
            if (strcmp(temp, "-") == 0) {
                s_exercises[ex_idx].comment[0] = '\0'; 
            } else {
                snprintf(s_exercises[ex_idx].comment, sizeof(s_exercises[ex_idx].comment), "%s", temp);
            }
            s_total_exercises = ex_idx + 1; 
          }
        }
      }
      token_count++;
      t_idx = 0; 
      if (data[i] == '\0') break; 
    } else {
      if (t_idx < 31) temp[t_idx++] = data[i];
    }
    i++;
  }

  s_total_workout_sets = 0;
  for (int k = 0; k < s_total_exercises; k++) {
    s_total_workout_sets += s_exercises[k].target_sets;
  }
}

static void refresh_directory() {
  s_active_slots = 0;
  RoutineHeader header;

  for(int i = 0; i < MAX_SLOTS; i++) {
    if(persist_exists(STORAGE_KEY_BASE + i)) {
      persist_read_data(STORAGE_KEY_BASE + i, &header, sizeof(RoutineHeader));
      
      if (i != s_active_slots) {
        persist_write_data(STORAGE_KEY_BASE + s_active_slots, &header, sizeof(RoutineHeader));
        for(int j = 0; j < header.total_exercises; j++) {
          Exercise temp_ex;
          if (persist_read_data(ROUTINE_EX_BASE + (i * MAX_EXERCISES) + j, &temp_ex, sizeof(Exercise)) > 0) {
              persist_write_data(ROUTINE_EX_BASE + (s_active_slots * MAX_EXERCISES) + j, &temp_ex, sizeof(Exercise));
          }
          persist_delete(ROUTINE_EX_BASE + (i * MAX_EXERCISES) + j);
        }
        persist_delete(STORAGE_KEY_BASE + i);
      }

      snprintf(s_slot_names[s_active_slots], sizeof(s_slot_names[s_active_slots]), "%s", header.name);
      s_slot_counts[s_active_slots] = header.total_exercises;
      s_active_slots++;
    }
  }
  
  for (int i = s_active_slots; i < MAX_SLOTS; i++) {
    snprintf(s_slot_names[i], sizeof(s_slot_names[i]), "Empty Slot");
    s_slot_counts[i] = 0;
  }
}

// --- LAZY LOADING WRAPPERS ---
static void push_settings_window() {
  if(!s_settings_window) {
    s_settings_window = window_create();
    window_set_window_handlers(s_settings_window, (WindowHandlers) { .load = settings_window_load, .unload = settings_window_unload });
  }
  window_set_background_color(s_settings_window, get_bg_color());
  window_stack_push(s_settings_window, true);
}

static void push_help_window() {
  if(!s_help_window) {
    s_help_window = window_create();
    window_set_window_handlers(s_help_window, (WindowHandlers) { .load = help_window_load, .unload = help_window_unload });
  }
  window_set_background_color(s_help_window, get_bg_color());
  window_stack_push(s_help_window, true);
}

static void push_confirm_window() {
  if(!s_confirm_window) {
    s_confirm_window = window_create();
    window_set_click_config_provider(s_confirm_window, confirm_click_provider);
    window_set_window_handlers(s_confirm_window, (WindowHandlers) { .load = confirm_window_load, .unload = confirm_window_unload });
  }
  window_set_background_color(s_confirm_window, get_bg_color());
  window_stack_push(s_confirm_window, true);
}

static void push_workout_window() {
  if(!s_workout_window) {
    s_workout_window = window_create();
    window_set_click_config_provider(s_workout_window, wo_click_provider);
    window_set_window_handlers(s_workout_window, (WindowHandlers) { .load = workout_window_load, .unload = workout_window_unload });
  }
  window_set_background_color(s_workout_window, get_bg_color());
  window_stack_push(s_workout_window, true);
}

static void push_summary_window() {
  if(!s_summary_window) {
    s_summary_window = window_create();
    window_set_click_config_provider(s_summary_window, summary_click_provider);
    window_set_window_handlers(s_summary_window, (WindowHandlers) { .load = summary_window_load, .unload = summary_window_unload });
  }
  window_set_background_color(s_summary_window, get_bg_color());
  window_stack_push(s_summary_window, true);
}

// --- SETTINGS WINDOW LOGIC ---
static uint16_t settings_get_num_sections_callback(MenuLayer *menu_layer, void *data) { 
  return 5; 
}

static uint16_t settings_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) { 
  if (section_index == 0) return 5; 
  if (section_index == 1) return 3; 
  if (section_index == 2) return 1; 
  if (section_index == 3) return 6; // V6.2 Fix: Added two new toggles
  if (section_index == 4) return 3; 
  return 0;
}

static int16_t settings_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void settings_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  switch (section_index) {
    case 0: menu_cell_basic_header_draw(ctx, cell_layer, "Timers"); break;
    case 1: menu_cell_basic_header_draw(ctx, cell_layer, "Haptics"); break;
    case 2: menu_cell_basic_header_draw(ctx, cell_layer, "Modifiers"); break;
    case 3: menu_cell_basic_header_draw(ctx, cell_layer, "System"); break;
    case 4: menu_cell_basic_header_draw(ctx, cell_layer, "Shortcuts"); break;
  }
}

static void settings_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  char title[32], subtitle[32];
  static const char* vibes[] = {"Off", "Short", "Long", "Double"};
  static const char* themes[] = {"Orange", "Blue", "Red", "Green"};
  static const char* units[] = {"kg", "lbs"};

  if (cell_index->section == 0) { 
    switch(cell_index->row) {
      case 0: snprintf(title, sizeof(title), "Set Rest"); snprintf(subtitle, sizeof(subtitle), "%ds", s_set_rest_sec); break;
      case 1: snprintf(title, sizeof(title), "Ex. Rest"); snprintf(subtitle, sizeof(subtitle), "%ds", s_ex_rest_sec); break;
      case 2: snprintf(title, sizeof(title), "Super Rest"); snprintf(subtitle, sizeof(subtitle), "%ds", s_super_rest_sec); break;
      case 3: snprintf(title, sizeof(title), "Drop Rest"); snprintf(subtitle, sizeof(subtitle), "%ds", s_drop_rest_sec); break;
      case 4: snprintf(title, sizeof(title), "Dynamic Rest"); 
              if(s_dynamic_hr_target == 0) snprintf(subtitle, sizeof(subtitle), "Off (Timer Only)");
              else snprintf(subtitle, sizeof(subtitle), "Resume @ %d BPM", s_dynamic_hr_target);
              break;
    }
  } else if (cell_index->section == 1) { 
    switch(cell_index->row) {
      case 0: snprintf(title, sizeof(title), "Set Vibe"); snprintf(subtitle, sizeof(subtitle), "%s", vibes[s_set_vibe]); break;
      case 1: snprintf(title, sizeof(title), "Ex. Vibe"); snprintf(subtitle, sizeof(subtitle), "%s", vibes[s_ex_vibe]); break;
      case 2: snprintf(title, sizeof(title), "Rest Vibe"); snprintf(subtitle, sizeof(subtitle), "%s", vibes[s_rest_vibe]); break;
    }
  } else if (cell_index->section == 2) { 
    switch(cell_index->row) {
      case 0: snprintf(title, sizeof(title), "Drop Set %%"); snprintf(subtitle, sizeof(subtitle), "-%d%%", s_drop_set_pct); break;
    }
  } else if (cell_index->section == 3) { 
    switch(cell_index->row) {
      case 0: 
        snprintf(title, sizeof(title), "Theme Color"); 
        if (s_theme_color_idx >= 4) snprintf(subtitle, sizeof(subtitle), "Secret Mode: %d", (s_theme_color_idx - 4));
        else snprintf(subtitle, sizeof(subtitle), "%s", themes[s_theme_color_idx]);
        break;
      case 1: snprintf(title, sizeof(title), "Weight Unit"); snprintf(subtitle, sizeof(subtitle), "%s", units[s_weight_unit_idx]); break;
      case 2: snprintf(title, sizeof(title), "Long Press"); snprintf(subtitle, sizeof(subtitle), "%d ms", s_long_press_ms); break; 
      case 3: 
        snprintf(title, sizeof(title), "Dark Mode"); 
        if (s_dark_mode == 0) snprintf(subtitle, sizeof(subtitle), "Off");
        else if (s_dark_mode == 1) snprintf(subtitle, sizeof(subtitle), "On");
        else snprintf(subtitle, sizeof(subtitle), "Auto (8PM-7AM)");
        break;
      case 4: 
        snprintf(title, sizeof(title), "Ghost Pacer"); 
        snprintf(subtitle, sizeof(subtitle), s_enable_ghost ? "On" : "Off"); 
        break;
      case 5: 
        snprintf(title, sizeof(title), "Sensation Q."); 
        snprintf(subtitle, sizeof(subtitle), s_enable_sensation ? "On" : "Off"); 
        break;
    }
  } else if (cell_index->section == 4) { 
    static const char* actions[] = {"Variations", "View Note", "Swap (Later)", "Skip Entirely", "Finish Set", "Skip Set", "Voice Note"};
    switch(cell_index->row) {
      case 0: snprintf(title, sizeof(title), "Up Long Press"); snprintf(subtitle, sizeof(subtitle), "%s", actions[s_shortcut_up]); break;
      case 1: snprintf(title, sizeof(title), "Down Long Press"); snprintf(subtitle, sizeof(subtitle), "%s", actions[s_shortcut_down]); break;
      case 2: snprintf(title, sizeof(title), "Select Long Press"); snprintf(subtitle, sizeof(subtitle), "%s", actions[s_shortcut_select]); break;
    }
  }
  menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
}

static void settings_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (cell_index->section == 0) { 
    switch(cell_index->row) {
      case 0: s_set_rest_sec += 15; if(s_set_rest_sec > 180) s_set_rest_sec = 0; save_setting(0, s_set_rest_sec); break;
      case 1: s_ex_rest_sec += 30; if(s_ex_rest_sec > 240) s_ex_rest_sec = 0; save_setting(1, s_ex_rest_sec); break;
      case 2: s_super_rest_sec += 5; if(s_super_rest_sec > 30) s_super_rest_sec = 0; save_setting(9, s_super_rest_sec); break;
      case 3: s_drop_rest_sec += 5; if(s_drop_rest_sec > 30) s_drop_rest_sec = 5; save_setting(12, s_drop_rest_sec); break; 
      case 4: s_dynamic_hr_target += 5; if(s_dynamic_hr_target > 160) s_dynamic_hr_target = 0; if(s_dynamic_hr_target > 0 && s_dynamic_hr_target < 90) s_dynamic_hr_target = 90; save_setting(18, s_dynamic_hr_target); break;
    }
  } else if (cell_index->section == 1) { 
    switch(cell_index->row) {
      case 0: s_set_vibe++; if(s_set_vibe > 3) s_set_vibe = 0; save_setting(2, s_set_vibe); play_vibe(s_set_vibe); break;
      case 1: s_ex_vibe++; if(s_ex_vibe > 3) s_ex_vibe = 0; save_setting(3, s_ex_vibe); play_vibe(s_ex_vibe); break;
      case 2: s_rest_vibe++; if(s_rest_vibe > 3) s_rest_vibe = 0; save_setting(4, s_rest_vibe); play_vibe(s_rest_vibe); break;
    }
  } else if (cell_index->section == 2) { 
    switch(cell_index->row) {
      case 0: s_drop_set_pct += 5; if(s_drop_set_pct > 50) s_drop_set_pct = 10; save_setting(8, s_drop_set_pct); break;
    }
  } else if (cell_index->section == 3) { 
    switch(cell_index->row) {
      case 0: 
        s_theme_color_idx++; 
        if (s_theme_color_idx == 4) s_theme_color_idx = 0; 
        else if (s_theme_color_idx > 67) s_theme_color_idx = 4; 
        save_setting(5, s_theme_color_idx); 
        menu_layer_set_highlight_colors(s_settings_menu_layer, get_theme_color(), get_bg_color()); 
        break;
      case 1: s_weight_unit_idx++; if(s_weight_unit_idx > 1) s_weight_unit_idx = 0; save_setting(6, s_weight_unit_idx); break;
      case 2: s_long_press_ms += 250; if(s_long_press_ms > 1500) s_long_press_ms = 250; save_setting(7, s_long_press_ms); break; 
      case 3: 
        s_dark_mode++; 
        if(s_dark_mode > 2) s_dark_mode = 0; 
        save_setting(14, s_dark_mode); 
        window_set_background_color(s_settings_window, get_bg_color());
        menu_layer_set_normal_colors(s_settings_menu_layer, get_bg_color(), get_text_color());
        menu_layer_set_highlight_colors(s_settings_menu_layer, get_theme_color(), get_bg_color());
        break;
      case 4: 
        s_enable_ghost = !s_enable_ghost; 
        save_setting(19, s_enable_ghost); 
        break;
      case 5: 
        s_enable_sensation = !s_enable_sensation; 
        save_setting(20, s_enable_sensation); 
        break;
    }
  } else if (cell_index->section == 4) { 
    switch(cell_index->row) {
      case 0: s_shortcut_up++; if(s_shortcut_up > 6) s_shortcut_up = 0; save_setting(15, s_shortcut_up); break;
      case 1: s_shortcut_down++; if(s_shortcut_down > 6) s_shortcut_down = 0; save_setting(16, s_shortcut_down); break;
      case 2: s_shortcut_select++; if(s_shortcut_select > 6) s_shortcut_select = 0; save_setting(17, s_shortcut_select); break;
    }
  }
  menu_layer_reload_data(s_settings_menu_layer);
}

static void header_bg_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone); 
}

static void settings_select_long_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (cell_index->section == 3 && cell_index->row == 0) {
    if (s_theme_color_idx < 4) s_theme_color_idx = 4; 
    else s_theme_color_idx = 0; 
    
    save_setting(5, s_theme_color_idx);
    vibes_double_pulse(); 
    menu_layer_reload_data(s_settings_menu_layer);
    menu_layer_set_highlight_colors(s_settings_menu_layer, get_theme_color(), get_bg_color());
  }
}

static void settings_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);
  
  s_settings_header_bg = layer_create(GRect(0, 0, bounds.size.w, 40));
  layer_set_update_proc(s_settings_header_bg, header_bg_update_proc);
  layer_add_child(w_layer, s_settings_header_bg);
  
  s_settings_header_text = build_text_layer(GRect(0, 6, bounds.size.w, 30), FONT_KEY_GOTHIC_24_BOLD, GColorWhite, GTextAlignmentCenter, s_settings_header_bg);
  text_layer_set_text(s_settings_header_text, "Settings");

  s_settings_menu_layer = menu_layer_create(GRect(0, 40, bounds.size.w, bounds.size.h - 40));
  menu_layer_set_callbacks(s_settings_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = settings_get_num_sections_callback,  
    .get_num_rows = settings_get_num_rows_callback,
    .get_header_height = settings_get_header_height_callback,    
    .draw_header = settings_draw_header_callback,                
    .draw_row = settings_draw_row_callback,
    .select_click = settings_select_callback,
    .select_long_click = settings_select_long_callback, 
  });
  
  menu_layer_set_normal_colors(s_settings_menu_layer, get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(s_settings_menu_layer, get_theme_color(), get_bg_color());
  menu_layer_set_click_config_onto_window(s_settings_menu_layer, window);
  layer_add_child(w_layer, menu_layer_get_layer(s_settings_menu_layer));
}

static void settings_window_unload(Window *window) {
  text_layer_destroy(s_settings_header_text);
  layer_destroy(s_settings_header_bg);
  menu_layer_destroy(s_settings_menu_layer); 
  
  window_set_background_color(s_main_window, get_bg_color());
  menu_layer_set_normal_colors(s_menu_layer, get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(s_menu_layer, get_theme_color(), get_bg_color());
  
  if (s_workout_window) { window_destroy(s_workout_window); s_workout_window = NULL; }
  if (s_summary_window) { window_destroy(s_summary_window); s_summary_window = NULL; }
}

// --- HELP WINDOW LOGIC ---
static void help_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);
  s_help_text_layer = build_text_layer(GRect(10, 40, bounds.size.w - 20, bounds.size.h - 40), FONT_KEY_GOTHIC_24_BOLD, GColorBlack, GTextAlignmentCenter, w_layer);
  text_layer_set_text(s_help_text_layer, "Empty Slot\n\nOpen this app's settings on your phone to send a routine here.");
}
static void help_window_unload(Window *window) { text_layer_destroy(s_help_text_layer); }

// --- EDIT / CONFIRM WINDOW LOGIC ---
static void update_edit_ui() {
  static char edit_buf[128];
  if (s_slot_to_edit == s_target_swap_slot) {
    snprintf(edit_buf, sizeof(edit_buf), "DELETE ROUTINE:\n'%s'\n\n[Up/Down] Move\n[Select] Delete", s_slot_names[s_slot_to_edit]);
  } else if (s_slot_counts[s_target_swap_slot] == 0) {
    snprintf(edit_buf, sizeof(edit_buf), "MOVE TO END\n\n[Up/Down] Move\n[Select] Swap");
  } else {
    snprintf(edit_buf, sizeof(edit_buf), "SWAP TO SLOT:\n'%s'\n\n[Up/Down] Move\n[Select] Swap", s_slot_names[s_target_swap_slot]);
  }
  text_layer_set_text(s_confirm_text_layer, edit_buf);
}

static void edit_up_click(ClickRecognizerRef recognizer, void *context) {
  s_target_swap_slot--;
  if (s_target_swap_slot < 0) s_target_swap_slot = (s_active_slots < MAX_SLOTS) ? s_active_slots : MAX_SLOTS - 1; 
  update_edit_ui();
}
static void edit_down_click(ClickRecognizerRef recognizer, void *context) {
  s_target_swap_slot++;
  if (s_target_swap_slot > ((s_active_slots < MAX_SLOTS) ? s_active_slots : MAX_SLOTS - 1)) s_target_swap_slot = 0; 
  update_edit_ui();
}
static void edit_select_click(ClickRecognizerRef recognizer, void *context) {
  if (s_slot_to_edit == s_target_swap_slot) {
    persist_delete(STORAGE_KEY_BASE + s_slot_to_edit);
    for(int j = 0; j < MAX_EXERCISES; j++) persist_delete(ROUTINE_EX_BASE + (s_slot_to_edit * MAX_EXERCISES) + j);
  } else {
    RoutineHeader edit_header, target_header;
    bool target_exists = persist_exists(STORAGE_KEY_BASE + s_target_swap_slot);
    
    persist_read_data(STORAGE_KEY_BASE + s_slot_to_edit, &edit_header, sizeof(RoutineHeader));
    if (target_exists) persist_read_data(STORAGE_KEY_BASE + s_target_swap_slot, &target_header, sizeof(RoutineHeader));
    
    persist_write_data(STORAGE_KEY_BASE + s_target_swap_slot, &edit_header, sizeof(RoutineHeader));
    if (target_exists) persist_write_data(STORAGE_KEY_BASE + s_slot_to_edit, &target_header, sizeof(RoutineHeader));
    else persist_delete(STORAGE_KEY_BASE + s_slot_to_edit);

    for(int j = 0; j < MAX_EXERCISES; j++) {
        Exercise edit_ex, target_ex;
        bool e_exists = (persist_read_data(ROUTINE_EX_BASE + (s_slot_to_edit * MAX_EXERCISES) + j, &edit_ex, sizeof(Exercise)) > 0);
        bool t_exists = target_exists && (persist_read_data(ROUTINE_EX_BASE + (s_target_swap_slot * MAX_EXERCISES) + j, &target_ex, sizeof(Exercise)) > 0);

        if (e_exists) persist_write_data(ROUTINE_EX_BASE + (s_target_swap_slot * MAX_EXERCISES) + j, &edit_ex, sizeof(Exercise));
        else persist_delete(ROUTINE_EX_BASE + (s_target_swap_slot * MAX_EXERCISES) + j);

        if (t_exists) persist_write_data(ROUTINE_EX_BASE + (s_slot_to_edit * MAX_EXERCISES) + j, &target_ex, sizeof(Exercise));
        else persist_delete(ROUTINE_EX_BASE + (s_slot_to_edit * MAX_EXERCISES) + j);
    }
  }
  refresh_directory();
  menu_layer_reload_data(s_menu_layer);
  window_stack_pop(true);
}

static void confirm_click_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, edit_select_click);
  window_single_click_subscribe(BUTTON_ID_UP, edit_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, edit_down_click);
}

static void confirm_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);
  s_confirm_text_layer = build_text_layer(GRect(5, 25, bounds.size.w - 10, bounds.size.h - 25), FONT_KEY_GOTHIC_24_BOLD, GColorBlack, GTextAlignmentCenter, w_layer);
  update_edit_ui(); 
}
static void confirm_window_unload(Window *window) { text_layer_destroy(s_confirm_text_layer); }

// --- EXIT CONFIRMATION LOGIC ---
static void exit_up_click(ClickRecognizerRef recognizer, void *context) {
  s_is_paused = false;
  window_stack_pop(true); 
}

static void exit_select_click(ClickRecognizerRef recognizer, void *context) {
  s_is_paused = false;
  s_workout_active = false;          
  persist_delete(ACTIVE_STATE_KEY);  
  vibes_double_pulse();
  if (s_enable_sensation) push_sensation_window();
  else push_summary_window();
  window_stack_remove(s_workout_window, false); 
  window_stack_remove(s_exit_window, false);    
}

static void exit_down_click(ClickRecognizerRef recognizer, void *context) {
  s_is_paused = false;
  s_workout_active = false;          
  s_has_resume = false;              
  persist_delete(ACTIVE_STATE_KEY);  
  window_stack_remove(s_workout_window, false); 
  window_stack_pop(true); 
  menu_layer_reload_data(s_menu_layer); 
}

static void exit_click_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, exit_up_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, exit_select_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, exit_down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, exit_up_click); 
}

static void exit_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);
  s_exit_text_layer = build_text_layer(GRect(5, 20, bounds.size.w - 10, bounds.size.h - 20), FONT_KEY_GOTHIC_24_BOLD, GColorBlack, GTextAlignmentCenter, w_layer);
  text_layer_set_text(s_exit_text_layer, "END WORKOUT?\n\n[Up] Resume\n[Select] Save\n[Down] Discard");
}

static void exit_window_unload(Window *window) {
  text_layer_destroy(s_exit_text_layer);
}

static void push_exit_window() {
  if(!s_exit_window) {
    s_exit_window = window_create();
    window_set_click_config_provider(s_exit_window, exit_click_provider);
    window_set_window_handlers(s_exit_window, (WindowHandlers) { .load = exit_window_load, .unload = exit_window_unload });
  }
  window_set_background_color(s_exit_window, get_bg_color());
  window_stack_push(s_exit_window, true);
}

// --- SENSATION WINDOW LOGIC ---
static uint16_t sensation_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) { return 5; }

static void sensation_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  menu_cell_basic_draw(ctx, cell_layer, s_sensation_titles[cell_index->row], s_sensation_subs[cell_index->row], NULL);
}

static void sensation_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  s_workout_sensation = 5 - cell_index->row; 
  push_summary_window(); 
  window_stack_remove(s_sensation_window, false); 
}

static void sensation_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);
  
  s_sensation_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_sensation_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = sensation_get_num_rows_callback,
    .draw_row = sensation_draw_row_callback,
    .select_click = sensation_select_callback,
  });
  
  menu_layer_set_normal_colors(s_sensation_menu_layer, get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(s_sensation_menu_layer, get_theme_color(), get_bg_color());
  menu_layer_set_click_config_onto_window(s_sensation_menu_layer, window);
  layer_add_child(w_layer, menu_layer_get_layer(s_sensation_menu_layer));
}

static void sensation_window_unload(Window *window) { menu_layer_destroy(s_sensation_menu_layer); }

static void push_sensation_window() {
  if(!s_sensation_window) {
    s_sensation_window = window_create();
    window_set_window_handlers(s_sensation_window, (WindowHandlers) { .load = sensation_window_load, .unload = sensation_window_unload });
  }
  window_set_background_color(s_sensation_window, get_bg_color());
  window_stack_push(s_sensation_window, true);
}

// --- SUMMARY WINDOW LOGIC ---
static void summary_exit_click(ClickRecognizerRef recognizer, void *context) { window_stack_pop(true); }

static void summary_click_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, summary_exit_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, summary_exit_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, summary_exit_click);
}

static void summary_bg_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int mid = bounds.size.w / 2;
  
  #if defined(PBL_ROUND)
    int y_top = (bounds.size.h * 32) / 100;
    int box_h = (bounds.size.h * 20) / 100; 
    int y_bot = y_top + box_h + 4;
  #else
    bool is_tall = (bounds.size.h > 180);
    int y_top = is_tall ? 80 : 50;
    int box_h = is_tall ? 42 : 36; 
    int y_bot = y_top + box_h + 4;
  #endif
  
  int box_w = mid - 15;
  
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorIslamicGreen, is_dark_theme() ? GColorWhite : GColorBlack));
  graphics_fill_rect(ctx, GRect(10, y_top, box_w, box_h), 4, GCornersAll);
  
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, is_dark_theme() ? GColorWhite : GColorBlack));
  graphics_fill_rect(ctx, GRect(mid + 5, y_top, box_w, box_h), 4, GCornersAll);

  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorCobaltBlue, is_dark_theme() ? GColorWhite : GColorBlack));
  graphics_fill_rect(ctx, GRect(10, y_bot, box_w, box_h), 4, GCornersAll);

  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorOrange, is_dark_theme() ? GColorWhite : GColorBlack));
  graphics_fill_rect(ctx, GRect(mid + 5, y_bot, box_w, box_h), 4, GCornersAll);
}

static void summary_window_load(Window *window) {
  s_workout_active = false;
  persist_delete(ACTIVE_STATE_KEY);
  s_last_routine_slot = s_current_slot;
  persist_write_int(SETTINGS_KEY_BASE + 13, s_last_routine_slot);
  
  if (s_workout_sec > 0) {
      persist_write_int(GHOST_KEY_BASE + s_current_slot, s_workout_sec);
  }
  
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);
  
  #if defined(PBL_ROUND)
    int y_title = (bounds.size.h * 10) / 100;
    int y_top = (bounds.size.h * 32) / 100;
    int box_h = (bounds.size.h * 20) / 100; 
    int y_bot = y_top + box_h + 4;
    int y_info = (bounds.size.h * 78) / 100; 
  #else
    bool is_tall = (bounds.size.h > 180);
    int y_title = is_tall ? 15 : 2;
    int y_top = is_tall ? 80 : 46;
    int box_h = is_tall ? 42 : 36; 
    int y_bot = y_top + box_h + 4;
    int y_info = is_tall ? 190 : 126; 
  #endif

  int box_w = (bounds.size.w / 2) - 15;
  int mid = bounds.size.w / 2;

  s_sum_title_layer = build_text_layer(GRect(0, y_title, bounds.size.w, 40), FONT_KEY_GOTHIC_28_BOLD, get_text_color(), GTextAlignmentCenter, w_layer);
  
  s_summary_bg_layer = layer_create(bounds);
  layer_set_update_proc(s_summary_bg_layer, summary_bg_update_proc);
  layer_add_child(w_layer, s_summary_bg_layer);

  GColor grid_text_color = PBL_IF_COLOR_ELSE(GColorWhite, is_dark_theme() ? GColorBlack : GColorWhite);
  
  s_beat_layer = build_text_layer(GRect(10, y_top + 2, box_w, box_h), FONT_KEY_GOTHIC_14_BOLD, grid_text_color, GTextAlignmentCenter, w_layer);
  s_missed_layer = build_text_layer(GRect(mid + 5, y_top + 2, box_w, box_h), FONT_KEY_GOTHIC_14_BOLD, grid_text_color, GTextAlignmentCenter, w_layer);
  s_accuracy_layer = build_text_layer(GRect(10, y_bot + 2, box_w, box_h), FONT_KEY_GOTHIC_14_BOLD, grid_text_color, GTextAlignmentCenter, w_layer);
  s_density_layer = build_text_layer(GRect(mid + 5, y_bot + 2, box_w, box_h), FONT_KEY_GOTHIC_14_BOLD, grid_text_color, GTextAlignmentCenter, w_layer);

  text_layer_set_text_color(s_beat_layer, grid_text_color);
  text_layer_set_text_color(s_missed_layer, grid_text_color);
  text_layer_set_text_color(s_accuracy_layer, grid_text_color);
  text_layer_set_text_color(s_density_layer, grid_text_color);
  
  s_sum_info_layer = build_text_layer(GRect(0, y_info, bounds.size.w, bounds.size.h - y_info), FONT_KEY_GOTHIC_14, get_text_color(), GTextAlignmentCenter, w_layer);

  tick_timer_service_unsubscribe();
  int m = s_workout_sec / 60;
  int s = s_workout_sec % 60;
  static char title_buf[32];
  snprintf(title_buf, sizeof(title_buf), "Done! %02d:%02d", m, s);
  text_layer_set_text(s_sum_title_layer, title_buf);

  int sets_above = 0, sets_below = 0;
  int total_target_reps = 0, total_actual_reps = 0, total_volume = 0;

  for (int i = 0; i < s_total_exercises; i++) {
    int ex_misses = 0; 
    for (int j = 0; j < s_exercises[i].target_sets; j++) {
      int a_r = s_exercises[i].actual_reps[j];
      int a_w = s_exercises[i].actual_weight[j];
      int t_r = s_exercises[i].target_reps;
      int t_w = s_exercises[i].target_weight;
      
      if (s_exercises[i].modifier == 1 && ((j + 1) % 2 == 0)) {
          if (s_exercises[i].target_weight == 0) {
              t_r = (t_r * (100 - s_drop_set_pct)) / 100;
          } else {
              t_w = (t_w * (100 - s_drop_set_pct)) / 100;
          }
      }
      
      total_target_reps += t_r;
      total_actual_reps += a_r;
      total_volume += (a_r * a_w);
      
      if (a_w > t_w || (a_w == t_w && a_r > t_r)) sets_above++;
      else if (a_w < t_w || (a_w == t_w && a_r < t_r)) {
          sets_below++;
          ex_misses++; 
      }
    }
    
    // V6.1 FIX: Smart Bodyweight Progression!
    if (s_progression_mode != -1 && ex_misses == 0) {
      if (s_exercises[i].modifier == 6) {
          int increase = (s_exercises[i].target_reps * 5) / 100;
          if (increase < 1) increase = 1;
          s_exercises[i].target_reps += increase;
      } else if (s_progression_mode == 0) {
          if (s_exercises[i].target_weight > 0) {
              s_exercises[i].target_weight += s_weight_increment; // Add weight normally
          } else {
              s_exercises[i].target_reps += 1; // Adapt: Add reps if it's bodyweight!
          }
      } else if (s_progression_mode == 1) {
          s_exercises[i].target_reps += 1;
      }
    }
  }

  int accuracy = (total_target_reps > 0) ? ((total_actual_reps * 100) / total_target_reps) : 0;
  int density = (s_workout_sec > 0) ? ((total_volume * 60) / s_workout_sec) : 0;
  
  // V6.0 NEW: Milestone & Bodyweight Celebration Logic!
  static char celebration_buf[80]; // Increased to 80 to ensure text fits safely
  
  if (total_volume > 0) {
      int norm_v = (s_weight_unit_idx == 1) ? (total_volume * 45 / 100) : total_volume; // Normalize lbs to kg
      const char *milestone = "Great session!";
      if (norm_v >= 10000) milestone = "You lifted a Jet plane!";
      else if (norm_v >= 5000) milestone = "You lifted an Elephant!";
      else if (norm_v >= 2000) milestone = "You lifted a Rhino!";
      else if (norm_v >= 1000) milestone = "You lifted a Great White!";
      else if (norm_v >= 500)  milestone = "You lifted a Grizzly Bear!";
      else if (norm_v >= 250)  milestone = "You lifted a Motorcycle!";
      else if (norm_v >= 100)  milestone = "You lifted a Giant Panda!";
    
      snprintf(celebration_buf, sizeof(celebration_buf), "Synced! %s\nTotal Tonnage: %d %s", milestone, total_volume, s_weight_unit_idx == 0 ? "kg" : "lbs");
      
  } else if (total_actual_reps > 0) {
      // V6.0 FIX: Pure Bodyweight Workout Celebration!
      snprintf(celebration_buf, sizeof(celebration_buf), "Synced! Gravity defeated.\nBody moved %d times!", total_actual_reps);
      
  } else {
      // User skipped every single set
      snprintf(celebration_buf, sizeof(celebration_buf), "Data synced!\nPress any button.");
  }

  static char beat_buf[16], missed_buf[16], acc_buf[16], den_buf[16];
  snprintf(beat_buf, sizeof(beat_buf), "Beat\n%d", sets_above);
  snprintf(missed_buf, sizeof(missed_buf), "Miss\n%d", sets_below);
  snprintf(acc_buf, sizeof(acc_buf), "Acc\n%d%%", accuracy);
  snprintf(den_buf, sizeof(den_buf), "Dens\n%d", density);
  
  text_layer_set_text(s_beat_layer, beat_buf);
  text_layer_set_text(s_missed_layer, missed_buf);
  text_layer_set_text(s_accuracy_layer, acc_buf);
  text_layer_set_text(s_density_layer, den_buf);

  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  char date_buf[32];
  strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M", tick_time);

  int avg_hr = 0;
  if (s_hr_samples > 0) avg_hr = s_total_hr / s_hr_samples;

  char *export_buf = malloc(EXPORT_BUF_SIZE); 
  char *sync_buf = malloc(EXPORT_BUF_SIZE); 
  
  if (export_buf && sync_buf) {
      int limit = EXPORT_BUF_SIZE;
      int written = snprintf(export_buf, limit, "%s|%s|%d|%d|%d|%d|%d|%d", 
                             s_routine_name, date_buf, s_workout_sec, s_workout_sensation, accuracy, density, s_peak_hr, avg_hr);
      int offset = (written < limit) ? written : limit - 1;

      for(int i = 0; i < s_total_exercises; i++) {
        if (offset >= limit - 1) break; 
        const char *mod_label = "";
        if (s_exercises[i].modifier == 1) mod_label = " [DROP]";
        else if (s_exercises[i].modifier == 2) mod_label = " [SUPER]";
        else if (s_exercises[i].modifier == 6) mod_label = " [TIMED]";
        
        written = snprintf(export_buf + offset, limit - offset, "|%s%s", s_exercises[i].name, mod_label);
        offset += (written < limit - offset) ? written : limit - offset - 1;
        
        for(int j = 0; j < s_exercises[i].target_sets; j++) {
          if (offset >= limit - 1) break; 
          written = snprintf(export_buf + offset, limit - offset, "|%d|%d", s_exercises[i].actual_reps[j], s_exercises[i].actual_weight[j]);
          offset += (written < limit - offset) ? written : limit - offset - 1;
        }
      }
      
      int s_written = snprintf(sync_buf, limit, "%s|%d|%d", s_routine_name, s_progression_mode, s_weight_increment);
      int s_off = (s_written < limit) ? s_written : limit - 1;
    
      for (int i = 0; i < s_total_exercises; i++) {
        
        if (s_progression_mode != -1) {
            persist_write_data(ROUTINE_EX_BASE + (s_current_slot * MAX_EXERCISES) + i, &s_exercises[i], sizeof(Exercise));
        }
    
        int base_sets = s_exercises[i].target_sets;
        if (s_exercises[i].modifier == 1) {
            s_exercises[i].target_sets = base_sets / 2;
        }
    
        if (s_off < limit - 1) {
            s_written = snprintf(sync_buf + s_off, limit - s_off, "|%s|%d|%d|%d|%d|%s", 
                s_exercises[i].name, s_exercises[i].target_sets, s_exercises[i].target_reps, 
                s_exercises[i].target_weight, s_exercises[i].modifier, 
                s_exercises[i].comment[0] != '\0' ? s_exercises[i].comment : "-");
            s_off += (s_written < limit - s_off) ? s_written : limit - s_off - 1;
        }
        
        s_exercises[i].target_sets = base_sets;
      }
      
      DictionaryIterator *iter;
      if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
          dict_write_cstring(iter, MESSAGE_KEY_WORKOUT_SUMMARY, export_buf);
          dict_write_cstring(iter, MESSAGE_KEY_ROUTINE_DATA, sync_buf);
          app_message_outbox_send();
          text_layer_set_text(s_sum_info_layer, celebration_buf);
      } else {
          text_layer_set_text(s_sum_info_layer, "Sync Failed.\nCheck Bluetooth.");
      }
  } else {
      text_layer_set_text(s_sum_info_layer, "Memory Error.\nCannot sync.");
  }
  
  if (export_buf) free(export_buf);
  if (sync_buf) free(sync_buf);
}

static void summary_window_unload(Window *window) {
  text_layer_destroy(s_sum_title_layer);
  layer_destroy(s_summary_bg_layer);
  text_layer_destroy(s_beat_layer);
  text_layer_destroy(s_missed_layer);
  text_layer_destroy(s_accuracy_layer);
  text_layer_destroy(s_density_layer);
  text_layer_destroy(s_sum_info_layer);
}

// --- VARIATION WINDOW LOGIC ---
static uint16_t variation_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return NUM_VARIATIONS;
}

static void variation_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  menu_cell_basic_draw(ctx, cell_layer, s_variations[cell_index->row], NULL, NULL);
}

static void variation_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  Exercise *ex = &s_exercises[s_curr_ex_idx];
  const char *var_str = s_variations[cell_index->row];
  
  char base_name[32];
  strncpy(base_name, ex->name, sizeof(base_name));
  base_name[sizeof(base_name) - 1] = '\0';
  
  for (int i = 1; i < NUM_VARIATIONS; i++) { 
    char *match = strstr(base_name, s_variations[i]);
    if (match && match == base_name + strlen(base_name) - strlen(s_variations[i])) {
      if (match > base_name && *(match - 1) == ' ') *(match - 1) = '\0';
      else *match = '\0';
      break; 
    }
  }
  
  if (cell_index->row == 0) {
    strncpy(ex->name, base_name, sizeof(ex->name) - 1);
  } else {
    char big_temp[64];
    snprintf(big_temp, sizeof(big_temp), "%s %s", base_name, var_str);
    strncpy(s_exercises[s_curr_ex_idx].name, big_temp, sizeof(s_exercises[s_curr_ex_idx].name) - 1);
    s_exercises[s_curr_ex_idx].name[sizeof(s_exercises[s_curr_ex_idx].name) - 1] = '\0';
  }
  
  update_workout_ui(false);
  window_stack_pop(true);
}

static void variation_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);
  
  s_variation_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_variation_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = variation_get_num_rows_callback,
    .draw_row = variation_draw_row_callback,
    .select_click = variation_select_callback,
  });
  
  menu_layer_set_normal_colors(s_variation_menu_layer, get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(s_variation_menu_layer, get_theme_color(), get_bg_color());
  menu_layer_set_click_config_onto_window(s_variation_menu_layer, window);
  layer_add_child(w_layer, menu_layer_get_layer(s_variation_menu_layer));
}

static void variation_window_unload(Window *window) {
  menu_layer_destroy(s_variation_menu_layer);
}

static void push_variation_window() {
  if(!s_variation_window) {
    s_variation_window = window_create();
    window_set_window_handlers(s_variation_window, (WindowHandlers) { .load = variation_window_load, .unload = variation_window_unload });
  }
  window_set_background_color(s_variation_window, get_bg_color());
  window_stack_push(s_variation_window, true);
}

// --- WORKOUT WINDOW LOGIC ---
static int get_completed_sets() {
  int completed = 0;
  for (int i = 0; i < s_total_exercises; i++) {
    if (i < s_curr_ex_idx) {
      if (s_exercises[i].modifier == 2 && i == s_curr_ex_idx - 1) completed += s_exercises[i].current_set; 
      else completed += s_exercises[i].target_sets; 
    } else if (i == s_curr_ex_idx) {
      completed += (s_exercises[i].current_set - 1); 
    } else if (i > s_curr_ex_idx) {
      if (i == s_curr_ex_idx + 1 && s_exercises[s_curr_ex_idx].modifier == 2) completed += (s_exercises[i].current_set - 1); 
    }
  }
  return completed;
}

static void progress_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  if (s_total_workout_sets > 0) {
    int completed = get_completed_sets();
    
    GColor track_color = is_dark_theme() ? PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack) : PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite);
    
    #if defined(PBL_ROUND)
      int angle_end = TRIG_MAX_ANGLE * completed / s_total_workout_sets;
      int inset_margin = (bounds.size.h <= 180) ? 0 : 4;
      GRect inset_bounds = grect_inset(bounds, GEdgeInsets(inset_margin)); 
      
      graphics_context_set_fill_color(ctx, track_color);
      graphics_fill_radial(ctx, inset_bounds, GOvalScaleModeFitCircle, 6, 0, TRIG_MAX_ANGLE);
      
      graphics_context_set_fill_color(ctx, get_theme_color());
      graphics_fill_radial(ctx, inset_bounds, GOvalScaleModeFitCircle, 6, 0, angle_end);
    #else
      graphics_context_set_fill_color(ctx, track_color);
      graphics_fill_rect(ctx, bounds, 0, GCornerNone);
      
      int width = (bounds.size.w * completed) / s_total_workout_sets;
      graphics_context_set_fill_color(ctx, get_theme_color());
      graphics_fill_rect(ctx, GRect(0, 0, width, bounds.size.h), 0, GCornerNone);
    #endif
  }
}

static void workout_bg_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_stroke_color(ctx, is_dark_theme() ? PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite) : PBL_IF_COLOR_ELSE(GColorLightGray, GColorDarkGray));
  graphics_context_set_stroke_width(ctx, 2); 
  graphics_draw_line(ctx, GPoint(0, s_line1_y), GPoint(bounds.size.w, s_line1_y));
  graphics_draw_line(ctx, GPoint(0, s_line2_y), GPoint(bounds.size.w, s_line2_y));
}

static void rest_bg_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, get_bg_color()); 
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  
  graphics_context_set_stroke_color(ctx, is_dark_theme() ? PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite) : PBL_IF_COLOR_ELSE(GColorLightGray, GColorDarkGray)); 
  graphics_context_set_stroke_width(ctx, 2); 
  graphics_draw_line(ctx, GPoint(0, 0), GPoint(bounds.size.w, 0));
}

static void highlight_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_stroke_color(ctx, get_theme_color());
  graphics_context_set_stroke_width(ctx, 4); 
  graphics_draw_round_rect(ctx, grect_inset(bounds, GEdgeInsets(2)), 8);
}

static void box_anim_stopped_handler(Animation *animation, bool finished, void *context) {
  s_box_anim = NULL;
}

static void animate_highlight_box(bool animated) {
  Layer *w_layer = window_get_root_layer(s_workout_window);
  GRect bounds = layer_get_bounds(w_layer);
  int half_w = bounds.size.w / 2;
  bool is_tall = (bounds.size.h > 180); 
  
  int box_y_coord = s_labels_y - (is_tall ? 4 : 2);
  int box_height = (s_target_y + 22) - box_y_coord + (is_tall ? 4 : 2);
  int offset_x = 0;
  
  #if defined(PBL_ROUND)
    if (bounds.size.h <= 180) { 
      offset_x = 10; box_y_coord = s_labels_y + 1; box_height = (s_target_y + 25) - box_y_coord; 
    } else { offset_x = 20; }
  #endif

  bool is_bw = (s_exercises[s_curr_ex_idx].target_weight == 0);
  int center_x;
  
  if (is_bw) {
      center_x = bounds.size.w / 2;
  } else {
      center_x = (s_edit_mode == 0) ? ((half_w / 2) + offset_x) : ((half_w + (half_w / 2)) - offset_x);
  }

  int layer_w = s_highlight_box_width + 8; 
  GRect target_rect = GRect(center_x - (layer_w / 2), box_y_coord, layer_w, box_height);
  
  if (s_box_anim) {
      animation_unschedule(s_box_anim);
      s_box_anim = NULL;
  }

  if (animated) {
    GRect current_rect = layer_get_frame(s_highlight_layer);
    int current_x = current_rect.origin.x;
    int target_x = target_rect.origin.x;
    
    if (current_x != target_x && current_rect.size.w > 0) {
        int direction = (target_x > current_x) ? 1 : -1;
        
        GRect overshoot_rect = target_rect;
        overshoot_rect.origin.x += (direction * 8); 
        
        PropertyAnimation *anim1 = property_animation_create_layer_frame(s_highlight_layer, &current_rect, &overshoot_rect);
        Animation *a1 = property_animation_get_animation(anim1);
        animation_set_duration(a1, 150);
        animation_set_curve(a1, AnimationCurveEaseOut);
        
        PropertyAnimation *anim2 = property_animation_create_layer_frame(s_highlight_layer, &overshoot_rect, &target_rect);
        Animation *a2 = property_animation_get_animation(anim2);
        animation_set_duration(a2, 100);
        animation_set_curve(a2, AnimationCurveEaseInOut);
        
        s_box_anim = animation_sequence_create(a1, a2, NULL);
    } else {
        PropertyAnimation *anim1 = property_animation_create_layer_frame(s_highlight_layer, NULL, &target_rect);
        s_box_anim = property_animation_get_animation(anim1);
        animation_set_duration(s_box_anim, 200);
        animation_set_curve(s_box_anim, AnimationCurveEaseOut);
    }
    
    animation_set_handlers(s_box_anim, (AnimationHandlers) {
        .stopped = box_anim_stopped_handler
    }, NULL);
    
    animation_schedule(s_box_anim);
  } else {
    layer_set_frame(s_highlight_layer, target_rect);
  }
}

static void rest_anim_stopped_handler(Animation *animation, bool finished, void *context) {
  s_rest_anim = NULL;
}

static void set_rest_overlay_state(bool is_resting, bool animated) {
  Layer *w_layer = window_get_root_layer(s_workout_window);
  GRect bounds = layer_get_bounds(w_layer);
  int rest_box_height = s_line2_y - s_line1_y;
  
  GRect on_screen = GRect(0, s_line1_y, bounds.size.w, rest_box_height);
  GRect off_screen = GRect(0, bounds.size.h, bounds.size.w, rest_box_height); 
  
  GRect target_rect = is_resting ? on_screen : off_screen;
  
  if (animated) {
    if (is_resting) layer_set_hidden(s_rest_overlay_layer, false); 
    
    if (s_rest_anim) {
        animation_unschedule(s_rest_anim);
        s_rest_anim = NULL;
    }
    
    PropertyAnimation *anim = property_animation_create_layer_frame(s_rest_overlay_layer, NULL, &target_rect);
    s_rest_anim = property_animation_get_animation(anim);
    
    animation_set_duration(s_rest_anim, 300);
    animation_set_curve(s_rest_anim, AnimationCurveEaseInOut);
    animation_set_handlers(s_rest_anim, (AnimationHandlers) {
        .stopped = rest_anim_stopped_handler
    }, NULL);
    
    animation_schedule(s_rest_anim);
  } else {
    layer_set_frame(s_rest_overlay_layer, target_rect);
    if (!is_resting) layer_set_hidden(s_rest_overlay_layer, true);
  }
}

static void update_workout_ui(bool animate_box) { 
  Exercise *ex = &s_exercises[s_curr_ex_idx];
  
  Layer *w_layer = window_get_root_layer(s_workout_window);
  GRect bounds = layer_get_bounds(w_layer);
  bool is_tall = (bounds.size.h > 180); 
  
  int name_len = strlen(ex->name);
  
  if (is_tall) {
    if (name_len > 14) text_layer_set_font(s_exercise_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    else if (name_len > 10) text_layer_set_font(s_exercise_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    else text_layer_set_font(s_exercise_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  } else {
    if (name_len > 10) text_layer_set_font(s_exercise_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    else text_layer_set_font(s_exercise_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  }
  
  text_layer_set_text(s_exercise_layer, ex->name);

  static char next_buf[64];
  bool is_second_half = (s_curr_ex_idx > 0 && s_exercises[s_curr_ex_idx - 1].modifier == 2);
  
  if (is_second_half && ex->current_set < ex->target_sets) {
      snprintf(next_buf, sizeof(next_buf), "NEXT: %s", s_exercises[s_curr_ex_idx - 1].name);
  } else if (s_curr_ex_idx + 1 < s_total_exercises) {
      snprintf(next_buf, sizeof(next_buf), "NEXT: %s", s_exercises[s_curr_ex_idx + 1].name);
  } else {
      snprintf(next_buf, sizeof(next_buf), "NEXT: FINISH!");
  }
  text_layer_set_text(s_next_exercise_layer, next_buf);

  int active_target_weight = ex->target_weight;
  int active_target_reps = ex->target_reps; // V6.1 FIX: Track reps dynamically
  static char set_buf[32];

  if (ex->modifier == 1 && (ex->current_set % 2 == 0)) {
      if (ex->target_weight == 0) {
          active_target_reps = (active_target_reps * (100 - s_drop_set_pct)) / 100;
      } else {
          active_target_weight = (active_target_weight * (100 - s_drop_set_pct)) / 100;
      }
      snprintf(set_buf, sizeof(set_buf), "Set %d of %d (DROP)", ex->current_set, ex->target_sets);
  } else if (ex->modifier == 3) {
      snprintf(set_buf, sizeof(set_buf), "Set %d of %d (WARM)", ex->current_set, ex->target_sets);
  } else {
      snprintf(set_buf, sizeof(set_buf), "Set %d of %d", ex->current_set, ex->target_sets);
  }
  text_layer_set_text(s_set_layer, set_buf);

  if (ex->modifier == 6) {
      text_layer_set_text(s_label_reps_layer, "Secs");
  } else {
      text_layer_set_text(s_label_reps_layer, "Reps");
  }

  static char t_reps_buf[32], t_weight_buf[32], reps_buf[16], weight_buf[16];
  snprintf(t_reps_buf, sizeof(t_reps_buf), "Target: %d", active_target_reps);
  snprintf(reps_buf, sizeof(reps_buf), "%d", s_temp_reps);

  if (s_weight_unit_idx == 0) text_layer_set_text(s_label_weight_layer, "Weight (kg)");
  else text_layer_set_text(s_label_weight_layer, "Weight (lbs)");
  
  snprintf(t_weight_buf, sizeof(t_weight_buf), "Target: %d", active_target_weight); 
  snprintf(weight_buf, sizeof(weight_buf), "%d", s_temp_weight);

  text_layer_set_text(s_target_reps_layer, t_reps_buf);
  text_layer_set_text(s_target_weight_layer, t_weight_buf);
  text_layer_set_text(s_actual_reps_layer, reps_buf);
  text_layer_set_text(s_actual_weight_layer, weight_buf);

  bool is_bw = (ex->target_weight == 0); // V6.1 FIX: Center UI if weight is 0
  layer_set_hidden(text_layer_get_layer(s_label_weight_layer), is_bw);
  layer_set_hidden(text_layer_get_layer(s_actual_weight_layer), is_bw);
  layer_set_hidden(text_layer_get_layer(s_target_weight_layer), is_bw);

  int offset_x = 0;
  #if defined(PBL_ROUND)
    offset_x = (bounds.size.h <= 180) ? 10 : 20;
  #endif
  
  int half_w = bounds.size.w / 2;

  if (is_bw) {
      layer_set_frame(text_layer_get_layer(s_label_reps_layer), GRect(0, s_labels_y, bounds.size.w, 20));
      layer_set_frame(text_layer_get_layer(s_actual_reps_layer), GRect(0, s_actual_y, bounds.size.w, 40));
      layer_set_frame(text_layer_get_layer(s_target_reps_layer), GRect(0, s_target_y, bounds.size.w, 22));
  } else {
      layer_set_frame(text_layer_get_layer(s_label_reps_layer), GRect(offset_x, s_labels_y, half_w, 20));
      layer_set_frame(text_layer_get_layer(s_actual_reps_layer), GRect(offset_x, s_actual_y, half_w, 40));
      layer_set_frame(text_layer_get_layer(s_target_reps_layer), GRect(offset_x, s_target_y, half_w, 22));
  }

  GColor active_color = get_theme_color();
  GColor inactive_color = is_dark_theme() ? PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite) : PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack);

  if (s_edit_mode == 0) { 
    text_layer_set_text_color(s_actual_reps_layer, active_color); 
    text_layer_set_text_color(s_actual_weight_layer, inactive_color);
  } else { 
    text_layer_set_text_color(s_actual_reps_layer, inactive_color);
    text_layer_set_text_color(s_actual_weight_layer, active_color);
  }

  const char *label_str = (s_edit_mode == 0) ? "Reps" : ((s_weight_unit_idx == 0) ? "Weight (kg)" : "Weight (lbs)");
  const char *actual_str = (s_edit_mode == 0) ? reps_buf : weight_buf;
  const char *target_str = (s_edit_mode == 0) ? t_reps_buf : t_weight_buf;
  
  GRect measure_rect = GRect(0, 0, bounds.size.w, 40);
  GSize s1 = graphics_text_layout_get_content_size(label_str, fonts_get_system_font(FONT_KEY_GOTHIC_14), measure_rect, GTextOverflowModeFill, GTextAlignmentCenter);
  GSize s2 = graphics_text_layout_get_content_size(actual_str, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK), measure_rect, GTextOverflowModeFill, GTextAlignmentCenter);
  GSize s3 = graphics_text_layout_get_content_size(target_str, fonts_get_system_font(FONT_KEY_GOTHIC_18), measure_rect, GTextOverflowModeFill, GTextAlignmentCenter);
  
  int max_w = s1.w;
  if (s2.w > max_w) max_w = s2.w;
  if (s3.w > max_w) max_w = s3.w;
  s_highlight_box_width = max_w + 16; 
  
  if (s_highlight_box_width > half_w - 4) s_highlight_box_width = half_w - 4; 
  
  text_layer_set_text_color(s_rest_time_layer, active_color);
  layer_mark_dirty(s_progress_layer);
  
  animate_highlight_box(animate_box);
}

static void skip_rest() {
  s_is_resting = false;
  set_rest_overlay_state(false, true);
  update_workout_ui(false);
}

static void init_temp_values(Exercise *ex) {
  s_is_timed_active = false;
  if (ex->modifier == 6) {
    s_temp_reps = 0;
  } else {
    s_temp_reps = ex->target_reps;
    if (ex->modifier == 1 && (ex->current_set % 2 == 0) && ex->target_weight == 0) {
      s_temp_reps = ex->actual_reps[ex->current_set - 2];
      s_temp_reps = (s_temp_reps * (100 - s_drop_set_pct)) / 100;
    }
  }

  int active_weight = ex->target_weight;
  if (ex->modifier == 1 && (ex->current_set % 2 == 0) && ex->target_weight != 0) {
      active_weight = ex->actual_weight[ex->current_set - 2];
      active_weight = (active_weight * (100 - s_drop_set_pct)) / 100;
  }
  s_temp_weight = active_weight;
}

static void swap_exercise() {
  if (s_is_resting) return;
  if (s_curr_ex_idx + 1 >= s_total_exercises) return;

  if (s_exercises[s_curr_ex_idx].modifier == 2 || (s_curr_ex_idx > 0 && s_exercises[s_curr_ex_idx - 1].modifier == 2)) {
      vibes_double_pulse(); 
      return; 
  }

  Exercise temp = s_exercises[s_curr_ex_idx];
  int next_idx = s_curr_ex_idx + 1;
  s_exercises[s_curr_ex_idx] = s_exercises[next_idx];
  s_exercises[next_idx] = temp;

  Exercise *new_ex = &s_exercises[s_curr_ex_idx];
  init_temp_values(new_ex);
  
  update_workout_ui(true);
}

static void perform_true_skip() {
  Exercise *ex = &s_exercises[s_curr_ex_idx];
  bool is_first_half = (ex->modifier == 2 && s_curr_ex_idx + 1 < s_total_exercises);
  bool is_second_half = (s_curr_ex_idx > 0 && s_exercises[s_curr_ex_idx - 1].modifier == 2);
  
  for (int i = ex->current_set - 1; i < ex->target_sets; i++) {
      ex->actual_reps[i] = 0;
      ex->actual_weight[i] = 0;
  }
  ex->current_set = ex->target_sets;

  if (is_first_half) {
      Exercise *next_ex = &s_exercises[s_curr_ex_idx + 1];
      for (int i = next_ex->current_set - 1; i < next_ex->target_sets; i++) {
          next_ex->actual_reps[i] = 0;
          next_ex->actual_weight[i] = 0;
      }
      next_ex->current_set = next_ex->target_sets;
      s_curr_ex_idx++;
  } else if (is_second_half) {
      Exercise *prev_ex = &s_exercises[s_curr_ex_idx - 1];
      for (int i = prev_ex->current_set - 1; i < prev_ex->target_sets; i++) {
          prev_ex->actual_reps[i] = 0;
          prev_ex->actual_weight[i] = 0;
      }
      prev_ex->current_set = prev_ex->target_sets;
  }

  if (s_curr_ex_idx + 1 < s_total_exercises) {
     s_curr_ex_idx++;
     
     Exercise *new_ex = &s_exercises[s_curr_ex_idx];
     init_temp_values(new_ex);
     
     s_is_resting = false;
     layer_set_hidden(s_rest_overlay_layer, true);
     
     update_workout_ui(true);
  } else {
     vibes_double_pulse();
     if (s_enable_sensation) push_sensation_window();
     else push_summary_window();
     window_stack_remove(s_workout_window, false);
  }
}

static void perform_finish_set() {
  if (s_is_resting) { skip_rest(); return; }

  Exercise *ex = &s_exercises[s_curr_ex_idx];
  ex->actual_reps[ex->current_set - 1] = s_temp_reps;
  ex->actual_weight[ex->current_set - 1] = s_temp_weight;

  bool is_first_half = (ex->modifier == 2 && s_curr_ex_idx + 1 < s_total_exercises);
  bool is_second_half = (s_curr_ex_idx > 0 && s_exercises[s_curr_ex_idx - 1].modifier == 2);

  if (is_first_half) {
    s_curr_ex_idx++;
    s_exercises[s_curr_ex_idx].current_set = ex->current_set; 
    s_rest_seconds_remaining = s_super_rest_sec;
    play_vibe(s_set_vibe);
    
  } else if (is_second_half) {
    if (ex->current_set < ex->target_sets) {
      ex->current_set++;
      s_exercises[s_curr_ex_idx - 1].current_set++; 
      s_curr_ex_idx--; 
      s_rest_seconds_remaining = s_set_rest_sec; 
      play_vibe(s_set_vibe);
    } else {
      s_curr_ex_idx++; 
      if (s_curr_ex_idx < s_total_exercises) {
        s_exercises[s_curr_ex_idx].current_set = 1;
        s_rest_seconds_remaining = s_ex_rest_sec; 
        play_vibe(s_ex_vibe);
      } else {
        vibes_double_pulse();
        if (s_enable_sensation) push_sensation_window();
        else push_summary_window();
        window_stack_remove(s_workout_window, false);
        return;
      }
    }
    
  } else {
    if (ex->current_set < ex->target_sets) {
      ex->current_set++;
      
      if (ex->modifier == 1 && (ex->current_set % 2 == 0)) {
          s_rest_seconds_remaining = s_drop_rest_sec;
      } else if (ex->modifier == 3) {
          s_rest_seconds_remaining = s_set_rest_sec / 2; 
      } else {
          s_rest_seconds_remaining = s_set_rest_sec; 
      }
      
      play_vibe(s_set_vibe);
    } else {
      s_curr_ex_idx++;
      if (s_curr_ex_idx < s_total_exercises) {
        s_exercises[s_curr_ex_idx].current_set = 1;
        s_rest_seconds_remaining = s_ex_rest_sec; 
        play_vibe(s_ex_vibe);
      } else {
        vibes_double_pulse();
        if (s_enable_sensation) push_sensation_window();
        else push_summary_window();
        window_stack_remove(s_workout_window, false);
        return;
      }
    }
  }

  Exercise *next_ex = &s_exercises[s_curr_ex_idx]; 
  init_temp_values(next_ex);
  
  s_edit_mode = 0; 
  
  if (s_rest_seconds_remaining > 0) {
    s_is_resting = true;
    set_rest_overlay_state(true, true); 
  }
  update_workout_ui(false); 
}

static void perform_skip_set() {
  if (s_is_resting) { skip_rest(); return; }
  
  s_temp_reps = 0;
  s_temp_weight = 0;
  
  perform_finish_set(); 
  
  if (s_is_resting) {
      skip_rest();
  }
}

// --- VOICE DICTATION LOGIC ---
static void dictation_session_callback(DictationSession *session, DictationSessionStatus status, char *transcription, void *context) {
  if (status == DictationSessionStatusSuccess) {
    snprintf(s_exercises[s_curr_ex_idx].comment, sizeof(s_exercises[0].comment), "%s", transcription);
    vibes_short_pulse(); 
  }
}

static void perform_voice_note() {
  if (!s_dictation_session) {
    s_dictation_session = dictation_session_create(sizeof(s_exercises[0].comment), dictation_session_callback, NULL);
  }
  dictation_session_start(s_dictation_session);
}

static void execute_shortcut(int action_idx) {
  if (s_is_resting && action_idx != 4 && action_idx != 5 && action_idx != 6) return;
  switch (action_idx) {
    case 0: push_variation_window(); break;
    case 1: 
      if (s_exercises[s_curr_ex_idx].comment[0] != '\0') window_stack_push(s_note_window, true);
      else vibes_short_pulse(); 
      break;
    case 2: swap_exercise(); break;
    case 3: perform_true_skip(); break;
    case 4: perform_finish_set(); break;
    case 5: perform_skip_set(); break;
    case 6: perform_voice_note(); break; 
  }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (!s_is_paused) {
      s_workout_sec++;
      
      if (s_is_timed_active) {
          s_temp_reps++;
          if (s_temp_reps == s_exercises[s_curr_ex_idx].target_reps) {
              vibes_long_pulse();
          }
          update_workout_ui(false);
      }
  }
  int m = s_workout_sec / 60;
  int s = s_workout_sec % 60;
  static char time_buf[16];
  snprintf(time_buf, sizeof(time_buf), "%02d:%02d", m, s);
  text_layer_set_text(s_timer_layer, time_buf);

  // V6.0 NEW: Peek at the Heart Rate sensor every single second!
  HealthValue current_hr = 0;
  #if defined(PBL_HEALTH)
    HealthServiceAccessibilityMask hr_mask = health_service_metric_accessible(HealthMetricHeartRateBPM, time(NULL), time(NULL));
    if (hr_mask & HealthServiceAccessibilityMaskAvailable) {
        current_hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
    }
  #endif

  // Only run the clock update and average calculation once a minute
  static int s_last_minute = -1;
  if (tick_time->tm_min != s_last_minute) {
    static char clock_buf[16];
    if(clock_is_24h_style()) strftime(clock_buf, sizeof(clock_buf), "%H:%M", tick_time);
    else strftime(clock_buf, sizeof(clock_buf), "%I:%M", tick_time);
    text_layer_set_text(s_clock_layer, clock_buf);
    s_last_minute = tick_time->tm_min;
    
    if (current_hr > 0) {
        if (current_hr > s_peak_hr) s_peak_hr = current_hr;
        s_total_hr += current_hr;
        s_hr_samples++;
    }
  }

  // V6.0 BONUS: Live 1-second UI updates for rectangle watches!
  #if !defined(PBL_ROUND)
  #if defined(PBL_HEALTH)
    
    if (hr_mask & HealthServiceAccessibilityMaskNotSupported) {
        text_layer_set_text(s_hr_layer, ""); // Blank out the corner for Pebble Time / Original Pebble
    } else {
        static char hr_buf[16];
        if (current_hr > 0) { 
            snprintf(hr_buf, sizeof(hr_buf), "%lu BPM", (uint32_t)current_hr);
        } else {
            snprintf(hr_buf, sizeof(hr_buf), "-- BPM"); // Show this on Pebble 2 / Emery while searching for pulse
        }
        text_layer_set_text(s_hr_layer, hr_buf);
    }
    
  #endif
  #endif

  if (s_is_resting) {
    // V6.0 NEW: Live Ghost Pacer!
    if (s_enable_ghost && s_last_workout_sec > 0 && s_total_workout_sets > 0) {
        int expected_sec = (s_last_workout_sec * get_completed_sets()) / s_total_workout_sets;
        int diff = s_workout_sec - expected_sec;
        
        static char ghost_buf[32];
        int abs_diff = diff < 0 ? -diff : diff;
        int dm = abs_diff / 60;
        int ds = abs_diff % 60;
        
        if (diff <= 0) {
            snprintf(ghost_buf, sizeof(ghost_buf), "Ahead -%02d:%02d", dm, ds);
            text_layer_set_text_color(s_rest_title_layer, PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorBlack));
        } else {
            snprintf(ghost_buf, sizeof(ghost_buf), "Behind +%02d:%02d", dm, ds);
            text_layer_set_text_color(s_rest_title_layer, PBL_IF_COLOR_ELSE(GColorRed, GColorBlack));
        }
        text_layer_set_text(s_rest_title_layer, ghost_buf);
    } else {
        text_layer_set_text_color(s_rest_title_layer, GColorBlack);
        text_layer_set_text(s_rest_title_layer, "REST");
    }

    if (!s_is_paused) {
        s_rest_seconds_remaining--;
    }
    
    bool rest_finished = false;
    
    // Check if the timer hit zero OR if our heart rate is fully recovered!
    if (s_rest_seconds_remaining <= 0) {
        rest_finished = true;
    } else if (s_dynamic_hr_target > 0 && current_hr > 0 && current_hr <= (HealthValue)s_dynamic_hr_target) {
        rest_finished = true; // Heart rate dropped to target, skip the rest!
    }
    
    if (rest_finished) {
      skip_rest();
      play_vibe(s_rest_vibe); 
    } else {
      static char rest_buf[16];
      static char skip_buf[32];
      
      // Swap the UI depending on the mode!
      if (s_dynamic_hr_target > 0) {
          if (current_hr > 0) snprintf(rest_buf, sizeof(rest_buf), "%lu", (uint32_t)current_hr);
          else snprintf(rest_buf, sizeof(rest_buf), "--");
          
          snprintf(skip_buf, sizeof(skip_buf), "Target: %d | Max: %ds", s_dynamic_hr_target, s_rest_seconds_remaining);
      } else {
          snprintf(rest_buf, sizeof(rest_buf), "%d", s_rest_seconds_remaining);
          snprintf(skip_buf, sizeof(skip_buf), "[Select] to Skip");
      }
      
      text_layer_set_text(s_rest_time_layer, rest_buf);
      text_layer_set_text(s_rest_skip_layer, skip_buf);
    }
  }
}

// --- NOTE WINDOW LOGIC ---
static void note_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_note_text_layer = text_layer_create(GRect(5, 30, bounds.size.w - 10, bounds.size.h - 30));
  text_layer_set_font(s_note_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_note_text_layer, GTextAlignmentCenter);
  text_layer_set_text(s_note_text_layer, s_exercises[s_curr_ex_idx].comment);
  
  text_layer_set_background_color(s_note_text_layer, GColorClear);
  text_layer_set_text_color(s_note_text_layer, PBL_IF_COLOR_ELSE(is_dark_theme() ? GColorWhite : GColorBlack, GColorBlack));
  window_set_background_color(window, PBL_IF_COLOR_ELSE(is_dark_theme() ? GColorBlack : GColorWhite, GColorWhite));
  
  layer_add_child(window_layer, text_layer_get_layer(s_note_text_layer));
}

static void note_window_unload(Window *window) {
  text_layer_destroy(s_note_text_layer);
}

// --- MEGA MENU LOGIC ---
static void menu_var_callback(int index, void *ctx) {
  push_variation_window(); 
}

static void menu_note_callback(int index, void *ctx) {
  if (s_exercises[s_curr_ex_idx].comment[0] != '\0') {
    window_stack_push(s_note_window, true);
  } else {
    vibes_short_pulse(); 
  }
}

static void menu_swap_callback(int index, void *ctx) {
  window_stack_remove(s_mega_window, false);
  swap_exercise(); 
}

static void menu_skip_callback(int index, void *ctx) {
  window_stack_remove(s_mega_window, false);
  perform_true_skip();
}

static void menu_finish_callback(int index, void *ctx) {
  window_stack_remove(s_mega_window, false);
  perform_finish_set();
}

static void menu_skip_set_callback(int index, void *ctx) {
  window_stack_remove(s_mega_window, false);
  perform_skip_set();
}

static void menu_voice_callback(int index, void *ctx) {
  window_stack_remove(s_mega_window, false);
  perform_voice_note();
}

static void mega_window_load(Window *window) {
  int num_items = 0;
  
  s_mega_menu_items[num_items++] = (SimpleMenuItem) {
    .title = "Finish Set",
    .subtitle = "Log & progress workout",
    .callback = menu_finish_callback,
  };
  
  s_mega_menu_items[num_items++] = (SimpleMenuItem) {
    .title = "View Note",
    .subtitle = s_exercises[s_curr_ex_idx].comment[0] != '\0' ? s_exercises[s_curr_ex_idx].comment : "No note attached",
    .callback = menu_note_callback,
  };
  
  s_mega_menu_items[num_items++] = (SimpleMenuItem) {
    .title = "Record Voice Note",
    .subtitle = "Dictate note for this exercise",
    .callback = menu_voice_callback,
  };

  s_mega_menu_items[num_items++] = (SimpleMenuItem) {
    .title = "Variations",
    .subtitle = "Add a variation",
    .callback = menu_var_callback,
  };

  s_mega_menu_items[num_items++] = (SimpleMenuItem) {
    .title = "Do Later (Swap)",
    .subtitle = "Swap with next exercise",
    .callback = menu_swap_callback,
  };
    
  s_mega_menu_items[num_items++] = (SimpleMenuItem) {
    .title = "Skip Set (Log 0)",
    .subtitle = "Progress to next set",
    .callback = menu_skip_set_callback,
  };
  
  s_mega_menu_items[num_items++] = (SimpleMenuItem) {
    .title = "Skip Exercise (Log 0)",
    .subtitle = "Progress to next exercise",
    .callback = menu_skip_callback,
  };

  s_mega_menu_sections[0] = (SimpleMenuSection) {
    .num_items = num_items,
    .items = s_mega_menu_items,
  };

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

  s_mega_menu_layer = simple_menu_layer_create(bounds, window, s_mega_menu_sections, 1, NULL);

  MenuLayer *internal_menu = simple_menu_layer_get_menu_layer(s_mega_menu_layer);
  menu_layer_set_normal_colors(internal_menu, get_bg_color(), get_text_color()); 
  menu_layer_set_highlight_colors(internal_menu, get_theme_color(), get_bg_color());

  layer_add_child(window_layer, simple_menu_layer_get_layer(s_mega_menu_layer));
}

static void mega_window_unload(Window *window) {
  simple_menu_layer_destroy(s_mega_menu_layer);
}

static void wo_up_click(ClickRecognizerRef recognizer, void *context) {
  if (s_is_resting) return; 
  if (s_edit_mode == 0) s_temp_reps++; else s_temp_weight++;
  update_workout_ui(false);
}

static void wo_down_click(ClickRecognizerRef recognizer, void *context) {
  if (s_is_resting) return;
  if (s_edit_mode == 0 && s_temp_reps > 0) s_temp_reps--; 
  else if (s_edit_mode == 1 && s_temp_weight > 0) s_temp_weight--;
  update_workout_ui(false);
}

static void wo_up_long_click(ClickRecognizerRef recognizer, void *context) {
  execute_shortcut(s_shortcut_up);
}

static void wo_down_long_click(ClickRecognizerRef recognizer, void *context) {
  execute_shortcut(s_shortcut_down);
}

static void wo_select_short_click(ClickRecognizerRef recognizer, void *context) {
  if (s_is_resting) { skip_rest(); return; }
  
  if (s_exercises[s_curr_ex_idx].modifier == 6) {
      s_is_timed_active = !s_is_timed_active;
  } else if (s_exercises[s_curr_ex_idx].target_weight == 0) {
      s_edit_mode = 0;
  } else {
      s_edit_mode = !s_edit_mode; 
  }
  update_workout_ui(true);
}

static void wo_select_long_click(ClickRecognizerRef recognizer, void *context) {
  execute_shortcut(s_shortcut_select);
}

static void wo_select_double_click(ClickRecognizerRef recognizer, void *context) {
  if (s_is_resting) return;
  window_set_background_color(s_mega_window, get_bg_color()); 
  
  window_stack_push(s_mega_window, true);
}

static void wo_back_click(ClickRecognizerRef recognizer, void *context) {
  s_is_paused = true;
  push_exit_window();
}

static void wo_click_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, wo_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, wo_down_click);
  window_long_click_subscribe(BUTTON_ID_UP, s_long_press_ms, wo_up_long_click, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, s_long_press_ms, wo_down_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_SELECT, wo_select_short_click);
  window_multi_click_subscribe(BUTTON_ID_SELECT, 2, 2, 300, true, wo_select_double_click); 
  window_long_click_subscribe(BUTTON_ID_SELECT, s_long_press_ms, wo_select_long_click, NULL); 
  window_single_click_subscribe(BUTTON_ID_BACK, wo_back_click);
}

static void workout_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);
  int half_w = bounds.size.w / 2;
  bool is_tall = (bounds.size.h > 180); 

  int top_margin, set_y;
  int offset_x = 0; 

  #if defined(PBL_ROUND)
    if (bounds.size.h <= 180) { 
      s_line1_y = 46;  
      s_line2_y = 140; 
      
      top_margin = 16; 
      offset_x = 10; 
      
      set_y  = s_line1_y + 1;
      
      s_labels_y = s_line1_y + 23;  
      s_actual_y = s_labels_y + 12; 
      s_target_y = s_actual_y + 28; 
    } else { 
      s_line1_y = 75;  
      s_line2_y = 205; 
      top_margin = 25; 
      offset_x = 20; 
      
      set_y  = s_line1_y + 1; 
      s_labels_y = s_line1_y + 36; 
      s_actual_y = s_labels_y + 20;
      s_target_y = s_actual_y + 42;
    }
  #else
    s_line1_y = is_tall ? 60 : 48; 
    s_line2_y = is_tall ? 195 : 144;
    top_margin = is_tall ? 10 : 0;
    offset_x = 0; 
    
    set_y  = s_line1_y + (is_tall ? 5 : 0);
    s_labels_y = s_line1_y + (is_tall ? 40 : 24); 
    s_actual_y = s_labels_y + (is_tall ? 20 : 14);
    s_target_y = s_actual_y + (is_tall ? 36 : 30);
  #endif

  s_workout_bg_layer = layer_create(bounds);
  layer_set_update_proc(s_workout_bg_layer, workout_bg_update_proc);
  layer_add_child(w_layer, s_workout_bg_layer);

  s_highlight_layer = layer_create(GRect(0,0,0,0)); 
  layer_set_update_proc(s_highlight_layer, highlight_update_proc);
  layer_add_child(w_layer, s_highlight_layer);

  #if defined(PBL_ROUND)
    if (bounds.size.h <= 180) { 
      s_exercise_layer = build_text_layer(GRect(10, top_margin, bounds.size.w - 20, 32), FONT_KEY_GOTHIC_18_BOLD, PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorBlack), GTextAlignmentCenter, w_layer);
      s_next_exercise_layer = build_text_layer(GRect(0, 0, 0, 0), FONT_KEY_GOTHIC_14, GColorClear, GTextAlignmentCenter, w_layer); 
      
      s_timer_layer = build_text_layer(GRect(10, s_line2_y + 2, 75, 24), FONT_KEY_GOTHIC_18_BOLD, GColorBlack, GTextAlignmentRight, w_layer);
      s_clock_layer = build_text_layer(GRect(95, s_line2_y + 2, 75, 24), FONT_KEY_GOTHIC_18_BOLD, GColorBlack, GTextAlignmentLeft, w_layer); 
    } else { 
      s_next_exercise_layer = build_text_layer(GRect(10, top_margin, bounds.size.w - 20, 20), FONT_KEY_GOTHIC_14, GColorBlack, GTextAlignmentCenter, w_layer);
      s_exercise_layer = build_text_layer(GRect(10, top_margin + 17, bounds.size.w - 20, 32), FONT_KEY_GOTHIC_24_BOLD, PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorBlack), GTextAlignmentCenter, w_layer);
      
      s_timer_layer = build_text_layer(GRect(20, s_line2_y + 5, 105, 28), FONT_KEY_GOTHIC_24_BOLD, GColorBlack, GTextAlignmentRight, w_layer);
      s_clock_layer = build_text_layer(GRect(135, s_line2_y + 5, 105, 28), FONT_KEY_GOTHIC_24_BOLD, GColorBlack, GTextAlignmentLeft, w_layer);
    }
  #else
    if (is_tall) { 
      s_exercise_layer = build_text_layer(GRect(5, top_margin, bounds.size.w - 10, 32), FONT_KEY_GOTHIC_28_BOLD, PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorBlack), GTextAlignmentLeft, w_layer);
      s_next_exercise_layer = build_text_layer(GRect(5, top_margin + 28, bounds.size.w - 10, 20), FONT_KEY_GOTHIC_14, GColorBlack, GTextAlignmentLeft, w_layer);
      s_hr_layer = build_text_layer(GRect(5, s_line2_y + 5, 70, 24), FONT_KEY_GOTHIC_18_BOLD, PBL_IF_COLOR_ELSE(GColorRed, GColorBlack), GTextAlignmentLeft, w_layer);
      s_timer_layer = build_text_layer(GRect(75, s_line2_y + 5, 50, 24), FONT_KEY_GOTHIC_18_BOLD, GColorBlack, GTextAlignmentCenter, w_layer);
      s_clock_layer = build_text_layer(GRect(125, s_line2_y + 5, 70, 24), FONT_KEY_GOTHIC_18_BOLD, GColorBlack, GTextAlignmentRight, w_layer);
    } else { 
      s_exercise_layer = build_text_layer(GRect(2, 6, bounds.size.w - 4, 28), FONT_KEY_GOTHIC_24_BOLD, GColorBlack, GTextAlignmentLeft, w_layer);
      s_next_exercise_layer = build_text_layer(GRect(2, 28, bounds.size.w - 4, 20), FONT_KEY_GOTHIC_14, GColorBlack, GTextAlignmentLeft, w_layer);
      s_hr_layer = build_text_layer(GRect(2, s_line2_y + 4, 52, 20), FONT_KEY_GOTHIC_14_BOLD, GColorBlack, GTextAlignmentLeft, w_layer);
      s_timer_layer = build_text_layer(GRect(54, s_line2_y + 4, 36, 20), FONT_KEY_GOTHIC_14_BOLD, GColorBlack, GTextAlignmentCenter, w_layer);
      s_clock_layer = build_text_layer(GRect(90, s_line2_y + 4, 52, 20), FONT_KEY_GOTHIC_14_BOLD, GColorBlack, GTextAlignmentRight, w_layer);
    }
  #endif

  s_set_layer = build_text_layer(GRect(0, set_y, bounds.size.w, 24), is_tall ? FONT_KEY_GOTHIC_24 : FONT_KEY_GOTHIC_18, GColorBlack, GTextAlignmentCenter, w_layer);
  
  s_label_reps_layer = build_text_layer(GRect(offset_x, s_labels_y, half_w, 20), FONT_KEY_GOTHIC_14, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack), GTextAlignmentCenter, w_layer);
  s_label_weight_layer = build_text_layer(GRect(half_w - offset_x, s_labels_y, half_w, 20), FONT_KEY_GOTHIC_14, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack), GTextAlignmentCenter, w_layer);
  s_actual_reps_layer = build_text_layer(GRect(offset_x, s_actual_y, half_w, 40), FONT_KEY_BITHAM_30_BLACK, GColorBlack, GTextAlignmentCenter, w_layer);
  s_actual_weight_layer = build_text_layer(GRect(half_w - offset_x, s_actual_y, half_w, 40), FONT_KEY_BITHAM_30_BLACK, GColorBlack, GTextAlignmentCenter, w_layer);
  s_target_reps_layer = build_text_layer(GRect(offset_x, s_target_y, half_w, 22), FONT_KEY_GOTHIC_18, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack), GTextAlignmentCenter, w_layer);
  s_target_weight_layer = build_text_layer(GRect(half_w - offset_x, s_target_y, half_w, 22), FONT_KEY_GOTHIC_18, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack), GTextAlignmentCenter, w_layer);

  int rest_box_height = s_line2_y - s_line1_y;
  s_rest_overlay_layer = layer_create(GRect(0, s_line1_y, bounds.size.w, rest_box_height));
  layer_set_update_proc(s_rest_overlay_layer, rest_bg_update_proc);
  
  #if defined(PBL_ROUND)
    int r_title_y, r_time_y, r_skip_y;
    if (bounds.size.h <= 180) { 
      r_title_y = (rest_box_height * 5) / 100;
      r_time_y  = (rest_box_height * 30) / 100;
      r_skip_y  = (rest_box_height * 75) / 100;
    } else { 
      r_title_y = 10; r_time_y  = 45; r_skip_y  = 105;
    }
  #else
    int r_title_y = is_tall ? 10 : 5;
    int r_time_y  = is_tall ? 45 : 30;
    int r_skip_y  = is_tall ? 100 : 75;
  #endif

  s_rest_title_layer = build_text_layer(GRect(0, r_title_y, bounds.size.w, 30), FONT_KEY_GOTHIC_24_BOLD, GColorBlack, GTextAlignmentCenter, s_rest_overlay_layer);
  text_layer_set_text(s_rest_title_layer, "REST");
  s_rest_time_layer = build_text_layer(GRect(0, r_time_y, bounds.size.w, 45), FONT_KEY_BITHAM_42_BOLD, PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack), GTextAlignmentCenter, s_rest_overlay_layer);
  s_rest_skip_layer = build_text_layer(GRect(0, r_skip_y, bounds.size.w, 20), FONT_KEY_GOTHIC_18, GColorBlack, GTextAlignmentCenter, s_rest_overlay_layer);
  text_layer_set_text(s_rest_skip_layer, "[Select] to Skip");
  
  layer_add_child(w_layer, s_rest_overlay_layer);

  #if defined(PBL_ROUND)
    s_progress_layer = layer_create(bounds);
  #else
    s_progress_layer = layer_create(GRect(0, 0, bounds.size.w, 6));
  #endif
  layer_set_update_proc(s_progress_layer, progress_update_proc);
  layer_add_child(w_layer, s_progress_layer);

  s_edit_mode = 0; 
  if (s_rest_seconds_remaining > 0) {
    s_is_resting = true;
    set_rest_overlay_state(true, false);
  } else {
    s_is_resting = false;
    layer_set_hidden(s_rest_overlay_layer, true);
  }
  
  update_workout_ui(false);
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
}

static void workout_window_unload(Window *window) {
  tick_timer_service_unsubscribe();

  if (s_box_anim) {
      animation_unschedule(s_box_anim);
      s_box_anim = NULL;
  }
  if (s_rest_anim) {
      animation_unschedule(s_rest_anim);
      s_rest_anim = NULL;
  }

  layer_destroy(s_progress_layer);
  layer_destroy(s_workout_bg_layer);
  
  text_layer_destroy(s_timer_layer);
  text_layer_destroy(s_clock_layer); 
  text_layer_destroy(s_exercise_layer);
  text_layer_destroy(s_next_exercise_layer); 
  text_layer_destroy(s_set_layer);
  text_layer_destroy(s_label_reps_layer); 
  text_layer_destroy(s_label_weight_layer);
  text_layer_destroy(s_target_reps_layer);
  text_layer_destroy(s_target_weight_layer);
  text_layer_destroy(s_actual_reps_layer);
  text_layer_destroy(s_actual_weight_layer);
  #if !defined(PBL_ROUND)
    text_layer_destroy(s_hr_layer);
  #endif
  layer_destroy(s_rest_overlay_layer);
  text_layer_destroy(s_rest_title_layer);
  text_layer_destroy(s_rest_time_layer);
  text_layer_destroy(s_rest_skip_layer);
  layer_destroy(s_highlight_layer);
}

// --- MAIN MENU WINDOW LOGIC ---
static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  int rows = s_active_slots;
  if (s_has_resume) rows++; 
  if (s_active_slots < MAX_SLOTS) rows++;
  rows++; 
  return rows;
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  int i = cell_index->row;
  
  if (s_has_resume) {
    if (i == 0) {
      menu_cell_basic_draw(ctx, cell_layer, "Resume Workout", "Continue where you left off", NULL);
      return;
    }
    i--; 
  }
  
  if (i < s_active_slots) {
    char subtitle[32];
    snprintf(subtitle, sizeof(subtitle), "%d Exercises", s_slot_counts[i]);
    menu_cell_basic_draw(ctx, cell_layer, s_slot_names[i], subtitle, NULL);
  } else if (i == s_active_slots && s_active_slots < MAX_SLOTS) {
    menu_cell_basic_draw(ctx, cell_layer, "Add New Routine", "Send to save here", NULL); 
  } else {
    menu_cell_basic_draw(ctx, cell_layer, "Settings", "Timers, Units, & Themes", NULL); 
  }
}

static void start_workout_from_slot(int slot_idx) {
  s_current_slot = slot_idx; 
  
  RoutineHeader header;
  persist_read_data(STORAGE_KEY_BASE + slot_idx, &header, sizeof(RoutineHeader));
  snprintf(s_routine_name, sizeof(s_routine_name), "%s", header.name);
  s_total_exercises = header.total_exercises;
  
  s_total_workout_sets = 0;
  for (int j = 0; j < s_total_exercises; j++) {
      persist_read_data(ROUTINE_EX_BASE + (slot_idx * MAX_EXERCISES) + j, &s_exercises[j], sizeof(Exercise));
      s_exercises[j].current_set = 1; 
      s_total_workout_sets += s_exercises[j].target_sets;
  }
  
  s_curr_ex_idx = 0;
  s_workout_sec = 0;
  s_peak_hr = 0;
  s_total_hr = 0;
  s_hr_samples = 0;
  
  init_temp_values(&s_exercises[0]);
  
  s_is_resting = false;
  s_rest_seconds_remaining = 0;
  s_workout_active = true; 
  
  s_last_workout_sec = persist_exists(GHOST_KEY_BASE + slot_idx) ? persist_read_int(GHOST_KEY_BASE + slot_idx) : 0;
  
  push_workout_window(); 
}

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  int i = cell_index->row;
  
  if (s_has_resume) {
    if (i == 0) {
      ActiveState state;
      persist_read_data(ACTIVE_STATE_KEY, &state, sizeof(state));
      s_total_exercises = state.total_exercises;
      s_total_workout_sets = state.total_workout_sets;
      s_curr_ex_idx = state.curr_ex_idx;
      s_workout_sec = state.workout_sec;
      s_current_slot = state.current_slot;
      s_peak_hr = state.peak_hr;
      s_total_hr = state.total_hr;
      s_hr_samples = state.hr_samples;
      strncpy(s_routine_name, state.routine_name, sizeof(s_routine_name));
      
      for (int j = 0; j < s_total_exercises; j++) {
          persist_read_data(ACTIVE_EX_BASE + j, &s_exercises[j], sizeof(Exercise));
      }
      
      s_workout_active = true;
      s_has_resume = false; 
      persist_delete(ACTIVE_STATE_KEY); 
      
      s_is_resting = false;
      s_rest_seconds_remaining = 0;
      
      init_temp_values(&s_exercises[s_curr_ex_idx]);
      
      s_last_workout_sec = persist_exists(GHOST_KEY_BASE + s_current_slot) ? persist_read_int(GHOST_KEY_BASE + s_current_slot) : 0;
      
      menu_layer_reload_data(s_menu_layer);
      push_workout_window();
      return;
    }
    i--; 
  }
  
  if (i < s_active_slots) {
    start_workout_from_slot(i);
  } else if (i == s_active_slots && s_active_slots < MAX_SLOTS) {
    push_help_window(); 
  } else {
    push_settings_window(); 
  }
}

// --- ROUTINE INSPECTOR LOGIC ---
static uint16_t inspector_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return s_slot_counts[s_slot_to_edit];
}

static void inspector_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  Exercise temp_ex;
  persist_read_data(ROUTINE_EX_BASE + (s_slot_to_edit * MAX_EXERCISES) + cell_index->row, &temp_ex, sizeof(Exercise));
  
  char subtitle[32];
  if (temp_ex.target_weight == 0) {
      snprintf(subtitle, sizeof(subtitle), "%d Sets x %d Reps (BW)", temp_ex.target_sets, temp_ex.target_reps);
  } else {
      snprintf(subtitle, sizeof(subtitle), "%d Sets x %d Reps @ %d%s", 
          temp_ex.target_sets, temp_ex.target_reps, temp_ex.target_weight, s_weight_unit_idx == 0 ? "kg" : "lbs");
  }
  menu_cell_basic_draw(ctx, cell_layer, temp_ex.name, subtitle, NULL);
}

static void inspector_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);
  
  s_inspector_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_inspector_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = inspector_get_num_rows_callback,
    .draw_row = inspector_draw_row_callback,
  });
  
  menu_layer_set_normal_colors(s_inspector_menu_layer, get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(s_inspector_menu_layer, get_theme_color(), get_bg_color());
  menu_layer_set_click_config_onto_window(s_inspector_menu_layer, window);
  layer_add_child(w_layer, menu_layer_get_layer(s_inspector_menu_layer));
}

static void inspector_window_unload(Window *window) {
  menu_layer_destroy(s_inspector_menu_layer);
}

static void push_inspector_window() {
  if(!s_inspector_window) {
    s_inspector_window = window_create();
    window_set_window_handlers(s_inspector_window, (WindowHandlers) { .load = inspector_window_load, .unload = inspector_window_unload });
  }
  window_set_background_color(s_inspector_window, get_bg_color());
  window_stack_push(s_inspector_window, true);
}

// --- ROUTINE ACTION MENU ---
static void action_start_callback(int index, void *ctx) {
  window_stack_remove(s_action_window, false);
  start_workout_from_slot(s_slot_to_edit);
}

static void action_inspect_callback(int index, void *ctx) {
  push_inspector_window();
}

static void action_edit_callback(int index, void *ctx) {
  s_target_swap_slot = s_slot_to_edit;
  push_confirm_window();
}

static void action_window_load(Window *window) {
  int num_items = 0;
  s_action_menu_items[num_items++] = (SimpleMenuItem) { .title = "Start Workout", .callback = action_start_callback };
  s_action_menu_items[num_items++] = (SimpleMenuItem) { .title = "View Exercises", .callback = action_inspect_callback };
  s_action_menu_items[num_items++] = (SimpleMenuItem) { .title = "Move / Delete", .callback = action_edit_callback };

  s_action_menu_sections[0] = (SimpleMenuSection) {
    .title = s_slot_names[s_slot_to_edit],
    .num_items = num_items,
    .items = s_action_menu_items,
  };

  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(w_layer);

  s_action_menu_layer = simple_menu_layer_create(bounds, window, s_action_menu_sections, 1, NULL);
  MenuLayer *internal_menu = simple_menu_layer_get_menu_layer(s_action_menu_layer);
  menu_layer_set_normal_colors(internal_menu, get_bg_color(), get_text_color()); 
  menu_layer_set_highlight_colors(internal_menu, get_theme_color(), get_bg_color());
  
  layer_add_child(w_layer, simple_menu_layer_get_layer(s_action_menu_layer));
}

static void action_window_unload(Window *window) {
  simple_menu_layer_destroy(s_action_menu_layer);
}

static void push_action_window() {
  if(!s_action_window) {
    s_action_window = window_create();
    window_set_window_handlers(s_action_window, (WindowHandlers) { .load = action_window_load, .unload = action_window_unload });
  }
  window_set_background_color(s_action_window, get_bg_color());
  window_stack_push(s_action_window, true);
}

static void menu_select_long_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  int i = cell_index->row;
  if (s_has_resume) {
      if (i == 0) return; 
      i--;
  }
  if (i < s_active_slots) {
    s_slot_to_edit = i;
    push_action_window(); 
  }
}

static void save_routine_to_slot(int slot_idx) {
  RoutineHeader header;
  snprintf(header.name, sizeof(header.name), "%s", s_routine_name);
  header.total_exercises = s_total_exercises;
  persist_write_data(STORAGE_KEY_BASE + slot_idx, &header, sizeof(RoutineHeader));
  
  for (int j = 0; j < s_total_exercises; j++) {
      persist_write_data(ROUTINE_EX_BASE + (slot_idx * MAX_EXERCISES) + j, &s_exercises[j], sizeof(Exercise));
  }
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *routine_data_tuple = dict_find(iterator, MESSAGE_KEY_ROUTINE_DATA);
  if (routine_data_tuple && routine_data_tuple->type == TUPLE_CSTRING) {
    char *delimited_string = routine_data_tuple->value->cstring;
    
    if (strncmp(delimited_string, "BATCH~", 6) == 0) {
        
        for(int i = 0; i < MAX_SLOTS; i++) {
            persist_delete(STORAGE_KEY_BASE + i);
            for(int j = 0; j < MAX_EXERCISES; j++) persist_delete(ROUTINE_EX_BASE + (i * MAX_EXERCISES) + j);
        }
        s_active_slots = 0;

        char *start = delimited_string + 6; 
        char *end = start;
        int slot_idx = 0;

        while (*end != '\0' && slot_idx < MAX_SLOTS) {
            if (*end == '~') {
                *end = '\0'; 
                parse_routine_string(start);
                save_routine_to_slot(slot_idx);
                slot_idx++;
                start = end + 1; 
            }
            end++;
        }
        
        if (*start != '\0' && slot_idx < MAX_SLOTS) {
            parse_routine_string(start);
            save_routine_to_slot(slot_idx);
        }
        
        vibes_double_pulse(); 

    } else {
        // --- STANDARD SINGLE-ROUTINE LOAD (Two-Way Sync Backup) ---
        parse_routine_string(delimited_string);
        
        int target_slot = s_active_slots; 
        for (int i = 0; i < s_active_slots; i++) {
            if (strcmp(s_slot_names[i], s_routine_name) == 0) { target_slot = i; break; }
        }
        if (target_slot > MAX_SLOTS - 1) target_slot = MAX_SLOTS - 1; 
        
        save_routine_to_slot(target_slot);
    }
    
    refresh_directory();
    menu_layer_reload_data(s_menu_layer);
  }
  
  Tuple *prog_mode_tuple = dict_find(iterator, MESSAGE_KEY_PROGRESSION_MODE);
  if (prog_mode_tuple) {
    s_progression_mode = prog_mode_tuple->value->int32;
    persist_write_int(SETTINGS_KEY_BASE + 10, s_progression_mode);
  }

  Tuple *weight_inc_tuple = dict_find(iterator, MESSAGE_KEY_WEIGHT_INCREMENT);
  if (weight_inc_tuple) {
    s_weight_increment = weight_inc_tuple->value->int32;
    persist_write_int(SETTINGS_KEY_BASE + 11, s_weight_increment);
  }
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  s_main_header_bg = layer_create(GRect(0, 0, bounds.size.w, 40));
  layer_set_update_proc(s_main_header_bg, header_bg_update_proc);
  layer_add_child(window_layer, s_main_header_bg);
  
  s_main_header_text = build_text_layer(GRect(0, 6, bounds.size.w, 30), FONT_KEY_GOTHIC_24_BOLD, GColorWhite, GTextAlignmentCenter, s_main_header_bg);
  text_layer_set_text(s_main_header_text, "Select Routine");

  s_menu_layer = menu_layer_create(GRect(0, 40, bounds.size.w, bounds.size.h - 40));
  refresh_directory();
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = menu_get_num_rows_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback,
    .select_long_click = menu_select_long_callback,
  });
  
  menu_layer_set_normal_colors(s_menu_layer, get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(s_menu_layer, get_theme_color(), get_bg_color());
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  
  if (s_active_slots > 0) {
    int next_slot = s_last_routine_slot + 1;
    if (next_slot >= s_active_slots) next_slot = 0; 
    menu_layer_set_selected_index(s_menu_layer, MenuIndex(0, next_slot), MenuRowAlignCenter, false);
  }
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_main_header_text);
  layer_destroy(s_main_header_bg);
  menu_layer_destroy(s_menu_layer);
}

// --- APP LIFECYCLE ---
static void init() {
  load_settings(); 
  
  if (!persist_exists(V5_MIGRATION_KEY)) {
      for(int i = 0; i < MAX_SLOTS; i++) persist_delete(STORAGE_KEY_BASE + i);
      persist_write_bool(V5_MIGRATION_KEY, true);
  }
  
  if (persist_exists(ACTIVE_STATE_KEY)) s_has_resume = true;
  
  s_main_window = window_create();
  
  s_mega_window = window_create();
  window_set_window_handlers(s_mega_window, (WindowHandlers) { .load = mega_window_load, .unload = mega_window_unload });
  
  s_note_window = window_create();
  window_set_window_handlers(s_note_window, (WindowHandlers) { .load = note_window_load, .unload = note_window_unload });
  
  window_set_background_color(s_main_window, get_bg_color()); 
  window_set_window_handlers(s_main_window, (WindowHandlers) { .load = main_window_load, .unload = main_window_unload });
  window_stack_push(s_main_window, true);
  
  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum()); 
}

static void deinit() {
  if (s_workout_active) {
    ActiveState state = {
      .total_exercises = s_total_exercises,
      .total_workout_sets = s_total_workout_sets,
      .curr_ex_idx = s_curr_ex_idx,
      .workout_sec = s_workout_sec,
      .current_slot = s_current_slot,
      .peak_hr = s_peak_hr,       
      .total_hr = s_total_hr,     
      .hr_samples = s_hr_samples  
    };
    strncpy(state.routine_name, s_routine_name, sizeof(state.routine_name));
    persist_write_data(ACTIVE_STATE_KEY, &state, sizeof(state));
    
    for(int j = 0; j < s_total_exercises; j++) {
      persist_write_data(ACTIVE_EX_BASE + j, &s_exercises[j], sizeof(Exercise));
    }
  }
  if (s_summary_window) { window_destroy(s_summary_window); s_summary_window = NULL; }
  if (s_workout_window) { window_destroy(s_workout_window); s_workout_window = NULL; }
  if (s_confirm_window) { window_destroy(s_confirm_window); s_confirm_window = NULL; }
  if (s_help_window) { window_destroy(s_help_window); s_help_window = NULL; }
  if (s_settings_window) { window_destroy(s_settings_window); s_settings_window = NULL; }
  if (s_variation_window) { window_destroy(s_variation_window); s_variation_window = NULL; }
  if (s_exit_window) { window_destroy(s_exit_window); s_exit_window = NULL; }
  if (s_sensation_window) { window_destroy(s_sensation_window); s_sensation_window = NULL; }
  if (s_mega_window) { window_destroy(s_mega_window); s_mega_window = NULL; }
  if (s_note_window) { window_destroy(s_note_window); s_note_window = NULL; }
  if (s_action_window) { window_destroy(s_action_window); s_action_window = NULL; }
  if (s_inspector_window) { window_destroy(s_inspector_window); s_inspector_window = NULL; }
  if (s_dictation_session) { dictation_session_destroy(s_dictation_session); s_dictation_session = NULL; }
    
  window_destroy(s_main_window); 
  s_main_window = NULL;
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}