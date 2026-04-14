#include <pebble.h>

#define MAX_EXERCISES 10 
#define MAX_SLOTS 7
#define STORAGE_KEY_BASE 100 
#define SETTINGS_KEY_BASE 200

// --- DATA STRUCTURES ---
typedef struct {
  char name[32]; 
  int target_sets;
  int target_reps;
  int target_weight;
  int modifier;
  int current_set;
  int actual_reps[10];   
  int actual_weight[10];
} Exercise;

// --- APP STATE ---
static char s_routine_name[32] = "No Routine";
static Exercise s_exercises[MAX_EXERCISES];
static int s_total_exercises = 0;
static int s_total_workout_sets = 0; 
static char s_slot_names[MAX_SLOTS][32];
static int s_slot_counts[MAX_SLOTS];
static int s_active_slots = 0; 

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

// --- WORKOUT TRACKING ---
static int s_curr_ex_idx = 0;
static int s_workout_sec = 0;
static int s_edit_mode = 0; 
static int s_temp_reps = 0;
static int s_temp_weight = 0;

// --- GEOMETRY & CACHING ---
static int s_line1_y, s_line2_y;
static int s_labels_y, s_actual_y, s_target_y; 
static int s_highlight_box_width = 0;

// --- UI ELEMENTS ---
static Window *s_main_window, *s_settings_window, *s_workout_window, *s_help_window, *s_confirm_window, *s_summary_window, *s_variation_window;
static MenuLayer *s_menu_layer, *s_settings_menu_layer, *s_variation_menu_layer; 
static Layer *s_main_header_bg, *s_settings_header_bg, *s_progress_layer, *s_workout_bg_layer, *s_summary_bg_layer, *s_rest_overlay_layer;
static TextLayer *s_main_header_text, *s_settings_header_text, *s_timer_layer, *s_clock_layer, *s_exercise_layer; 
static TextLayer *s_next_exercise_layer, *s_set_layer, *s_label_reps_layer, *s_label_weight_layer, *s_target_reps_layer; 
static TextLayer *s_target_weight_layer, *s_actual_reps_layer, *s_actual_weight_layer, *s_hr_layer, *s_help_text_layer;
static TextLayer *s_confirm_text_layer, *s_sum_title_layer, *s_sum_info_layer, *s_beat_layer, *s_missed_layer;
static TextLayer *s_rest_title_layer, *s_rest_time_layer, *s_rest_skip_layer;
static BitmapLayer *s_fireworks_layer;
static GBitmap *s_fireworks_bitmap;

static const char *s_variations[] = {
  "None (Clear)", "(Seated)", "(Standing)", "(Dumbbell)", "(Barbell)", 
  "(Cable)", "(Machine)", "(Single Arm)", "(Single Leg)"
};
#define NUM_VARIATIONS 9

static int s_slot_to_edit = -1;
static int s_target_swap_slot = -1; 
static bool s_is_resting = false;
static int s_rest_seconds_remaining = 0;

// --- FORWARD DECLARATIONS FOR LAZY LOADING ---
static void update_workout_ui();
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
  text_layer_set_text_color(text_layer, text_color);
  text_layer_set_background_color(text_layer, GColorClear);
  text_layer_set_text_alignment(text_layer, alignment);
  text_layer_set_overflow_mode(text_layer, GTextOverflowModeTrailingEllipsis);
  
  if (parent) {
    layer_add_child(parent, text_layer_get_layer(text_layer));
  }
  return text_layer;
}

static GColor get_theme_color() {
  if (s_theme_color_idx >= 4) {
    #if defined(PBL_COLOR)
      uint8_t color_val = s_theme_color_idx - 4; 
      return (GColor){ .argb = 0b11000000 | color_val };
    #else
      return GColorBlack;
    #endif
  }

  if (s_theme_color_idx == 1) return PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorBlack);
  if (s_theme_color_idx == 2) return PBL_IF_COLOR_ELSE(GColorRed, GColorBlack);
  if (s_theme_color_idx == 3) return PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorBlack);
  return PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack); 
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
        int ex_idx = (token_count - 1) / 5;
        int field = (token_count - 1) % 5;
        
        if (ex_idx < MAX_EXERCISES) {
          if (field == 0) snprintf(s_exercises[ex_idx].name, sizeof(s_exercises[ex_idx].name), "%s", temp);
          else if (field == 1) s_exercises[ex_idx].target_sets = atoi(temp);
          else if (field == 2) s_exercises[ex_idx].target_reps = atoi(temp);
          else if (field == 3) s_exercises[ex_idx].target_weight = atoi(temp);
          else if (field == 4) {
            s_exercises[ex_idx].modifier = atoi(temp);
            
            if (s_exercises[ex_idx].modifier == 1) {
                s_exercises[ex_idx].target_sets *= 2;
                if (s_exercises[ex_idx].target_sets > 10) s_exercises[ex_idx].target_sets = 10; 
            }
            
            s_exercises[ex_idx].current_set = 1;
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
  
  // OPTIMIZATION: Moving large arrays to static to prevent Stack Overflows
  static char temp_data[256]; 

  for(int i = 0; i < MAX_SLOTS; i++) {
    if(persist_exists(STORAGE_KEY_BASE + i)) {
      persist_read_string(STORAGE_KEY_BASE + i, temp_data, sizeof(temp_data));
      
      if (i != s_active_slots) {
        persist_write_string(STORAGE_KEY_BASE + s_active_slots, temp_data);
        persist_delete(STORAGE_KEY_BASE + i);
      }

      parse_routine_string(temp_data);
      snprintf(s_slot_names[s_active_slots], sizeof(s_slot_names[s_active_slots]), "%s", s_routine_name);
      s_slot_counts[s_active_slots] = s_total_exercises;
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
  window_stack_push(s_settings_window, true);
}

static void push_help_window() {
  if(!s_help_window) {
    s_help_window = window_create();
    window_set_window_handlers(s_help_window, (WindowHandlers) { .load = help_window_load, .unload = help_window_unload });
  }
  window_stack_push(s_help_window, true);
}

static void push_confirm_window() {
  if(!s_confirm_window) {
    s_confirm_window = window_create();
    window_set_click_config_provider(s_confirm_window, confirm_click_provider);
    window_set_window_handlers(s_confirm_window, (WindowHandlers) { .load = confirm_window_load, .unload = confirm_window_unload });
  }
  window_stack_push(s_confirm_window, true);
}

static void push_workout_window() {
  if(!s_workout_window) {
    s_workout_window = window_create();
    window_set_click_config_provider(s_workout_window, wo_click_provider);
    window_set_window_handlers(s_workout_window, (WindowHandlers) { .load = workout_window_load, .unload = workout_window_unload });
  }
  window_stack_push(s_workout_window, true);
}

static void push_summary_window() {
  if(!s_summary_window) {
    s_summary_window = window_create();
    window_set_click_config_provider(s_summary_window, summary_click_provider);
    window_set_window_handlers(s_summary_window, (WindowHandlers) { .load = summary_window_load, .unload = summary_window_unload });
  }
  window_stack_push(s_summary_window, true);
}

// --- SETTINGS WINDOW LOGIC ---
static uint16_t settings_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) { return 10; }

static void settings_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  int i = cell_index->row;
  char title[32], subtitle[32];
  const char* vibes[] = {"Off", "Short", "Long", "Double"};
  const char* themes[] = {"Orange", "Blue", "Red", "Green"};
  const char* units[] = {"kg", "lbs"};

  switch(i) {
    case 0: snprintf(title, sizeof(title), "Set Rest"); snprintf(subtitle, sizeof(subtitle), "%ds", s_set_rest_sec); break;
    case 1: snprintf(title, sizeof(title), "Ex. Rest"); snprintf(subtitle, sizeof(subtitle), "%ds", s_ex_rest_sec); break;
    case 2: snprintf(title, sizeof(title), "Set Vibe"); snprintf(subtitle, sizeof(subtitle), "%s", vibes[s_set_vibe]); break;
    case 3: snprintf(title, sizeof(title), "Ex. Vibe"); snprintf(subtitle, sizeof(subtitle), "%s", vibes[s_ex_vibe]); break;
    case 4: snprintf(title, sizeof(title), "Rest Vibe"); snprintf(subtitle, sizeof(subtitle), "%s", vibes[s_rest_vibe]); break;
    case 5: 
      snprintf(title, sizeof(title), "Theme Color"); 
      if (s_theme_color_idx >= 4) {
          snprintf(subtitle, sizeof(subtitle), "Secret Mode: %d", (s_theme_color_idx - 4));
      } else {
          snprintf(subtitle, sizeof(subtitle), "%s", themes[s_theme_color_idx]);
      }
      break;
    case 6: snprintf(title, sizeof(title), "Weight Unit"); snprintf(subtitle, sizeof(subtitle), "%s", units[s_weight_unit_idx]); break;
    case 7: snprintf(title, sizeof(title), "Long Press"); snprintf(subtitle, sizeof(subtitle), "%d ms", s_long_press_ms); break; 
    case 8: snprintf(title, sizeof(title), "Drop Set %%"); snprintf(subtitle, sizeof(subtitle), "-%d%%", s_drop_set_pct); break;
    case 9: snprintf(title, sizeof(title), "Super Rest"); snprintf(subtitle, sizeof(subtitle), "%ds", s_super_rest_sec); break;
  }
  menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
}

static void settings_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  switch(cell_index->row) {
    case 0: s_set_rest_sec += 15; if(s_set_rest_sec > 180) s_set_rest_sec = 0; save_setting(0, s_set_rest_sec); break;
    case 1: s_ex_rest_sec += 30; if(s_ex_rest_sec > 240) s_ex_rest_sec = 0; save_setting(1, s_ex_rest_sec); break;
    case 2: s_set_vibe++; if(s_set_vibe > 3) s_set_vibe = 0; save_setting(2, s_set_vibe); play_vibe(s_set_vibe); break;
    case 3: s_ex_vibe++; if(s_ex_vibe > 3) s_ex_vibe = 0; save_setting(3, s_ex_vibe); play_vibe(s_ex_vibe); break;
    case 4: s_rest_vibe++; if(s_rest_vibe > 3) s_rest_vibe = 0; save_setting(4, s_rest_vibe); play_vibe(s_rest_vibe); break;
    case 5: 
      s_theme_color_idx++; 
      if (s_theme_color_idx == 4) s_theme_color_idx = 0; 
      else if (s_theme_color_idx > 67) s_theme_color_idx = 4; 
      
      save_setting(5, s_theme_color_idx); 
      menu_layer_set_highlight_colors(s_settings_menu_layer, get_theme_color(), GColorWhite); 
      break;
    case 6: s_weight_unit_idx++; if(s_weight_unit_idx > 1) s_weight_unit_idx = 0; save_setting(6, s_weight_unit_idx); break;
    case 7: s_long_press_ms += 250; if(s_long_press_ms > 1500) s_long_press_ms = 250; save_setting(7, s_long_press_ms); break; 
    case 8: s_drop_set_pct += 5; if(s_drop_set_pct > 50) s_drop_set_pct = 10; save_setting(8, s_drop_set_pct); break;
    case 9: s_super_rest_sec += 5; if(s_super_rest_sec > 30) s_super_rest_sec = 0; save_setting(9, s_super_rest_sec); break;
  }
  menu_layer_reload_data(s_settings_menu_layer);
}

static void header_bg_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone); 
}

static void settings_select_long_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (cell_index->row == 5) {
    if (s_theme_color_idx < 4) s_theme_color_idx = 4; 
    else s_theme_color_idx = 0; 
    
    save_setting(5, s_theme_color_idx);
    vibes_double_pulse(); 
    menu_layer_reload_data(s_settings_menu_layer);
    menu_layer_set_highlight_colors(s_settings_menu_layer, get_theme_color(), GColorWhite);
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
    .get_num_rows = settings_get_num_rows_callback,
    .draw_row = settings_draw_row_callback,
    .select_click = settings_select_callback,
    .select_long_click = settings_select_long_callback, 
  });
  
  menu_layer_set_normal_colors(s_settings_menu_layer, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_settings_menu_layer, get_theme_color(), GColorWhite);
  menu_layer_set_click_config_onto_window(s_settings_menu_layer, window);
  layer_add_child(w_layer, menu_layer_get_layer(s_settings_menu_layer));
}

static void settings_window_unload(Window *window) {
  text_layer_destroy(s_settings_header_text);
  layer_destroy(s_settings_header_bg);
  menu_layer_set_highlight_colors(s_menu_layer, get_theme_color(), GColorWhite);
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
  } else {
    char edit_data[256], target_data[256];
    bool target_exists = persist_exists(STORAGE_KEY_BASE + s_target_swap_slot);
    persist_read_string(STORAGE_KEY_BASE + s_slot_to_edit, edit_data, sizeof(edit_data));
    if (target_exists) persist_read_string(STORAGE_KEY_BASE + s_target_swap_slot, target_data, sizeof(target_data));
    persist_write_string(STORAGE_KEY_BASE + s_target_swap_slot, edit_data);
    if (target_exists) persist_write_string(STORAGE_KEY_BASE + s_slot_to_edit, target_data);
    else persist_delete(STORAGE_KEY_BASE + s_slot_to_edit);
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

// --- SUMMARY WINDOW LOGIC ---
static void summary_exit_click(ClickRecognizerRef recognizer, void *context) { window_stack_pop(true); }
static void summary_click_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, summary_exit_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, summary_exit_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, summary_exit_click);
}
static void summary_bg_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  bool is_tall = (bounds.size.h > 180);
  
  int y_boxes = is_tall ? 120 : 75;
  int box_h = is_tall ? 50 : 46; 
  int mid = bounds.size.w / 2;
  
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorBlack));
  graphics_fill_rect(ctx, GRect(10, y_boxes, mid - 15, box_h), 8, GCornersAll);
  
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, GColorDarkGray));
  graphics_fill_rect(ctx, GRect(mid + 5, y_boxes, mid - 15, box_h), 8, GCornersAll);
}

static void summary_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);
  bool is_tall = (bounds.size.h > 180);
  
  int y_title = is_tall ? 5 : 0;
  int y_img = is_tall ? 50 : 32;
  int y_boxes = is_tall ? 120 : 75;
  int box_text_y = y_boxes + (is_tall ? 5 : 3);
  int y_info = is_tall ? 190 : 126; 

  s_sum_title_layer = build_text_layer(GRect(0, y_title, bounds.size.w, 40), FONT_KEY_GOTHIC_28_BOLD, GColorBlack, GTextAlignmentCenter, w_layer);

  s_fireworks_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_FIREWORKS);
  if (s_fireworks_bitmap) {
    s_fireworks_layer = bitmap_layer_create(GRect(0, y_img, bounds.size.w, 40));
    bitmap_layer_set_bitmap(s_fireworks_layer, s_fireworks_bitmap);
    bitmap_layer_set_compositing_mode(s_fireworks_layer, GCompOpSet);
    bitmap_layer_set_alignment(s_fireworks_layer, GAlignCenter);
    layer_add_child(w_layer, bitmap_layer_get_layer(s_fireworks_layer));
  }

  s_summary_bg_layer = layer_create(bounds);
  layer_set_update_proc(s_summary_bg_layer, summary_bg_update_proc);
  layer_add_child(w_layer, s_summary_bg_layer);

  s_beat_layer = build_text_layer(GRect(10, box_text_y, (bounds.size.w/2) - 15, 40), FONT_KEY_GOTHIC_18_BOLD, GColorWhite, GTextAlignmentCenter, w_layer);
  s_missed_layer = build_text_layer(GRect((bounds.size.w/2) + 5, box_text_y, (bounds.size.w/2) - 15, 40), FONT_KEY_GOTHIC_18_BOLD, GColorWhite, GTextAlignmentCenter, w_layer);
  
  s_sum_info_layer = build_text_layer(GRect(0, y_info, bounds.size.w, 40), FONT_KEY_GOTHIC_14, GColorBlack, GTextAlignmentCenter, w_layer);

  tick_timer_service_unsubscribe();
  int m = s_workout_sec / 60;
  int s = s_workout_sec % 60;
  static char title_buf[32];
  snprintf(title_buf, sizeof(title_buf), "Done! %02d:%02d", m, s);
  text_layer_set_text(s_sum_title_layer, title_buf);

  int sets_above = 0, sets_below = 0;
  for (int i = 0; i < s_total_exercises; i++) {
    for (int j = 0; j < s_exercises[i].target_sets; j++) {
      int a_r = s_exercises[i].actual_reps[j];
      int a_w = s_exercises[i].actual_weight[j];
      int t_r = s_exercises[i].target_reps;
      
      int t_w = s_exercises[i].target_weight;
      if (s_exercises[i].modifier == 1 && ((j + 1) % 2 == 0)) {
          t_w = (t_w * (100 - s_drop_set_pct)) / 100;
      }
      
      if (a_w > t_w || (a_w == t_w && a_r > t_r)) sets_above++;
      else if (a_w < t_w || (a_w == t_w && a_r < t_r)) sets_below++;
    }
  }

  static char beat_buf[32], missed_buf[32];
  snprintf(beat_buf, sizeof(beat_buf), "Beat\n%d", sets_above);
  snprintf(missed_buf, sizeof(missed_buf), "Missed\n%d", sets_below);
  text_layer_set_text(s_beat_layer, beat_buf);
  text_layer_set_text(s_missed_layer, missed_buf);

  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  char date_buf[16];
  strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tick_time);

  // OPTIMIZATION: Moving large arrays to static to prevent Stack Overflows
  static char export_buf[512]; 
  int limit = sizeof(export_buf);
  int written = snprintf(export_buf, limit, "%s|%s|%d", s_routine_name, date_buf, s_workout_sec);
  int offset = (written < limit) ? written : limit - 1;

  for(int i = 0; i < s_total_exercises; i++) {
    if (offset >= limit - 1) break; 
    
    const char *mod_label = "";
    if (s_exercises[i].modifier == 1) mod_label = " [DROP]";
    else if (s_exercises[i].modifier == 2) mod_label = " [SUPER]";
    
    written = snprintf(export_buf + offset, limit - offset, "|%s%s", s_exercises[i].name, mod_label);
    offset += (written < limit - offset) ? written : limit - offset - 1;
    
    for(int j = 0; j < s_exercises[i].target_sets; j++) {
      if (offset >= limit - 1) break; 
      written = snprintf(export_buf + offset, limit - offset, "|%d|%d", s_exercises[i].actual_reps[j], s_exercises[i].actual_weight[j]);
      offset += (written < limit - offset) ? written : limit - offset - 1;
    }
  }
  
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      dict_write_cstring(iter, MESSAGE_KEY_WORKOUT_SUMMARY, export_buf);
      app_message_outbox_send();
      text_layer_set_text(s_sum_info_layer, "Data synced!\nPress any button to exit.");
  } else {
      text_layer_set_text(s_sum_info_layer, "Sync Failed.\nCheck Bluetooth connection.");
  }
}

static void summary_window_unload(Window *window) {
  text_layer_destroy(s_sum_title_layer);
  layer_destroy(s_summary_bg_layer);
  text_layer_destroy(s_beat_layer);
  text_layer_destroy(s_missed_layer);
  text_layer_destroy(s_sum_info_layer);
  if (s_fireworks_bitmap) {
    bitmap_layer_destroy(s_fireworks_layer);
    gbitmap_destroy(s_fireworks_bitmap);
  }
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
    char temp[32];
    snprintf(temp, sizeof(temp), "%s %s", base_name, var_str);
    strncpy(ex->name, temp, sizeof(ex->name) - 1);
  }
  ex->name[sizeof(ex->name) - 1] = '\0'; 
  
  update_workout_ui();
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
  
  menu_layer_set_normal_colors(s_variation_menu_layer, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_variation_menu_layer, get_theme_color(), GColorWhite);
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
  window_stack_push(s_variation_window, true);
}

// --- WORKOUT WINDOW LOGIC ---
static void progress_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite));
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (s_total_workout_sets > 0) {
    int completed = 0;
    for (int i = 0; i < s_total_exercises; i++) {
      if (i < s_curr_ex_idx) {
        if (s_exercises[i].modifier == 2 && i == s_curr_ex_idx - 1) {
          completed += s_exercises[i].current_set; // First half of active superset
        } else {
          completed += s_exercises[i].target_sets; // Fully completed exercise
        }
      } else if (i == s_curr_ex_idx) {
        completed += (s_exercises[i].current_set - 1); // Current exercise
      } else if (i > s_curr_ex_idx) {
        if (i == s_curr_ex_idx + 1 && s_exercises[s_curr_ex_idx].modifier == 2) {
          completed += (s_exercises[i].current_set - 1); // Second half of active superset
        }
      }
    }
    
    int width = (bounds.size.w * completed) / s_total_workout_sets;
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorBlack));
    graphics_fill_rect(ctx, GRect(0, 0, width, bounds.size.h), 0, GCornerNone);
  }
}

static void workout_bg_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int half_w = bounds.size.w / 2;
  bool is_tall = (bounds.size.h > 180); 
  
  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorLightGray, GColorDarkGray));
  graphics_context_set_stroke_width(ctx, 2); 
  graphics_draw_line(ctx, GPoint(0, s_line1_y), GPoint(bounds.size.w, s_line1_y));
  graphics_draw_line(ctx, GPoint(0, s_line2_y), GPoint(bounds.size.w, s_line2_y));

  graphics_context_set_stroke_color(ctx, get_theme_color());
  graphics_context_set_stroke_width(ctx, 4); 
  
  int box_y_coord = s_labels_y - (is_tall ? 4 : 2);
  int box_height = (s_target_y + 22) - box_y_coord + (is_tall ? 4 : 2);

  int center_x = (s_edit_mode == 0) ? (half_w / 2) : (half_w + (half_w / 2));
  GRect highlight_box = GRect(center_x - (s_highlight_box_width / 2), box_y_coord, s_highlight_box_width, box_height);
  graphics_draw_round_rect(ctx, highlight_box, 8); 
}

static void rest_bg_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorWhite));
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  
  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorLightGray, GColorDarkGray));
  graphics_context_set_stroke_width(ctx, 2); 
  graphics_draw_line(ctx, GPoint(0, 0), GPoint(bounds.size.w, 0));
}

static void update_workout_ui() {
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

// --- DYNAMIC "NEXT" LABEL LOGIC ---
  static char next_buf[64];
  bool is_second_half = (s_curr_ex_idx > 0 && s_exercises[s_curr_ex_idx - 1].modifier == 2);
  
  if (is_second_half && ex->current_set < ex->target_sets) {
      // If we are on the second half of a superset, we bounce BACK to the first half!
      snprintf(next_buf, sizeof(next_buf), "NEXT: %s", s_exercises[s_curr_ex_idx - 1].name);
  } else if (s_curr_ex_idx + 1 < s_total_exercises) {
      snprintf(next_buf, sizeof(next_buf), "NEXT: %s", s_exercises[s_curr_ex_idx + 1].name);
  } else {
      snprintf(next_buf, sizeof(next_buf), "NEXT: FINISH!");
  }
  text_layer_set_text(s_next_exercise_layer, next_buf);

  // --- DROP SET UI LOGIC ---
  int active_target_weight = ex->target_weight;
  static char set_buf[32];

  if (ex->modifier == 1 && (ex->current_set % 2 == 0)) {
      active_target_weight = (active_target_weight * (100 - s_drop_set_pct)) / 100;
      snprintf(set_buf, sizeof(set_buf), "Set %d of %d (DROP)", ex->current_set, ex->target_sets);
  } else {
      snprintf(set_buf, sizeof(set_buf), "Set %d of %d", ex->current_set, ex->target_sets);
  }
  text_layer_set_text(s_set_layer, set_buf);

  text_layer_set_text(s_label_reps_layer, "Reps");

  if (s_weight_unit_idx == 0) text_layer_set_text(s_label_weight_layer, "Weight (kg)");
  else text_layer_set_text(s_label_weight_layer, "Weight (lbs)");

  static char t_reps_buf[32], t_weight_buf[32], reps_buf[16], weight_buf[16];
  snprintf(t_reps_buf, sizeof(t_reps_buf), "Target: %d", ex->target_reps);
  snprintf(t_weight_buf, sizeof(t_weight_buf), "Target: %d", active_target_weight); 
  snprintf(reps_buf, sizeof(reps_buf), "%d", s_temp_reps);
  snprintf(weight_buf, sizeof(weight_buf), "%d", s_temp_weight);
  
  text_layer_set_text(s_target_reps_layer, t_reps_buf);
  text_layer_set_text(s_target_weight_layer, t_weight_buf);
  text_layer_set_text(s_actual_reps_layer, reps_buf);
  text_layer_set_text(s_actual_weight_layer, weight_buf);

  GColor active_color = get_theme_color();
  GColor inactive_color = PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack);

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
  
  int half_w = bounds.size.w / 2;
  if (s_highlight_box_width > half_w - 4) s_highlight_box_width = half_w - 4; 
  
  text_layer_set_text_color(s_rest_time_layer, active_color);
  layer_mark_dirty(s_progress_layer);
  layer_mark_dirty(s_workout_bg_layer);
}

static void skip_rest() {
  s_is_resting = false;
  layer_set_hidden(s_rest_overlay_layer, true);
  update_workout_ui();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  s_workout_sec++;
  int m = s_workout_sec / 60;
  int s = s_workout_sec % 60;
  static char time_buf[16];
  snprintf(time_buf, sizeof(time_buf), "%02d:%02d", m, s);
  text_layer_set_text(s_timer_layer, time_buf);

  static int s_last_minute = -1;
  if (tick_time->tm_min != s_last_minute) {
    static char clock_buf[16];
    if(clock_is_24h_style()) strftime(clock_buf, sizeof(clock_buf), "%H:%M", tick_time);
    else strftime(clock_buf, sizeof(clock_buf), "%I:%M", tick_time);
    text_layer_set_text(s_clock_layer, clock_buf);
    s_last_minute = tick_time->tm_min;
  }

  HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
  static char hr_buf[16];
  if (hr > 0) snprintf(hr_buf, sizeof(hr_buf), "%lu BPM", (uint32_t)hr);
  else snprintf(hr_buf, sizeof(hr_buf), "-- BPM");
  text_layer_set_text(s_hr_layer, hr_buf);

  if (s_is_resting) {
    s_rest_seconds_remaining--;
    if (s_rest_seconds_remaining <= 0) {
      skip_rest();
      play_vibe(s_rest_vibe); 
    } else {
      static char rest_buf[16];
      snprintf(rest_buf, sizeof(rest_buf), "%d", s_rest_seconds_remaining);
      text_layer_set_text(s_rest_time_layer, rest_buf);
    }
  }
}

static void wo_up_click(ClickRecognizerRef recognizer, void *context) {
  if (s_is_resting) return; 
  if (s_edit_mode == 0) s_temp_reps++; else s_temp_weight++;
  update_workout_ui();
}
static void wo_down_click(ClickRecognizerRef recognizer, void *context) {
  if (s_is_resting) return;
  if (s_edit_mode == 0 && s_temp_reps > 0) s_temp_reps--; 
  else if (s_edit_mode == 1 && s_temp_weight > 0) s_temp_weight--;
  update_workout_ui();
}
static void wo_select_short_click(ClickRecognizerRef recognizer, void *context) {
  if (s_is_resting) { skip_rest(); return; }
  s_edit_mode = !s_edit_mode; 
  update_workout_ui();
}

static void wo_select_long_click(ClickRecognizerRef recognizer, void *context) {
  if (s_is_resting) { skip_rest(); return; }

  Exercise *ex = &s_exercises[s_curr_ex_idx];
  ex->actual_reps[ex->current_set - 1] = s_temp_reps;
  ex->actual_weight[ex->current_set - 1] = s_temp_weight;

  // Identify our position in a potential Superset couple
  bool is_first_half = (ex->modifier == 2 && s_curr_ex_idx + 1 < s_total_exercises);
  bool is_second_half = (s_curr_ex_idx > 0 && s_exercises[s_curr_ex_idx - 1].modifier == 2);

  if (is_first_half) {
    // Finished Exercise A: Jump to Exercise B immediately!
    s_curr_ex_idx++;
    s_exercises[s_curr_ex_idx].current_set = ex->current_set; // Keep their sets perfectly synced
    s_rest_seconds_remaining = s_super_rest_sec;
    play_vibe(s_set_vibe);
    
  } else if (is_second_half) {
    // Finished Exercise B: Check if we need to bounce back to Exercise A
    if (ex->current_set < ex->target_sets) {
      ex->current_set++;
      s_exercises[s_curr_ex_idx - 1].current_set++; 
      s_curr_ex_idx--; // BOUNCE BACK!
      s_rest_seconds_remaining = s_set_rest_sec; // Standard rest after completing the couple
      play_vibe(s_set_vibe);
    } else {
      // Superset completely finished! Move on to Exercise C.
      s_curr_ex_idx++; 
      if (s_curr_ex_idx < s_total_exercises) {
        s_exercises[s_curr_ex_idx].current_set = 1;
        s_rest_seconds_remaining = s_ex_rest_sec; 
        play_vibe(s_ex_vibe);
      } else {
        vibes_double_pulse();
        push_summary_window(); 
        window_stack_remove(s_workout_window, false);
        return;
      }
    }
    
  } else {
    // NORMAL EXERCISE LOGIC (No Superset)
    if (ex->current_set < ex->target_sets) {
      ex->current_set++;
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
        push_summary_window(); 
        window_stack_remove(s_workout_window, false);
        return;
      }
    }
  }

  // Safely grab the newly selected exercise to pre-fill the screen variables
  Exercise *next_ex = &s_exercises[s_curr_ex_idx]; 
  s_temp_reps = next_ex->target_reps;
  
  int next_target_weight = next_ex->target_weight;
  if (next_ex->modifier == 1 && (next_ex->current_set % 2 == 0)) {
      next_target_weight = (next_target_weight * (100 - s_drop_set_pct)) / 100;
  }
  s_temp_weight = next_target_weight;
  
  s_edit_mode = 0; 
  
  if (s_rest_seconds_remaining > 0) {
    s_is_resting = true;
    layer_set_hidden(s_rest_overlay_layer, false);
  }
  update_workout_ui(); 
}

static void wo_select_double_click(ClickRecognizerRef recognizer, void *context) {
  if (s_is_resting) return;
  push_variation_window();
}

static void wo_click_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, wo_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, wo_down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, wo_select_short_click);
  
  window_multi_click_subscribe(BUTTON_ID_SELECT, 2, 0, 0, true, wo_select_double_click); 
  
  window_long_click_subscribe(BUTTON_ID_SELECT, s_long_press_ms, wo_select_long_click, NULL); 
}

static void workout_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);
  bool is_tall = (bounds.size.h > 180); 

  s_line1_y = is_tall ? 60 : 48; 
  s_line2_y = is_tall ? 195 : 144;
  int top_margin = is_tall ? 10 : 0;
  int half_w = bounds.size.w / 2;
  
  int set_y  = s_line1_y + (is_tall ? 5 : 0);
  s_labels_y = s_line1_y + (is_tall ? 40 : 24); 
  s_actual_y = s_labels_y + (is_tall ? 20 : 14);
  s_target_y = s_actual_y + (is_tall ? 36 : 30);

  s_progress_layer = layer_create(GRect(0, 0, bounds.size.w, 6));
  layer_set_update_proc(s_progress_layer, progress_update_proc);
  layer_add_child(w_layer, s_progress_layer);

  s_workout_bg_layer = layer_create(bounds);
  layer_set_update_proc(s_workout_bg_layer, workout_bg_update_proc);
  layer_add_child(w_layer, s_workout_bg_layer);

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

  s_set_layer = build_text_layer(GRect(0, set_y, bounds.size.w, 24), is_tall ? FONT_KEY_GOTHIC_24 : FONT_KEY_GOTHIC_18, GColorBlack, GTextAlignmentCenter, w_layer);
  s_label_reps_layer = build_text_layer(GRect(0, s_labels_y, half_w, 20), FONT_KEY_GOTHIC_14, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack), GTextAlignmentCenter, w_layer);
  s_label_weight_layer = build_text_layer(GRect(half_w, s_labels_y, half_w, 20), FONT_KEY_GOTHIC_14, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack), GTextAlignmentCenter, w_layer);
  s_actual_reps_layer = build_text_layer(GRect(0, s_actual_y, half_w, 40), FONT_KEY_BITHAM_30_BLACK, GColorBlack, GTextAlignmentCenter, w_layer);
  s_actual_weight_layer = build_text_layer(GRect(half_w, s_actual_y, half_w, 40), FONT_KEY_BITHAM_30_BLACK, GColorBlack, GTextAlignmentCenter, w_layer);
  s_target_reps_layer = build_text_layer(GRect(0, s_target_y, half_w, 22), FONT_KEY_GOTHIC_18, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack), GTextAlignmentCenter, w_layer);
  s_target_weight_layer = build_text_layer(GRect(half_w, s_target_y, half_w, 22), FONT_KEY_GOTHIC_18, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack), GTextAlignmentCenter, w_layer);

  int rest_box_height = s_line2_y - s_line1_y;
  s_rest_overlay_layer = layer_create(GRect(0, s_line1_y, bounds.size.w, rest_box_height));
  layer_set_update_proc(s_rest_overlay_layer, rest_bg_update_proc);
  
  int r_title_y = is_tall ? 10 : 5;
  int r_time_y  = is_tall ? 45 : 30;
  int r_skip_y  = is_tall ? 100 : 75;

  s_rest_title_layer = build_text_layer(GRect(0, r_title_y, bounds.size.w, 30), FONT_KEY_GOTHIC_28_BOLD, GColorBlack, GTextAlignmentCenter, s_rest_overlay_layer);
  text_layer_set_text(s_rest_title_layer, "REST");
  
  s_rest_time_layer = build_text_layer(GRect(0, r_time_y, bounds.size.w, 45), FONT_KEY_BITHAM_42_BOLD, PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack), GTextAlignmentCenter, s_rest_overlay_layer);
  
  s_rest_skip_layer = build_text_layer(GRect(0, r_skip_y, bounds.size.w, 20), FONT_KEY_GOTHIC_18, GColorBlack, GTextAlignmentCenter, s_rest_overlay_layer);
  text_layer_set_text(s_rest_skip_layer, "[Select] to Skip");
  
  layer_add_child(w_layer, s_rest_overlay_layer);
  layer_set_hidden(s_rest_overlay_layer, true); 

  s_curr_ex_idx = 0;
  s_workout_sec = 0;
  s_temp_reps = s_exercises[0].target_reps;
  s_temp_weight = s_exercises[0].target_weight;
  
  update_workout_ui();
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
}

static void workout_window_unload(Window *window) {
  tick_timer_service_unsubscribe();
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
  text_layer_destroy(s_hr_layer);

  layer_destroy(s_rest_overlay_layer);
  text_layer_destroy(s_rest_title_layer);
  text_layer_destroy(s_rest_time_layer);
  text_layer_destroy(s_rest_skip_layer);
}

// --- MAIN MENU WINDOW LOGIC ---
static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return (s_active_slots < MAX_SLOTS) ? s_active_slots + 2 : MAX_SLOTS + 1;
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  int i = cell_index->row;
  
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

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  int i = cell_index->row;
  
  if (i < s_active_slots) {
    char saved_data[256];
    persist_read_string(STORAGE_KEY_BASE + i, saved_data, sizeof(saved_data));
    parse_routine_string(saved_data);
    push_workout_window(); 
  } else if (i == s_active_slots && s_active_slots < MAX_SLOTS) {
    push_help_window(); 
  } else {
    push_settings_window(); 
  }
}

static void menu_select_long_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (cell_index->row < s_active_slots) {
    s_slot_to_edit = cell_index->row;
    s_target_swap_slot = cell_index->row;
    push_confirm_window(); 
  }
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *routine_data_tuple = dict_find(iterator, MESSAGE_KEY_ROUTINE_DATA);
  if (routine_data_tuple && routine_data_tuple->type == TUPLE_CSTRING) {
    char *delimited_string = routine_data_tuple->value->cstring;
    char safe_buffer[256]; 
    snprintf(safe_buffer, sizeof(safe_buffer), "%s", delimited_string);
    MenuIndex selected = menu_layer_get_selected_index(s_menu_layer);
    
    int target_slot = selected.row;
    if (target_slot > MAX_SLOTS - 1) target_slot = MAX_SLOTS - 1;
    
    persist_write_string(STORAGE_KEY_BASE + target_slot, safe_buffer);
    refresh_directory();
    menu_layer_reload_data(s_menu_layer);
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
  
  menu_layer_set_normal_colors(s_menu_layer, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_menu_layer, get_theme_color(), GColorWhite);
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
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
  
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) { .load = main_window_load, .unload = main_window_unload });
  window_stack_push(s_main_window, true);
  
  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(256, 512); 
}

static void deinit() {
  if (s_summary_window) window_destroy(s_summary_window);
  if (s_workout_window) window_destroy(s_workout_window);
  if (s_confirm_window) window_destroy(s_confirm_window);
  if (s_help_window) window_destroy(s_help_window);
  if (s_settings_window) window_destroy(s_settings_window);
  if (s_variation_window) window_destroy(s_variation_window);
  
  window_destroy(s_main_window); 
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}