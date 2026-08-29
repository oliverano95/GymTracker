#include <pebble.h>

// ========================================================================= //
//                            1. MACROS & DEFINES                            //
// ========================================================================= //
#define MAX_EXERCISES 15
#define MAX_SLOTS 7
#define EXPORT_BUF_SIZE 1024
#define CHUNK_BUF_SIZE 2048

#define STORAGE_KEY_BASE   100
#define SETTINGS_KEY_BASE  200
#define ACTIVE_STATE_KEY   300
#define ACTIVE_EX_BASE     400
#define ROUTINE_EX_BASE    1000
#define V5_MIGRATION_KEY   500
#define GHOST_KEY_BASE     800
#define LAST_COMPLETED_KEY_BASE 900

#define NUM_VARIATIONS 9

// Settings key offsets – named constants so inserting a new setting
// never silently remaps existing saved data.          FIX: named offsets
typedef enum {
  SK_SET_REST      =  0,
  SK_EX_REST       =  1,
  SK_SET_VIBE      =  2,
  SK_EX_VIBE       =  3,
  SK_REST_VIBE     =  4,
  SK_THEME_COLOR   =  5,
  SK_WEIGHT_UNIT   =  6,
  SK_LONG_PRESS    =  7,
  SK_DROP_PCT      =  8,
  SK_SUPER_REST    =  9,
  SK_PROG_MODE     = 10,
  SK_WEIGHT_INC    = 11,
  SK_DROP_REST     = 12,
  SK_LAST_SLOT     = 13,
  SK_DARK_MODE     = 14,
  SK_SHORTCUT_UP   = 15,
  SK_SHORTCUT_DOWN = 16,
  SK_SHORTCUT_SEL  = 17,
  SK_DYNAMIC_HR    = 18,
  SK_ENABLE_GHOST  = 19,
  SK_ENABLE_SENS   = 20,
  SK_NOTE_TOAST    = 21,
  SK_HR_RATE       = 22,
  SK_SWAP_UI       = 23,
  SK_PREFILL_MODE  = 24,
} SettingsKey;

// ========================================================================= //
//                           2. DATA STRUCTURES                              //
// ========================================================================= //
typedef struct {
  char name[32];
  int  total_exercises;
} RoutineHeader;

typedef struct {
  char routine_name[32];
  int  total_exercises;
  int  total_workout_sets;
  int  curr_ex_idx;
  int  workout_sec;
  int  current_slot;
  int  peak_hr;
  int  total_hr;
  int  hr_samples;
} ActiveState;

typedef struct {
  char name[32];
  char comment[32];
  int  target_weight;
  int  actual_weight[10];
  int  target_sets;
  int  target_reps;
  int  modifier;
  int  current_set;
  int  actual_reps[10];
  bool dirty;
} Exercise;

typedef struct {
  int set_rest_sec;
  int ex_rest_sec;
  int super_rest_sec;
  int drop_rest_sec;
  int set_vibe;
  int ex_vibe;
  int rest_vibe;
  int theme_color_idx;
  int weight_unit_idx;
  int long_press_ms;
  int drop_set_pct;
  int last_routine_slot;
  int dark_mode;
  int shortcut_up;
  int shortcut_down;
  int shortcut_select;
  int dynamic_hr_target;
  int progression_mode;
  int weight_increment;
  int enable_ghost;
  int enable_sensation;
  int hr_sample_rate;
  int swap_reps_weight;
  int note_toast_mode;
  int prefill_mode;
} AppSettings;

typedef struct {
  bool active;
  bool has_resume;
  bool is_paused;
  bool is_resting;
  bool is_timed_active;
  int rest_seconds_remaining;
  char routine_name[32];
  Exercise exercises[MAX_EXERCISES];
  int total_exercises;
  int total_workout_sets;
  int current_slot;
  int curr_ex_idx;
  int workout_sec;
  int peak_hr;
  int total_hr;
  int hr_samples;
  int last_workout_sec;
  int edit_mode;
  int temp_reps;
  int temp_weight;
  int sensation;
  int adding_exercise_to_slot;
  int cached_completed_sets; // OPT #4: cached to avoid per-second loop
  bool hr_supported;         // OPT #2: checked once at workout start
  bool is_24h;               // OPT #8: cached once at workout start
  DictationSession *dictation_session;
  DictationSession *new_ex_dictation_session;
  Exercise pending_exercise;
  int note_toast_prev_ex_idx;
} WorkoutState;

typedef struct {
  char slot_names[MAX_SLOTS][32];
  int  slot_counts[MAX_SLOTS];
  int  last_completed[MAX_SLOTS];
  int  active_slots;
  int  slot_to_edit;
  int  target_swap_slot;
  int  exercise_to_edit;
} AppStorage;

typedef struct {
  int line1_y;
  int line2_y;
  int labels_y;
  int actual_y;
  int target_y;
  int highlight_box_width;
} UIGeometry;

typedef struct {
  Window *main_window, *settings_window, *workout_window, *help_window, *confirm_window;
  Window *summary_window, *variation_window, *exit_window, *sensation_window;
  Window *action_window, *inspector_window, *mega_window;
  Window *confirm_add_window, *ex_action_window;

  MenuLayer *menu_layer, *settings_menu_layer, *variation_menu_layer;
  MenuLayer *sensation_menu_layer, *inspector_menu_layer;
  SimpleMenuLayer *action_menu_layer, *mega_menu_layer, *ex_action_menu_layer;

  Layer *main_header_bg, *settings_header_bg, *progress_layer, *workout_bg_layer;
  Layer *summary_bg_layer, *rest_overlay_layer, *highlight_layer;
  Layer *note_toast_layer, *note_toast_text_layer, *note_toast_acc_layer;

  Animation *box_anim;
  Animation *rest_anim;
  Animation *note_toast_anim;

  AppTimer *note_toast_dismiss_timer;
  AppTimer *note_toast_scroll_timer;
  char note_toast_text[32];
  int16_t note_toast_text_width;
  int16_t note_toast_scroll_offset;
  int16_t note_toast_scroll_pause_until;
  bool note_toast_visible;

  TextLayer *main_header_text, *settings_header_text, *timer_layer, *clock_layer;
  TextLayer *exercise_layer, *next_exercise_layer, *set_layer, *label_reps_layer;
  TextLayer *label_weight_layer, *target_reps_layer, *target_weight_layer;
  TextLayer *actual_reps_layer, *actual_weight_layer, *help_text_layer;

  #if !defined(PBL_ROUND)
  TextLayer *hr_layer;
  #endif

  TextLayer *confirm_text_layer, *sum_title_layer, *sum_info_layer, *exit_text_layer;
  TextLayer *rest_title_layer, *rest_time_layer, *rest_skip_layer, *rest_prep_layer;
  TextLayer *beat_layer, *missed_layer, *accuracy_layer, *density_layer;
  TextLayer *confirm_add_text_layer;

  SimpleMenuSection action_menu_sections[1];
  SimpleMenuItem action_menu_items[4];
  SimpleMenuSection mega_menu_sections[1];
  SimpleMenuItem mega_menu_items[9];
  SimpleMenuSection ex_action_menu_sections[1];
  SimpleMenuItem ex_action_menu_items[3];
} AppUI;

typedef struct {
  AppSettings settings;
  WorkoutState state;
  AppStorage   storage;
  UIGeometry   geom;
  AppUI        ui;
} GymTrackerApp;

// ========================================================================= //
//                            3. GLOBAL STATE                                //
// ========================================================================= //
static GymTrackerApp s_app = {
  .settings = {
    .set_rest_sec = 60, .ex_rest_sec = 90, .super_rest_sec = 15, .drop_rest_sec = 5,
    .set_vibe = 1, .ex_vibe = 3, .rest_vibe = 2, .theme_color_idx = 0, .weight_unit_idx = 0,
    .long_press_ms = 500, .drop_set_pct = 20, .last_routine_slot = 0, .dark_mode = 0,
    .shortcut_up = 1, .shortcut_down = 2, .shortcut_select = 4, .dynamic_hr_target = 0,
    .progression_mode = -1, .weight_increment = 2, .enable_ghost = 1, .enable_sensation = 1,
    .hr_sample_rate = 1, .swap_reps_weight = 0, .note_toast_mode = 0, .prefill_mode = 0
  },
  .state = {
    .active = false, .has_resume = false, .is_paused = false, .is_resting = false,
    .is_timed_active = false, .rest_seconds_remaining = 0, .routine_name = "No Routine",
    .total_exercises = 0, .total_workout_sets = 0, .current_slot = 0, .curr_ex_idx = 0,
    .workout_sec = 0, .peak_hr = 0, .total_hr = 0, .hr_samples = 0, .last_workout_sec = 0,
    .edit_mode = 0, .temp_reps = 0, .temp_weight = 0, .sensation = 3,
    .dictation_session = NULL
  },
  .storage = {
    .active_slots = 0, .slot_to_edit = -1, .target_swap_slot = -1
  },
  .geom = {
    .line1_y = 0, .line2_y = 0, .labels_y = 0, .actual_y = 0, .target_y = 0,
    .highlight_box_width = 0
  }
};

static char s_shared_text_buffer[256];
static const char *s_sensation_titles[] = {"Unstoppable", "Strong", "Normal", "Exhausted", "Struggled"};
static const char *s_sensation_subs[]   = {"Felt amazing!", "Hit targets well", "Got work done", "Completely drained", "Felt weak/off"};
static const char *s_variations[]       = {
  "None (Clear)", "(Seated)", "(Standing)", "(Dumbbell)", "(Barbell)",
  "(Cable)", "(Machine)", "(Single Arm)", "(Single Leg)"
};

static int s_timer_edit_row = 0;
static int s_timer_edit_val = 0;
static Window *s_timer_edit_window = NULL;
static TextLayer *s_timer_title_layer;
static TextLayer *s_timer_value_layer;
static TextLayer *s_timer_hints_layer;

// ========================================================================= //
//                        4. FORWARD DECLARATIONS                            //
// ========================================================================= //
static void push_settings_window(void);
static void push_help_window(void);
static void push_confirm_window(void);
static void push_exit_window(void);
static void push_sensation_window(void);
static void push_summary_window(void);
static void push_variation_window(void);
static void push_inspector_window(void);
static void push_action_window(void);
static void push_workout_window(void);
static void push_mega_window(void);

static void set_rest_overlay_state(bool is_resting, bool animated);
static void tick_handler(struct tm *tick_time, TimeUnits units_changed);
static void update_workout_ui(bool animate_box);
static void skip_rest(void);
static void swap_exercise(void);
static void perform_true_skip(void);
static void perform_finish_set(void);
static void perform_skip_set(void);
static void perform_voice_note(void);
static void execute_shortcut(int action_idx);
static void start_workout_from_slot(int slot_idx);
static void init_temp_values(Exercise *ex);

static void show_note_toast(void);
static void hide_note_toast(bool animated);
static void note_toast_dismiss_cb(void *ctx);
static void note_toast_scroll_cb(void *ctx);
static void update_note_toast_visibility(void);
static void note_toast_text_update_proc(Layer *layer, GContext *ctx);
static void note_toast_bg_update_proc(Layer *layer, GContext *ctx);
static void note_toast_acc_update_proc(Layer *layer, GContext *ctx);
static void note_toast_anim_stopped_handler(Animation *animation, bool finished, void *ctx);
static void trigger_manual_note_toast(void);

static void settings_window_load(Window *window);
static void settings_window_unload(Window *window);
static void help_window_load(Window *window);
static void help_window_unload(Window *window);
static void confirm_window_load(Window *window);
static void confirm_window_unload(Window *window);
static void confirm_click_provider(void *context);
static void exit_window_load(Window *window);
static void exit_window_unload(Window *window);
static void exit_click_provider(void *context);
static void sensation_window_load(Window *window);
static void sensation_window_unload(Window *window);
static void summary_window_load(Window *window);
static void summary_window_unload(Window *window);
static void summary_click_provider(void *context);
static void variation_window_load(Window *window);
static void variation_window_unload(Window *window);
static void inspector_window_load(Window *window);
static void inspector_window_unload(Window *window);
static void action_window_load(Window *window);
static void action_window_unload(Window *window);
static void mega_window_load(Window *window);
static void mega_window_unload(Window *window);
static void workout_window_load(Window *window);
static void workout_window_unload(Window *window);
static void wo_click_provider(void *context);
static void main_window_load(Window *window);
static void main_window_unload(Window *window);
static void menu_select_long_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data);
static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data);
static void menu_draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data);
static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data);
static void inbox_received_callback(DictionaryIterator *iterator, void *context);
static int  get_completed_sets(void);
static void refresh_directory(void);
static void save_routine_to_slot(int slot_idx);

static void new_exercise_dictation_callback(DictationSession *session, DictationSessionStatus status, char *transcription, void *context);
static void action_quick_add_callback(int index, void *ctx);
static void menu_quick_add_callback(int index, void *ctx);
static void push_confirm_add_window(void);
static void push_ex_action_window(void);
static void push_timer_edit_window(void);

// ========================================================================= //
//                           5. CORE UTILITIES                               //
// ========================================================================= //

// FIX #11: Cache dark-theme result, updated once per minute by tick_handler.
static bool s_dark_theme_cache = false;

static void update_dark_theme_cache(struct tm *tick_time) {
  if (s_app.settings.dark_mode == 1) {
    s_dark_theme_cache = true;
  } else if (s_app.settings.dark_mode == 2) {
    s_dark_theme_cache = tick_time
      ? (tick_time->tm_hour >= 20 || tick_time->tm_hour <= 7)
      : false;
  } else {
    s_dark_theme_cache = false;
  }
}

// is_dark_theme() now reads the cache – safe to call from draw callbacks.
static bool is_dark_theme(void) {
  return s_dark_theme_cache;
}

static GColor get_bg_color(void)   { return is_dark_theme() ? GColorBlack : GColorWhite; }
static GColor get_text_color(void) { return is_dark_theme() ? GColorWhite : GColorBlack; }

static TextLayer *build_text_layer(GRect bounds, const char *font_key, GColor text_color,
                                   GTextAlignment alignment, Layer *parent) {
  TextLayer *tl = text_layer_create(bounds);
  text_layer_set_font(tl, fonts_get_system_font(font_key));

  if (is_dark_theme()) {
    if (gcolor_equal(text_color, GColorBlack))
      text_color = GColorWhite;
    else if (gcolor_equal(text_color, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack)))
      text_color = PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite);
    else if (gcolor_equal(text_color, PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorBlack)))
      text_color = PBL_IF_COLOR_ELSE(GColorPictonBlue, GColorWhite);
  }

  text_layer_set_text_color(tl, text_color);
  text_layer_set_background_color(tl, GColorClear);
  text_layer_set_text_alignment(tl, alignment);
  text_layer_set_overflow_mode(tl, GTextOverflowModeTrailingEllipsis);

  if (parent) layer_add_child(parent, text_layer_get_layer(tl));
  return tl;
}

static GColor get_theme_color(void) {
#if !defined(PBL_COLOR)
  return is_dark_theme() ? GColorWhite : GColorBlack;
#else
  if (s_app.settings.theme_color_idx >= 4) {
    uint8_t color_val = (uint8_t)(s_app.settings.theme_color_idx - 4);
    return (GColor){ .argb = (uint8_t)(0b11000000 | color_val) };
  }
  if (s_app.settings.theme_color_idx == 1) return GColorCobaltBlue;
  if (s_app.settings.theme_color_idx == 2) return GColorRed;
  if (s_app.settings.theme_color_idx == 3) return GColorIslamicGreen;
  return GColorOrange;
#endif
}

static void play_vibe(int type) {
  if      (type == 1) vibes_short_pulse();
  else if (type == 2) vibes_long_pulse();
  else if (type == 3) vibes_double_pulse();
}

// ========================================================================= //
//                           6. STORAGE & DATA                               //
// ========================================================================= //
static void load_settings(void) {
  if (persist_exists(SETTINGS_KEY_BASE + SK_SET_REST))    s_app.settings.set_rest_sec     = persist_read_int(SETTINGS_KEY_BASE + SK_SET_REST);
  if (persist_exists(SETTINGS_KEY_BASE + SK_EX_REST))     s_app.settings.ex_rest_sec      = persist_read_int(SETTINGS_KEY_BASE + SK_EX_REST);
  if (persist_exists(SETTINGS_KEY_BASE + SK_SET_VIBE))    s_app.settings.set_vibe         = persist_read_int(SETTINGS_KEY_BASE + SK_SET_VIBE);
  if (persist_exists(SETTINGS_KEY_BASE + SK_EX_VIBE))     s_app.settings.ex_vibe          = persist_read_int(SETTINGS_KEY_BASE + SK_EX_VIBE);
  if (persist_exists(SETTINGS_KEY_BASE + SK_REST_VIBE))   s_app.settings.rest_vibe        = persist_read_int(SETTINGS_KEY_BASE + SK_REST_VIBE);
  if (persist_exists(SETTINGS_KEY_BASE + SK_THEME_COLOR)) s_app.settings.theme_color_idx  = persist_read_int(SETTINGS_KEY_BASE + SK_THEME_COLOR);
  if (persist_exists(SETTINGS_KEY_BASE + SK_WEIGHT_UNIT)) s_app.settings.weight_unit_idx  = persist_read_int(SETTINGS_KEY_BASE + SK_WEIGHT_UNIT);
  if (persist_exists(SETTINGS_KEY_BASE + SK_LONG_PRESS))  s_app.settings.long_press_ms    = persist_read_int(SETTINGS_KEY_BASE + SK_LONG_PRESS);
  if (persist_exists(SETTINGS_KEY_BASE + SK_DROP_PCT))    s_app.settings.drop_set_pct     = persist_read_int(SETTINGS_KEY_BASE + SK_DROP_PCT);
  if (persist_exists(SETTINGS_KEY_BASE + SK_SUPER_REST))  s_app.settings.super_rest_sec   = persist_read_int(SETTINGS_KEY_BASE + SK_SUPER_REST);
  if (persist_exists(SETTINGS_KEY_BASE + SK_PROG_MODE))   s_app.settings.progression_mode = persist_read_int(SETTINGS_KEY_BASE + SK_PROG_MODE);
  if (persist_exists(SETTINGS_KEY_BASE + SK_WEIGHT_INC))  s_app.settings.weight_increment = persist_read_int(SETTINGS_KEY_BASE + SK_WEIGHT_INC);
  if (persist_exists(SETTINGS_KEY_BASE + SK_DROP_REST))   s_app.settings.drop_rest_sec    = persist_read_int(SETTINGS_KEY_BASE + SK_DROP_REST);
  if (persist_exists(SETTINGS_KEY_BASE + SK_LAST_SLOT))   s_app.settings.last_routine_slot= persist_read_int(SETTINGS_KEY_BASE + SK_LAST_SLOT);
  if (persist_exists(SETTINGS_KEY_BASE + SK_DARK_MODE))   s_app.settings.dark_mode        = persist_read_int(SETTINGS_KEY_BASE + SK_DARK_MODE);
  if (persist_exists(SETTINGS_KEY_BASE + SK_SHORTCUT_UP)) s_app.settings.shortcut_up      = persist_read_int(SETTINGS_KEY_BASE + SK_SHORTCUT_UP);
  if (persist_exists(SETTINGS_KEY_BASE + SK_SHORTCUT_DOWN))s_app.settings.shortcut_down   = persist_read_int(SETTINGS_KEY_BASE + SK_SHORTCUT_DOWN);
  if (persist_exists(SETTINGS_KEY_BASE + SK_SHORTCUT_SEL))s_app.settings.shortcut_select  = persist_read_int(SETTINGS_KEY_BASE + SK_SHORTCUT_SEL);
  if (persist_exists(SETTINGS_KEY_BASE + SK_DYNAMIC_HR))  s_app.settings.dynamic_hr_target= persist_read_int(SETTINGS_KEY_BASE + SK_DYNAMIC_HR);
  if (persist_exists(SETTINGS_KEY_BASE + SK_ENABLE_GHOST))s_app.settings.enable_ghost     = persist_read_int(SETTINGS_KEY_BASE + SK_ENABLE_GHOST);
  if (persist_exists(SETTINGS_KEY_BASE + SK_ENABLE_SENS)) s_app.settings.enable_sensation = persist_read_int(SETTINGS_KEY_BASE + SK_ENABLE_SENS);
  if (persist_exists(SETTINGS_KEY_BASE + SK_HR_RATE))     s_app.settings.hr_sample_rate   = persist_read_int(SETTINGS_KEY_BASE + SK_HR_RATE);
  if (persist_exists(SETTINGS_KEY_BASE + SK_SWAP_UI))     s_app.settings.swap_reps_weight = persist_read_int(SETTINGS_KEY_BASE + SK_SWAP_UI);
  if (persist_exists(SETTINGS_KEY_BASE + SK_NOTE_TOAST))  s_app.settings.note_toast_mode  = persist_read_int(SETTINGS_KEY_BASE + SK_NOTE_TOAST);
  if (persist_exists(SETTINGS_KEY_BASE + SK_PREFILL_MODE))  s_app.settings.prefill_mode   = persist_read_int(SETTINGS_KEY_BASE + SK_PREFILL_MODE);
}

static void save_setting(SettingsKey key, int value) {
  persist_write_int(SETTINGS_KEY_BASE + (int)key, value);
}

static void parse_routine_string(const char *data) {
  s_app.state.total_exercises = 0;
  memset(&s_app.state.exercises, 0, sizeof(s_app.state.exercises));
  if (!data) return;

  int i = 0, token_count = 0, t_idx = 0;
  char temp[32];

  while (true) {
    if (data[i] == '|' || data[i] == '\0') {
      temp[t_idx] = '\0';
      if (token_count == 0) {
        snprintf(s_app.state.routine_name, sizeof(s_app.state.routine_name), "%s", temp);
      } else {
        int ex_idx = (token_count - 1) / 6;
        int field  = (token_count - 1) % 6;

        if (ex_idx < MAX_EXERCISES) {
          Exercise *ex = &s_app.state.exercises[ex_idx];
          if      (field == 0) snprintf(ex->name,    sizeof(ex->name),    "%s", temp);
          else if (field == 1) ex->target_sets   = atoi(temp);
          else if (field == 2) ex->target_reps   = atoi(temp);
          else if (field == 3) ex->target_weight = atoi(temp);
          else if (field == 4) {
            ex->modifier = atoi(temp);
            if (ex->modifier == 4) { ex->modifier = 0; ex->target_weight = 0; }
            if (ex->modifier == 6) { ex->target_weight = 0; }
            if (ex->modifier == 1) {
              ex->target_sets *= 2;
            }
            // actual_reps/actual_weight hold at most 10 sets — clamp every
            // modifier, not just drop sets, or set 11+ corrupts memory.
            if (ex->target_sets > 10) ex->target_sets = 10;
            if (ex->target_sets < 1)  ex->target_sets = 1;
            ex->current_set = 1;
          } else if (field == 5) {
            if (strcmp(temp, "-") == 0) {
              ex->comment[0] = '\0';
            } else {
              snprintf(ex->comment, sizeof(ex->comment), "%s", temp);
            }
            s_app.state.total_exercises = ex_idx + 1;
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

  s_app.state.total_workout_sets = 0;
  for (int k = 0; k < s_app.state.total_exercises; k++)
    s_app.state.total_workout_sets += s_app.state.exercises[k].target_sets;
}

static void format_relative_time(char *buf, size_t size, int timestamp) {
  int diff = (int)time(NULL) - timestamp;
  if      (diff < 60)       snprintf(buf, size, "just now");
  else if (diff < 3600)     snprintf(buf, size, "%dm ago", diff / 60);
  else if (diff < 86400)    snprintf(buf, size, "%dh ago", diff / 3600);
  else if (diff < 604800)   snprintf(buf, size, "%dd ago", diff / 86400);
  else if (diff < 2592000)  snprintf(buf, size, "%dw ago", diff / 604800);
  else                      snprintf(buf, size, "%dmo ago", diff / 2592000);
}

// Move an optional per-slot persisted int (last-completed timestamp,
// ghost-pacer duration) from one slot to another, deleting the source.
static void move_slot_int(int key_base, int from_slot, int to_slot) {
  if (persist_exists(key_base + from_slot)) {
    persist_write_int(key_base + to_slot, persist_read_int(key_base + from_slot));
    persist_delete(key_base + from_slot);
  }
}

// Swap an optional per-slot persisted int between two slots. b_may_exist
// gates whether the target slot's value is treated as valid (false when the
// target slot holds no routine).
static void swap_slot_int(int key_base, int slot_a, int slot_b, bool b_may_exist) {
  if (slot_a == slot_b) return; // CRITICAL FIX: Prevent self-deletion
  
  bool has_a = persist_exists(key_base + slot_a);
  bool has_b = b_may_exist && persist_exists(key_base + slot_b);
  int  val_a = has_a ? persist_read_int(key_base + slot_a) : 0;
  int  val_b = has_b ? persist_read_int(key_base + slot_b) : 0;

  if (has_a) persist_write_int(key_base + slot_b, val_a);
  else       persist_delete(key_base + slot_b);

  if (has_b) persist_write_int(key_base + slot_a, val_b);
  else       persist_delete(key_base + slot_a);
}

static void refresh_directory(void) {
  s_app.storage.active_slots = 0;
  RoutineHeader header;

  for (int i = 0; i < MAX_SLOTS; i++) {
    if (persist_exists(STORAGE_KEY_BASE + i)) {
      persist_read_data(STORAGE_KEY_BASE + i, &header, sizeof(RoutineHeader));

      if (i != s_app.storage.active_slots) {
        persist_write_data(STORAGE_KEY_BASE + s_app.storage.active_slots, &header, sizeof(RoutineHeader));
        for (int j = 0; j < header.total_exercises; j++) {
          Exercise temp_ex;
          if (persist_read_data(ROUTINE_EX_BASE + (i * MAX_EXERCISES) + j, &temp_ex, sizeof(Exercise)) > 0)
            persist_write_data(ROUTINE_EX_BASE + (s_app.storage.active_slots * MAX_EXERCISES) + j, &temp_ex, sizeof(Exercise));
          persist_delete(ROUTINE_EX_BASE + (i * MAX_EXERCISES) + j);
        }
        persist_delete(STORAGE_KEY_BASE + i);
        move_slot_int(LAST_COMPLETED_KEY_BASE, i, s_app.storage.active_slots);
        move_slot_int(GHOST_KEY_BASE,          i, s_app.storage.active_slots);
      }

      snprintf(s_app.storage.slot_names[s_app.storage.active_slots],
               sizeof(s_app.storage.slot_names[s_app.storage.active_slots]),
               "%s", header.name);
      s_app.storage.slot_counts[s_app.storage.active_slots] = header.total_exercises;
      s_app.storage.last_completed[s_app.storage.active_slots] =
        persist_exists(LAST_COMPLETED_KEY_BASE + s_app.storage.active_slots)
        ? persist_read_int(LAST_COMPLETED_KEY_BASE + s_app.storage.active_slots) : 0;
      s_app.storage.active_slots++;
    }
  }

  for (int i = s_app.storage.active_slots; i < MAX_SLOTS; i++) {
    snprintf(s_app.storage.slot_names[i], sizeof(s_app.storage.slot_names[i]), "Empty Slot");
    s_app.storage.slot_counts[i] = 0;
    s_app.storage.last_completed[i] = 0;
  }
}

static void save_routine_to_slot(int slot_idx) {
  RoutineHeader header;
  snprintf(header.name, sizeof(header.name), "%s", s_app.state.routine_name);
  header.total_exercises = s_app.state.total_exercises;
  persist_write_data(STORAGE_KEY_BASE + slot_idx, &header, sizeof(RoutineHeader));

  for (int j = 0; j < s_app.state.total_exercises; j++)
    persist_write_data(ROUTINE_EX_BASE + (slot_idx * MAX_EXERCISES) + j,
                       &s_app.state.exercises[j], sizeof(Exercise));
}

// ========================================================================= //
//                          7. WORKOUT ENGINE                                //
// ========================================================================= //
static int get_completed_sets(void) {
  int completed = 0;
  // Determine whether the current exercise is part of a giant set (modifier 7)
  // and, if so, the anchor index of that trio.
  int giant_anchor = -1;
  if (s_app.state.curr_ex_idx < s_app.state.total_exercises &&
      s_app.state.exercises[s_app.state.curr_ex_idx].modifier == 7 &&
      s_app.state.curr_ex_idx + 2 < s_app.state.total_exercises) {
    giant_anchor = s_app.state.curr_ex_idx;
  } else if (s_app.state.curr_ex_idx >= 1 &&
             s_app.state.curr_ex_idx - 1 < s_app.state.total_exercises &&
             s_app.state.exercises[s_app.state.curr_ex_idx - 1].modifier == 7 &&
             s_app.state.curr_ex_idx + 1 < s_app.state.total_exercises) {
    giant_anchor = s_app.state.curr_ex_idx - 1;
  } else if (s_app.state.curr_ex_idx >= 2 &&
             s_app.state.curr_ex_idx - 2 < s_app.state.total_exercises &&
             s_app.state.exercises[s_app.state.curr_ex_idx - 2].modifier == 7) {
    giant_anchor = s_app.state.curr_ex_idx - 2;
  }

  for (int i = 0; i < s_app.state.total_exercises; i++) {
    bool in_giant = (giant_anchor >= 0 && i >= giant_anchor && i < giant_anchor + 3);
    if (i < s_app.state.curr_ex_idx) {
      // Past exercise. For a superset's first half OR any past giant-set
      // member, current_set holds the count of finished sets (because the
      // increment happens at the lap end, when we move on to the next set
      // on every member). Use it directly.
      if (in_giant) {
        completed += s_app.state.exercises[i].current_set;
      } else if (s_app.state.exercises[i].modifier == 2 && i == s_app.state.curr_ex_idx - 1) {
        completed += s_app.state.exercises[i].current_set;
      } else {
        completed += s_app.state.exercises[i].target_sets;
      }
    } else if (i == s_app.state.curr_ex_idx) {
      // Current exercise: user is on set current_set (0..N-1 already done).
      completed += (s_app.state.exercises[i].current_set - 1);
    } else {
      // Future exercise. For giant-set members that are still in the same
      // trio, or a superset's second half, current_set holds the same
      // "set number we're on" — the user has done current_set - 1 of them.
      if (in_giant) {
        completed += (s_app.state.exercises[i].current_set - 1);
      } else if (i == s_app.state.curr_ex_idx + 1 &&
                 s_app.state.exercises[s_app.state.curr_ex_idx].modifier == 2) {
        completed += (s_app.state.exercises[i].current_set - 1);
      }
    }
  }
  return completed;
}

static void init_temp_values(Exercise *ex) {
  s_app.state.is_timed_active = false;
  
  int base_reps = ex->target_reps;
  int base_weight = ex->target_weight;

  // Evaluate the user's Pre-fill Setting
  if (s_app.settings.prefill_mode == 2) {
    // Mode 2: Always start at Zero
    base_reps = 0;
    base_weight = 0;
  } else if (s_app.settings.prefill_mode == 1) {
    // Mode 1: Safely peek at History (Flash Memory), fallback to Target if 0
    Exercise hist_ex;
    
    // Loop through flash memory to find the matching exercise by name (safeguards against live swapping)
    for (int i = 0; i < s_app.state.total_exercises; i++) {
      if (persist_read_data(ROUTINE_EX_BASE + (s_app.state.current_slot * MAX_EXERCISES) + i, &hist_ex, sizeof(Exercise)) > 0) {
        if (strcmp(hist_ex.name, ex->name) == 0) {
          int hist_reps   = hist_ex.actual_reps[ex->current_set - 1];
          int hist_weight = hist_ex.actual_weight[ex->current_set - 1];
          if (hist_reps > 0) base_reps = hist_reps;
          if (hist_weight > 0) base_weight = hist_weight;
          break; // Match found, stop reading flash memory
        }
      }
    }
  }

  // Modifiers and Final Assignment
  if (ex->modifier == 6) {
    s_app.state.temp_reps = 0;
  } else {
    s_app.state.temp_reps = base_reps;
    
    // Drop Set modifier overrides standard pre-fill
    if (ex->modifier == 1 && (ex->current_set % 2 == 0) && ex->target_weight == 0) {
      s_app.state.temp_reps = ex->actual_reps[ex->current_set - 2];
      s_app.state.temp_reps = (s_app.state.temp_reps * (100 - s_app.settings.drop_set_pct)) / 100;
    }
  }

  int active_weight = base_weight;
  
  // Drop Set modifier overrides standard pre-fill
  if (ex->modifier == 1 && (ex->current_set % 2 == 0) && ex->target_weight != 0) {
    active_weight = ex->actual_weight[ex->current_set - 2];
    active_weight = (active_weight * (100 - s_app.settings.drop_set_pct)) / 100;
  }
  
  s_app.state.temp_weight = active_weight;
}

static void skip_rest(void) {
  s_app.state.is_resting = false;
  set_rest_overlay_state(false, true);
  update_workout_ui(false);
}

static void swap_exercise(void) {
  if (s_app.state.is_resting) return;
  if (s_app.state.curr_ex_idx + 1 >= s_app.state.total_exercises) return;

  int curr_idx = s_app.state.curr_ex_idx;
  int next_idx = curr_idx + 1;

  // Check if the CURRENT exercise is ANY part of a linked set (Anchor, 2nd, or 3rd member)
  bool curr_is_linked = (
      s_app.state.exercises[curr_idx].modifier == 2 ||
      s_app.state.exercises[curr_idx].modifier == 7 ||
      (curr_idx > 0 && s_app.state.exercises[curr_idx - 1].modifier == 2) ||
      (curr_idx > 0 && s_app.state.exercises[curr_idx - 1].modifier == 7) ||
      (curr_idx > 1 && s_app.state.exercises[curr_idx - 2].modifier == 7)
  );

  // Check if the NEXT exercise is the start (anchor) of a linked set
  bool next_is_anchor = (
      s_app.state.exercises[next_idx].modifier == 2 ||
      s_app.state.exercises[next_idx].modifier == 7
  );

  // Disallow reordering if it breaks a Superset or Giant Set structure
  if (curr_is_linked || next_is_anchor) {
    vibes_double_pulse();
    return;
  }

  Exercise temp = s_app.state.exercises[curr_idx];
  s_app.state.exercises[curr_idx] = s_app.state.exercises[next_idx];
  s_app.state.exercises[next_idx] = temp;

  init_temp_values(&s_app.state.exercises[curr_idx]);
  update_workout_ui(true);
}

static void perform_true_skip(void) {
  if (s_app.state.curr_ex_idx >= s_app.state.total_exercises) return;

  Exercise *ex = &s_app.state.exercises[s_app.state.curr_ex_idx];
  bool is_first_half  = (ex->modifier == 2 && s_app.state.curr_ex_idx + 1 < s_app.state.total_exercises);
  bool is_second_half = (s_app.state.curr_ex_idx > 0 &&
                         s_app.state.exercises[s_app.state.curr_ex_idx - 1].modifier == 2);

  // Detect giant-set membership. If the current exercise is part of a
  // giant-set trio, mark all 3 members as fully complete and skip past them.
  int giant_anchor = -1;
  if (ex->modifier == 7 && s_app.state.curr_ex_idx + 2 < s_app.state.total_exercises) {
    giant_anchor = s_app.state.curr_ex_idx;
  } else if (s_app.state.curr_ex_idx > 0 &&
             s_app.state.exercises[s_app.state.curr_ex_idx - 1].modifier == 7 &&
             s_app.state.curr_ex_idx + 1 < s_app.state.total_exercises) {
    giant_anchor = s_app.state.curr_ex_idx - 1;
  } else if (s_app.state.curr_ex_idx >= 2 &&
             s_app.state.exercises[s_app.state.curr_ex_idx - 2].modifier == 7) {
    giant_anchor = s_app.state.curr_ex_idx - 2;
  }

  if (giant_anchor >= 0) {
    for (int i = giant_anchor; i < giant_anchor + 3; i++) {
      Exercise *member = &s_app.state.exercises[i];
      for (int j = member->current_set - 1; j < member->target_sets; j++) {
        member->actual_reps[j] = 0; member->actual_weight[j] = 0;
      }
      member->current_set = member->target_sets;
    }
    s_app.state.curr_ex_idx = giant_anchor + 3;
    if (s_app.state.curr_ex_idx < s_app.state.total_exercises) {
      init_temp_values(&s_app.state.exercises[s_app.state.curr_ex_idx]);
      s_app.state.cached_completed_sets = get_completed_sets();
      s_app.state.is_resting = false;
      layer_set_hidden(s_app.ui.rest_overlay_layer, true);
      s_app.state.edit_mode = (s_app.state.exercises[s_app.state.curr_ex_idx].target_weight == 0) ? 0 : s_app.settings.swap_reps_weight;
      layer_mark_dirty(s_app.ui.progress_layer);
      update_workout_ui(true);
    } else {
      window_stack_remove(s_app.ui.workout_window, false);
      vibes_double_pulse();
      if (s_app.settings.enable_sensation) push_sensation_window();
      else push_summary_window();
    }
    return;
  }

  for (int i = ex->current_set - 1; i < ex->target_sets; i++) {
    ex->actual_reps[i] = 0; ex->actual_weight[i] = 0;
  }
  ex->current_set = ex->target_sets;

  if (is_first_half) {
    Exercise *next_ex = &s_app.state.exercises[s_app.state.curr_ex_idx + 1];
    for (int i = next_ex->current_set - 1; i < next_ex->target_sets; i++) {
      next_ex->actual_reps[i] = 0; next_ex->actual_weight[i] = 0;
    }
    next_ex->current_set = next_ex->target_sets;
    s_app.state.curr_ex_idx++;
  } else if (is_second_half) {
    Exercise *prev_ex = &s_app.state.exercises[s_app.state.curr_ex_idx - 1];
    for (int i = prev_ex->current_set - 1; i < prev_ex->target_sets; i++) {
      prev_ex->actual_reps[i] = 0; prev_ex->actual_weight[i] = 0;
    }
    prev_ex->current_set = prev_ex->target_sets;
  }

  if (s_app.state.curr_ex_idx + 1 < s_app.state.total_exercises) {
    s_app.state.curr_ex_idx++;
    init_temp_values(&s_app.state.exercises[s_app.state.curr_ex_idx]);
    s_app.state.cached_completed_sets = get_completed_sets();
    s_app.state.is_resting = false;
    layer_set_hidden(s_app.ui.rest_overlay_layer, true);
    s_app.state.edit_mode = (s_app.state.exercises[s_app.state.curr_ex_idx].target_weight == 0) ? 0 : s_app.settings.swap_reps_weight;
    layer_mark_dirty(s_app.ui.progress_layer);
    update_workout_ui(true);
  } else {
    window_stack_remove(s_app.ui.workout_window, false);
    vibes_double_pulse();
    if (s_app.settings.enable_sensation) push_sensation_window();
    else push_summary_window();
  }
}

static void perform_finish_set(void) {
  if (s_app.state.is_resting) { skip_rest(); return; }

  if (s_app.state.curr_ex_idx >= s_app.state.total_exercises) return;

  Exercise *ex = &s_app.state.exercises[s_app.state.curr_ex_idx];
  ex->actual_reps[ex->current_set - 1]   = s_app.state.temp_reps;
  ex->actual_weight[ex->current_set - 1] = s_app.state.temp_weight;

  bool is_first_half  = (ex->modifier == 2 && s_app.state.curr_ex_idx + 1 < s_app.state.total_exercises);
  bool is_second_half = (s_app.state.curr_ex_idx > 0 &&
                         s_app.state.exercises[s_app.state.curr_ex_idx - 1].modifier == 2);

  // Giant-set detection: the current exercise is one of 3 linked exercises
  // (modifier 7 on the anchor). Determine the anchor and the position-in-trio.
  int giant_anchor = -1;
  int giant_pos    = 0;
  if (ex->modifier == 7 && s_app.state.curr_ex_idx + 2 < s_app.state.total_exercises) {
    giant_anchor = s_app.state.curr_ex_idx;
    giant_pos    = 0;
  } else if (s_app.state.curr_ex_idx > 0 &&
             s_app.state.exercises[s_app.state.curr_ex_idx - 1].modifier == 7 &&
             s_app.state.curr_ex_idx + 1 < s_app.state.total_exercises) {
    giant_anchor = s_app.state.curr_ex_idx - 1;
    giant_pos    = 1;
  } else if (s_app.state.curr_ex_idx >= 2 &&
             s_app.state.exercises[s_app.state.curr_ex_idx - 2].modifier == 7) {
    giant_anchor = s_app.state.curr_ex_idx - 2;
    giant_pos    = 2;
  }

  if (giant_anchor >= 0) {
    bool is_lap_end = (giant_pos == 2);

    if (is_lap_end) {
      // We just finished the 3rd member of a lap. Decide whether the lap
      // was the final one (anchor's current_set has already reached target)
      // or whether we should advance to the next lap.
      int anchor_current = s_app.state.exercises[giant_anchor].current_set;
      int anchor_target  = s_app.state.exercises[giant_anchor].target_sets;
      if (anchor_current >= anchor_target) {
        // Final lap complete: advance past the trio and use the inter-exercise rest vibe.
        s_app.state.curr_ex_idx = giant_anchor + 3;
        if (s_app.state.curr_ex_idx < s_app.state.total_exercises) {
          s_app.state.exercises[s_app.state.curr_ex_idx].current_set = 1;
          s_app.state.rest_seconds_remaining = s_app.settings.ex_rest_sec;
          play_vibe(s_app.settings.ex_vibe);
        } else {
          window_stack_remove(s_app.ui.workout_window, false);
          vibes_double_pulse();
          if (s_app.settings.enable_sensation) push_sensation_window();
          else push_summary_window();
          return;
        }
      } else {
        // More laps to go: bump current_set on every member and return to the anchor.
        for (int i = giant_anchor; i < giant_anchor + 3; i++) {
          s_app.state.exercises[i].current_set++;
        }
        s_app.state.curr_ex_idx = giant_anchor;
        s_app.state.rest_seconds_remaining = s_app.settings.super_rest_sec;
        play_vibe(s_app.settings.set_vibe);
      }
    } else {
      // Middle of a lap: cycle to the next member of the trio.
      int next_pos = (giant_pos + 1) % 3;
      s_app.state.curr_ex_idx = giant_anchor + next_pos;
      s_app.state.rest_seconds_remaining = s_app.settings.super_rest_sec;
      play_vibe(s_app.settings.set_vibe);
    }
  } else if (is_first_half) {
    s_app.state.curr_ex_idx++;
    s_app.state.exercises[s_app.state.curr_ex_idx].current_set = ex->current_set;
    s_app.state.rest_seconds_remaining = s_app.settings.super_rest_sec;
    play_vibe(s_app.settings.set_vibe);

  } else if (is_second_half) {
    if (ex->current_set < ex->target_sets) {
      ex->current_set++;
      s_app.state.exercises[s_app.state.curr_ex_idx - 1].current_set++;
      s_app.state.curr_ex_idx--;
      s_app.state.rest_seconds_remaining = s_app.settings.set_rest_sec;
      play_vibe(s_app.settings.set_vibe);
    } else {
      s_app.state.curr_ex_idx++;
      if (s_app.state.curr_ex_idx < s_app.state.total_exercises) {
        s_app.state.exercises[s_app.state.curr_ex_idx].current_set = 1;
        s_app.state.rest_seconds_remaining = s_app.settings.ex_rest_sec;
        play_vibe(s_app.settings.ex_vibe);
      } else {
        window_stack_remove(s_app.ui.workout_window, false);
        vibes_double_pulse();
        if (s_app.settings.enable_sensation) push_sensation_window();
        else push_summary_window();
        return;
      }
    }
  } else {
    if (ex->current_set < ex->target_sets) {
      ex->current_set++;
      if      (ex->modifier == 1 && (ex->current_set % 2 == 0)) s_app.state.rest_seconds_remaining = s_app.settings.drop_rest_sec;
      else if (ex->modifier == 3)                                 s_app.state.rest_seconds_remaining = s_app.settings.set_rest_sec / 2;
      else                                                        s_app.state.rest_seconds_remaining = s_app.settings.set_rest_sec;
      play_vibe(s_app.settings.set_vibe);
    } else {
      s_app.state.curr_ex_idx++;
      if (s_app.state.curr_ex_idx < s_app.state.total_exercises) {
        s_app.state.exercises[s_app.state.curr_ex_idx].current_set = 1;
        s_app.state.rest_seconds_remaining = s_app.settings.ex_rest_sec;
        play_vibe(s_app.settings.ex_vibe);
      } else {
        window_stack_remove(s_app.ui.workout_window, false);
        vibes_double_pulse();
        if (s_app.settings.enable_sensation) push_sensation_window();
        else push_summary_window();
        return;
      }
    }
  }

  init_temp_values(&s_app.state.exercises[s_app.state.curr_ex_idx]);
  s_app.state.edit_mode = (s_app.state.exercises[s_app.state.curr_ex_idx].target_weight == 0) ? 0 : s_app.settings.swap_reps_weight;
  s_app.state.cached_completed_sets = get_completed_sets();

  if (s_app.state.rest_seconds_remaining > 0) {
    s_app.state.is_resting = true;
    set_rest_overlay_state(true, true);
  }
  layer_mark_dirty(s_app.ui.progress_layer);
  update_workout_ui(false);
}

static void perform_skip_set(void) {
  if (s_app.state.is_resting) { skip_rest(); return; }
  s_app.state.temp_reps   = 0;
  s_app.state.temp_weight = 0;
  perform_finish_set();
  if (s_app.state.is_resting) skip_rest();
}

// "Add Set" (double-click action sheet): append one more set to the current
// exercise mid-workout. Linked exercises must keep matching target_sets, so a superset
// grows as a pair and a giant set as a trio. Drop sets alternate normal/drop
// sets, so they grow by a pair to keep the parity intact.
static void add_extra_set(void) {
  if (s_app.state.curr_ex_idx >= s_app.state.total_exercises) return;
  
  int curr = s_app.state.curr_ex_idx;
  int first = curr;
  int last = curr;
  Exercise *ex = &s_app.state.exercises[curr];

  // Safely map Supersets and Giant Sets by finding the true Anchor
  if (ex->modifier == 2 || ex->modifier == 7) {
    // Scan backwards to find the first exercise in the linked group
    while (first > 0 && (s_app.state.exercises[first - 1].modifier == 2 || s_app.state.exercises[first - 1].modifier == 7)) {
      first--;
    }
    // Scan forwards to find the last exercise in the linked group
    while (last < s_app.state.total_exercises - 1 && (s_app.state.exercises[last].modifier == 2 || s_app.state.exercises[last].modifier == 7)) {
      last++;
    }
  }

  int inc = (ex->modifier == 1) ? 2 : 1;
  
  // CRITICAL FIX: Use <= last to ensure EVERY exercise is bounds-checked!
  for (int i = first; i <= last; i++) {
    if (s_app.state.exercises[i].target_sets + inc > 10) {
      vibes_double_pulse(); // Reject if any linked exercise would exceed 10 sets
      return; 
    }
  }

  // Safely apply the sets
  for (int i = first; i <= last; i++) {
    s_app.state.exercises[i].target_sets += inc;
    s_app.state.total_workout_sets += inc;
  }

  s_app.state.cached_completed_sets = get_completed_sets();
  if (s_app.ui.progress_layer) layer_mark_dirty(s_app.ui.progress_layer);
  update_workout_ui(false);
  vibes_short_pulse();
}

static void dictation_session_callback(DictationSession *session, DictationSessionStatus status,
                                       char *transcription, void *context) {
  if (status == DictationSessionStatusSuccess) {
    snprintf(s_app.state.exercises[s_app.state.curr_ex_idx].comment,
             sizeof(s_app.state.exercises[0].comment), "%s", transcription);
    vibes_short_pulse();
    // Reflect the newly dictated note immediately on the toast, respecting
    // the current SK_NOTE_TOAST mode (e.g. don't show during rest in
    // modes 0/1/2/3; do show in modes 4/5).
    update_note_toast_visibility();
  }
}

static void perform_voice_note(void) {
  if (!s_app.state.dictation_session) {
    s_app.state.dictation_session = dictation_session_create(
      sizeof(s_app.state.exercises[0].comment), dictation_session_callback, NULL);
  }
  dictation_session_start(s_app.state.dictation_session);
}

static void new_exercise_dictation_callback(DictationSession *session, DictationSessionStatus status, char *transcription, void *context) {
  if (status == DictationSessionStatusSuccess) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      dict_write_cstring(iter, MESSAGE_KEY_VOICE_ADD_EXERCISE, transcription);
      app_message_outbox_send();
      vibes_short_pulse();
    }
  }
}

static void execute_shortcut(int action_idx) {
  if (s_app.state.is_resting && action_idx != 4 && action_idx != 5 && action_idx != 6) return;
  switch (action_idx) {
    case 0: push_variation_window(); break;
    case 1:
      if (s_app.state.curr_ex_idx < s_app.state.total_exercises &&
          s_app.state.exercises[s_app.state.curr_ex_idx].comment[0] != '\0')
        trigger_manual_note_toast();
      else
        vibes_short_pulse();
      break;
    case 2: swap_exercise();      break;
    case 3: perform_true_skip();  break;
    case 4: perform_finish_set(); break;
    case 5: perform_skip_set();   break;
    case 6: perform_voice_note(); break;
    case 7:
      s_app.state.adding_exercise_to_slot = s_app.state.current_slot;
      if (!s_app.state.new_ex_dictation_session) {
        s_app.state.new_ex_dictation_session = dictation_session_create(128, new_exercise_dictation_callback, NULL);
      }
      dictation_session_start(s_app.state.new_ex_dictation_session);
      break;
    case 8: 
      add_extra_set(); 
      break;
  }
}

// ========================================================================= //
//                           8. TICK HANDLER                                 //
// ========================================================================= //

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (!s_app.state.is_paused) {
    s_app.state.workout_sec++;

    if (s_app.state.is_timed_active) {
      if (s_app.state.curr_ex_idx < s_app.state.total_exercises) {
        s_app.state.temp_reps++;
        if (s_app.state.temp_reps == s_app.state.exercises[s_app.state.curr_ex_idx].target_reps)
          vibes_long_pulse();
        update_workout_ui(false);
      }
    }

    int m = s_app.state.workout_sec / 60;
    int s = s_app.state.workout_sec % 60;
    static char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", m, s);
    text_layer_set_text(s_app.ui.timer_layer, time_buf);
  }

  HealthValue current_hr = 0;
#if defined(PBL_HEALTH)
  if (s_app.state.hr_supported)
    current_hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
#endif

  // user-configured interval (hr_sample_rate minutes).
  static int s_last_minute = -1;
  if (tick_time->tm_min != s_last_minute) {
    update_dark_theme_cache(tick_time);
    s_last_minute = tick_time->tm_min;

    static char clock_buf[16];
    if (s_app.state.is_24h) strftime(clock_buf, sizeof(clock_buf), "%H:%M", tick_time);
    else                     strftime(clock_buf, sizeof(clock_buf), "%I:%M", tick_time);
    text_layer_set_text(s_app.ui.clock_layer, clock_buf);

    bool hr_sample_tick = (tick_time->tm_min % s_app.settings.hr_sample_rate == 0);
    if (hr_sample_tick && current_hr > 0) {
      if (current_hr > (HealthValue)s_app.state.peak_hr) s_app.state.peak_hr = (int)current_hr;
      s_app.state.total_hr += (int)current_hr;
      s_app.state.hr_samples++;
    }
  }

#if !defined(PBL_ROUND)
#if defined(PBL_HEALTH)
  // Live HR display updates every second when available (unchanged behaviour)
  if (s_app.state.hr_supported) {
    static char hr_buf[16];
    if (current_hr > 0) snprintf(hr_buf, sizeof(hr_buf), "%lu BPM", (uint32_t)current_hr);
    else                 snprintf(hr_buf, sizeof(hr_buf), "-- BPM");
    text_layer_set_text(s_app.ui.hr_layer, hr_buf);
  }
#endif
#endif

  if (s_app.state.is_resting) {
    if (s_app.settings.enable_ghost && s_app.state.last_workout_sec > 0 &&
        s_app.state.total_workout_sets > 0) {
      int expected_sec = (s_app.state.last_workout_sec * s_app.state.cached_completed_sets) /
                          s_app.state.total_workout_sets;
      int diff     = s_app.state.workout_sec - expected_sec;
      int abs_diff = diff < 0 ? -diff : diff;
      int dm = abs_diff / 60, ds = abs_diff % 60;

      static char ghost_buf[32];
      if (diff <= 0) {
        snprintf(ghost_buf, sizeof(ghost_buf), "Ahead -%02d:%02d", dm, ds);
        text_layer_set_text_color(s_app.ui.rest_title_layer, PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorBlack));
      } else {
        snprintf(ghost_buf, sizeof(ghost_buf), "Behind +%02d:%02d", dm, ds);
        text_layer_set_text_color(s_app.ui.rest_title_layer, PBL_IF_COLOR_ELSE(GColorRed, GColorBlack));
      }
      text_layer_set_text(s_app.ui.rest_title_layer, ghost_buf);
    } else {
      text_layer_set_text_color(s_app.ui.rest_title_layer, GColorBlack);
      text_layer_set_text(s_app.ui.rest_title_layer, "REST");
    }

    if (!s_app.state.is_paused) s_app.state.rest_seconds_remaining--;

    bool rest_finished = (s_app.state.rest_seconds_remaining <= 0);
#if defined(PBL_HEALTH)
    if (!rest_finished && s_app.settings.dynamic_hr_target > 0 &&
        current_hr > 0 && current_hr <= (HealthValue)s_app.settings.dynamic_hr_target)
      rest_finished = true;
#endif

    if (rest_finished) {
      skip_rest();
      play_vibe(s_app.settings.rest_vibe);
    } else {
      static char rest_buf[16], skip_buf[32];
      if (s_app.settings.dynamic_hr_target > 0) {
        if (current_hr > 0) snprintf(rest_buf, sizeof(rest_buf), "%lu", (uint32_t)current_hr);
        else                 snprintf(rest_buf, sizeof(rest_buf), "--");
        snprintf(skip_buf, sizeof(skip_buf), "Target: %d | Max: %ds",
                 s_app.settings.dynamic_hr_target, s_app.state.rest_seconds_remaining);
      } else {
        snprintf(rest_buf, sizeof(rest_buf), "%d", s_app.state.rest_seconds_remaining);
        snprintf(skip_buf, sizeof(skip_buf), "[Select] to Skip");
      }
      text_layer_set_text(s_app.ui.rest_time_layer, rest_buf);
      text_layer_set_text(s_app.ui.rest_skip_layer,  skip_buf);
    }
  }
}

// ========================================================================= //
//                            9. UI WINDOWS                                  //
// ========================================================================= //

// ----------- Settings Window -----------
static uint16_t settings_get_num_sections_callback(MenuLayer *ml, void *data) { return 5; }
static uint16_t settings_get_num_rows_callback(MenuLayer *ml, uint16_t section, void *data) {
  if (section == 0) return 5;
  if (section == 1) return 3;
  if (section == 2) return 1;
  if (section == 3) return 10;
  if (section == 4) return 3;
  return 0;
}
static int16_t settings_get_header_height_callback(MenuLayer *ml, uint16_t section, void *data) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}
static void settings_draw_header_callback(GContext *ctx, const Layer *cell_layer, uint16_t section, void *data) {
  switch (section) {
    case 0: menu_cell_basic_header_draw(ctx, cell_layer, "Timers");    break;
    case 1: menu_cell_basic_header_draw(ctx, cell_layer, "Haptics");   break;
    case 2: menu_cell_basic_header_draw(ctx, cell_layer, "Modifiers"); break;
    case 3: menu_cell_basic_header_draw(ctx, cell_layer, "System");    break;
    case 4: menu_cell_basic_header_draw(ctx, cell_layer, "Shortcuts"); break;
  }
}

static void settings_draw_row_callback(GContext *ctx, const Layer *cell_layer,
                                        MenuIndex *cell_index, void *data) {
  char title[32], subtitle[32];
  static const char *vibes[]  = {"Off", "Short", "Long", "Double"};
  static const char *themes[] = {"Orange", "Blue", "Red", "Green"};
  static const char *units[]  = {"kg", "lbs"};

  if (cell_index->section == 0) {
    switch (cell_index->row) {
      case 0: snprintf(title, sizeof(title), "Set Rest");   snprintf(subtitle, sizeof(subtitle), "%ds", s_app.settings.set_rest_sec);   break;
      case 1: snprintf(title, sizeof(title), "Ex. Rest");   snprintf(subtitle, sizeof(subtitle), "%ds", s_app.settings.ex_rest_sec);    break;
      case 2: snprintf(title, sizeof(title), "Super Rest"); snprintf(subtitle, sizeof(subtitle), "%ds", s_app.settings.super_rest_sec); break;
      case 3: snprintf(title, sizeof(title), "Drop Rest");  snprintf(subtitle, sizeof(subtitle), "%ds", s_app.settings.drop_rest_sec);  break;
      case 4:
        snprintf(title, sizeof(title), "Dynamic Rest");
        if (s_app.settings.dynamic_hr_target == 0) snprintf(subtitle, sizeof(subtitle), "Off (Timer Only)");
        else                                         snprintf(subtitle, sizeof(subtitle), "Resume @ %d BPM", s_app.settings.dynamic_hr_target);
        break;
    }
  } else if (cell_index->section == 1) {
    switch (cell_index->row) {
      case 0: snprintf(title, sizeof(title), "Set Vibe");  snprintf(subtitle, sizeof(subtitle), "%s", vibes[s_app.settings.set_vibe]);  break;
      case 1: snprintf(title, sizeof(title), "Ex. Vibe");  snprintf(subtitle, sizeof(subtitle), "%s", vibes[s_app.settings.ex_vibe]);   break;
      case 2: snprintf(title, sizeof(title), "Rest Vibe"); snprintf(subtitle, sizeof(subtitle), "%s", vibes[s_app.settings.rest_vibe]); break;
    }
  } else if (cell_index->section == 2) {
    snprintf(title, sizeof(title), "Drop Set %%"); snprintf(subtitle, sizeof(subtitle), "-%d%%", s_app.settings.drop_set_pct);
  } else if (cell_index->section == 3) {
    switch (cell_index->row) {
      case 0:
        snprintf(title, sizeof(title), "Theme Color");
        if (s_app.settings.theme_color_idx >= 4) snprintf(subtitle, sizeof(subtitle), "Secret Mode: %d", s_app.settings.theme_color_idx - 4);
        else                                       snprintf(subtitle, sizeof(subtitle), "%s", themes[s_app.settings.theme_color_idx]);
        break;
      case 1: snprintf(title, sizeof(title), "Weight Unit"); snprintf(subtitle, sizeof(subtitle), "%s", units[s_app.settings.weight_unit_idx]); break;
      case 2: snprintf(title, sizeof(title), "Long Press");  snprintf(subtitle, sizeof(subtitle), "%d ms", s_app.settings.long_press_ms);        break;
      case 3:
        snprintf(title, sizeof(title), "Dark Mode");
        if      (s_app.settings.dark_mode == 0) snprintf(subtitle, sizeof(subtitle), "Off");
        else if (s_app.settings.dark_mode == 1) snprintf(subtitle, sizeof(subtitle), "On");
        else                                     snprintf(subtitle, sizeof(subtitle), "Auto (8PM-7AM)");
        break;
      case 4: snprintf(title, sizeof(title), "Ghost Pacer"); snprintf(subtitle, sizeof(subtitle), s_app.settings.enable_ghost     ? "On" : "Off"); break;
      case 5: snprintf(title, sizeof(title), "Sensation Q."); snprintf(subtitle, sizeof(subtitle), s_app.settings.enable_sensation ? "On" : "Off"); break;
      case 6: {
        static const char *hr_rates[] = {"Every min", "Every 2 min", "Every 5 min", "Every 10 min"};
        int rate_idx = 0;
        if      (s_app.settings.hr_sample_rate >= 10) rate_idx = 3;
        else if (s_app.settings.hr_sample_rate >=  5) rate_idx = 2;
        else if (s_app.settings.hr_sample_rate >=  2) rate_idx = 1;
        snprintf(title, sizeof(title), "HR Sampling");
        snprintf(subtitle, sizeof(subtitle), "%s", hr_rates[rate_idx]);
        break;
      }
      case 7:
        snprintf(title, sizeof(title), "UI Layout");
        snprintf(subtitle, sizeof(subtitle), s_app.settings.swap_reps_weight ? "Weight | Reps" : "Reps | Weight");
        break;
      case 8: {
        static const char *toast_modes[] = {
          "Auto 8s", "Auto 15s", "Auto 20s",
          "Perm. Workout", "Perm. Rest", "Perm. Both", "Manual"
        };
        int mode = s_app.settings.note_toast_mode;
        if (mode < 0 || mode > 6) mode = 0;
        snprintf(title, sizeof(title), "Note Toast");
        snprintf(subtitle, sizeof(subtitle), "%s", toast_modes[mode]);
        break;
      }
      case 9: {
        static const char *prefill_modes[] = {"Target", "History", "Zero"};
        int mode = s_app.settings.prefill_mode;
        if (mode < 0 || mode > 2) mode = 0;
        snprintf(title, sizeof(title), "Pre-fill Sets");
        snprintf(subtitle, sizeof(subtitle), "%s", prefill_modes[mode]);
        break;
      }
    }
  } else if (cell_index->section == 4) {
    static const char *actions[] = {"Variations","View Note","Swap (Later)","Skip Entirely","Finish Set","Skip Set","Voice Note", "Add Ex. (Voice)", "Add Set"};
    switch (cell_index->row) {
      case 0: snprintf(title, sizeof(title), "Up Long Press");     snprintf(subtitle, sizeof(subtitle), "%s", actions[s_app.settings.shortcut_up]);     break;
      case 1: snprintf(title, sizeof(title), "Down Long Press");   snprintf(subtitle, sizeof(subtitle), "%s", actions[s_app.settings.shortcut_down]);   break;
      case 2: snprintf(title, sizeof(title), "Select Long Press"); snprintf(subtitle, sizeof(subtitle), "%s", actions[s_app.settings.shortcut_select]); break;
    }
  }
  menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
}

static void settings_select_callback(MenuLayer *ml, MenuIndex *cell_index, void *data) {
  if (cell_index->section == 0) {
    switch (cell_index->row) {
      case 0: s_app.settings.set_rest_sec   += 15; if (s_app.settings.set_rest_sec   > 180) s_app.settings.set_rest_sec   = 0;  save_setting(SK_SET_REST,   s_app.settings.set_rest_sec);   break;
      case 1: s_app.settings.ex_rest_sec    += 30; if (s_app.settings.ex_rest_sec    > 240) s_app.settings.ex_rest_sec    = 0;  save_setting(SK_EX_REST,    s_app.settings.ex_rest_sec);    break;
      case 2: s_app.settings.super_rest_sec +=  5; if (s_app.settings.super_rest_sec >  30) s_app.settings.super_rest_sec = 0;  save_setting(SK_SUPER_REST, s_app.settings.super_rest_sec); break;
      case 3: s_app.settings.drop_rest_sec  +=  5; if (s_app.settings.drop_rest_sec  >  30) s_app.settings.drop_rest_sec  = 5;  save_setting(SK_DROP_REST,  s_app.settings.drop_rest_sec);  break;
      case 4:
        s_app.settings.dynamic_hr_target += 5;
        if (s_app.settings.dynamic_hr_target > 160)  s_app.settings.dynamic_hr_target = 0;
        if (s_app.settings.dynamic_hr_target > 0 && s_app.settings.dynamic_hr_target < 90) s_app.settings.dynamic_hr_target = 90;
        save_setting(SK_DYNAMIC_HR, s_app.settings.dynamic_hr_target);
        break;
    }
  } else if (cell_index->section == 1) {
    switch (cell_index->row) {
      case 0: s_app.settings.set_vibe++;  if (s_app.settings.set_vibe  > 3) s_app.settings.set_vibe  = 0; save_setting(SK_SET_VIBE,  s_app.settings.set_vibe);  play_vibe(s_app.settings.set_vibe);  break;
      case 1: s_app.settings.ex_vibe++;   if (s_app.settings.ex_vibe   > 3) s_app.settings.ex_vibe   = 0; save_setting(SK_EX_VIBE,   s_app.settings.ex_vibe);   play_vibe(s_app.settings.ex_vibe);   break;
      case 2: s_app.settings.rest_vibe++; if (s_app.settings.rest_vibe > 3) s_app.settings.rest_vibe = 0; save_setting(SK_REST_VIBE, s_app.settings.rest_vibe); play_vibe(s_app.settings.rest_vibe); break;
    }
  } else if (cell_index->section == 2) {
    s_app.settings.drop_set_pct += 5;
    if (s_app.settings.drop_set_pct > 50) s_app.settings.drop_set_pct = 10;
    save_setting(SK_DROP_PCT, s_app.settings.drop_set_pct);
  } else if (cell_index->section == 3) {
    switch (cell_index->row) {
      case 0:
        s_app.settings.theme_color_idx++;
        if (s_app.settings.theme_color_idx == 4) s_app.settings.theme_color_idx = 0;
        else if (s_app.settings.theme_color_idx > 67) s_app.settings.theme_color_idx = 4;
        save_setting(SK_THEME_COLOR, s_app.settings.theme_color_idx);
        menu_layer_set_highlight_colors(s_app.ui.settings_menu_layer, get_theme_color(), get_bg_color());
        break;
      case 1: s_app.settings.weight_unit_idx++; if (s_app.settings.weight_unit_idx > 1) s_app.settings.weight_unit_idx = 0; save_setting(SK_WEIGHT_UNIT, s_app.settings.weight_unit_idx); break;
      case 2: s_app.settings.long_press_ms += 250; if (s_app.settings.long_press_ms > 1500) s_app.settings.long_press_ms = 250; save_setting(SK_LONG_PRESS, s_app.settings.long_press_ms); break;
      case 3:
        s_app.settings.dark_mode++;
        if (s_app.settings.dark_mode > 2) s_app.settings.dark_mode = 0;
        save_setting(SK_DARK_MODE, s_app.settings.dark_mode);
        { time_t now = time(NULL); struct tm *t = localtime(&now); update_dark_theme_cache(t); }
        window_set_background_color(s_app.ui.settings_window, get_bg_color());
        menu_layer_set_normal_colors(s_app.ui.settings_menu_layer,    get_bg_color(), get_text_color());
        menu_layer_set_highlight_colors(s_app.ui.settings_menu_layer, get_theme_color(), get_bg_color());
        break;
      case 4: s_app.settings.enable_ghost     = !s_app.settings.enable_ghost;     save_setting(SK_ENABLE_GHOST, s_app.settings.enable_ghost);     break;
      case 5: s_app.settings.enable_sensation = !s_app.settings.enable_sensation; save_setting(SK_ENABLE_SENS,  s_app.settings.enable_sensation);  break;
      case 6: {
        // Cycle through 1 → 2 → 5 → 10 → 1
        static const int hr_steps[] = {1, 2, 5, 10};
        int cur = s_app.settings.hr_sample_rate;
        int next = 1;
        for (int k = 0; k < 3; k++) {
          if (cur == hr_steps[k]) { next = hr_steps[k + 1]; break; }
        }
        s_app.settings.hr_sample_rate = next;
        save_setting(SK_HR_RATE, s_app.settings.hr_sample_rate);
        break;
      }
      case 7:
        s_app.settings.swap_reps_weight = !s_app.settings.swap_reps_weight;
        save_setting(SK_SWAP_UI, s_app.settings.swap_reps_weight);
        break;
      case 8:
        s_app.settings.note_toast_mode++;
        if (s_app.settings.note_toast_mode > 6) s_app.settings.note_toast_mode = 0;
        save_setting(SK_NOTE_TOAST, s_app.settings.note_toast_mode);
        update_note_toast_visibility();
        break;
      case 9:
        s_app.settings.prefill_mode++;
        if (s_app.settings.prefill_mode > 2) s_app.settings.prefill_mode = 0;
        save_setting(SK_PREFILL_MODE, s_app.settings.prefill_mode);
        break;
    }
  } else if (cell_index->section == 4) {
    switch (cell_index->row) {
      case 0: s_app.settings.shortcut_up++;     if (s_app.settings.shortcut_up     > 8) s_app.settings.shortcut_up     = 0; save_setting(SK_SHORTCUT_UP,   s_app.settings.shortcut_up);     break;
      case 1: s_app.settings.shortcut_down++;   if (s_app.settings.shortcut_down   > 8) s_app.settings.shortcut_down   = 0; save_setting(SK_SHORTCUT_DOWN, s_app.settings.shortcut_down);   break;
      case 2: s_app.settings.shortcut_select++; if (s_app.settings.shortcut_select > 8) s_app.settings.shortcut_select = 0; save_setting(SK_SHORTCUT_SEL,  s_app.settings.shortcut_select); break;
    }
  }
  menu_layer_reload_data(s_app.ui.settings_menu_layer);
}

static void header_bg_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
}

static void settings_select_long_callback(MenuLayer *ml, MenuIndex *cell_index, void *data) {
  // Keep the secret theme color easter egg!
  if (cell_index->section == 3 && cell_index->row == 0) {
    s_app.settings.theme_color_idx = (s_app.settings.theme_color_idx < 4) ? 4 : 0;
    save_setting(SK_THEME_COLOR, s_app.settings.theme_color_idx);
    vibes_double_pulse();
    menu_layer_reload_data(s_app.ui.settings_menu_layer);
    menu_layer_set_highlight_colors(s_app.ui.settings_menu_layer, get_theme_color(), get_bg_color());
    return;
  }
  
  if (cell_index->section == 0 && cell_index->row < 4) {
    s_timer_edit_row = cell_index->row;
    if (s_timer_edit_row == 0) s_timer_edit_val = s_app.settings.set_rest_sec;
    else if (s_timer_edit_row == 1) s_timer_edit_val = s_app.settings.ex_rest_sec;
    else if (s_timer_edit_row == 2) s_timer_edit_val = s_app.settings.super_rest_sec;
    else if (s_timer_edit_row == 3) s_timer_edit_val = s_app.settings.drop_rest_sec;
    
    push_timer_edit_window();
    return; 
  }

  // Tooltip Router
  const char* tooltip = "No description available.";
  
  if (cell_index->section == 0) {
    // We only reach here for row 4 (Dynamic Rest) now!
    if (cell_index->row == 4) tooltip = "Dynamic Rest:\nAutomatically ends your rest period early if your Heart Rate drops below this target BPM.";
  } else if (cell_index->section == 1) {
    tooltip = "Haptics:\nChoose the vibration pattern that plays when this specific timer finishes.";
  } else if (cell_index->section == 2) {
    tooltip = "Drop Set %:\nThe percentage of weight automatically removed for the next set when performing a Drop Set.";
  } else if (cell_index->section == 3) {
    switch (cell_index->row) {
      case 3: tooltip = "Dark Mode:\nAuto switches to a dark theme between 8PM and 7AM to save battery and reduce glare."; break;
      case 4: tooltip = "Ghost Pacer:\nCompares your current workout pace against your last session in the rest timer window."; break;
      case 5: tooltip = "Sensation Q:\nAsks how you felt at the end of the workout before syncing data."; break;
      case 6: tooltip = "HR Sampling:\nHow often the watch saves your heart rate to calculate the workout average."; break;
      case 7: tooltip = "UI Layout:\nSwaps the position of Reps and Weight on the screen."; break;
      case 8: tooltip = "Note Toast:\nAuto: Dismisses after delay.\nPerm: Always visible based on state.\nManual: Only shows via the View Note shortcut or menu."; break;
      case 9: tooltip = "Pre-fill Sets:\nTarget: Uses set goals.\nHistory: Uses actuals from last workout.\nZero: Starts at 0."; break;
    }
  } else if (cell_index->section == 4) {
    tooltip = "Shortcuts:\nBind a specific action to a long-press of the Up, Down, or Select button during a workout.";
  }

  // Display the tooltip
  snprintf(s_shared_text_buffer, sizeof(s_shared_text_buffer), "%s", tooltip);
  push_help_window();
}

static void settings_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds   = layer_get_bounds(w_layer);

  s_app.ui.settings_header_bg = layer_create(GRect(0, 0, bounds.size.w, 40));
  layer_set_update_proc(s_app.ui.settings_header_bg, header_bg_update_proc);
  layer_add_child(w_layer, s_app.ui.settings_header_bg);

  s_app.ui.settings_header_text = build_text_layer(GRect(0, 6, bounds.size.w, 30),
    FONT_KEY_GOTHIC_24_BOLD, GColorWhite, GTextAlignmentCenter, s_app.ui.settings_header_bg);
  text_layer_set_text(s_app.ui.settings_header_text, "Settings");

  s_app.ui.settings_menu_layer = menu_layer_create(GRect(0, 40, bounds.size.w, bounds.size.h - 40));
  menu_layer_set_callbacks(s_app.ui.settings_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = settings_get_num_sections_callback,
    .get_num_rows     = settings_get_num_rows_callback,
    .get_header_height= settings_get_header_height_callback,
    .draw_header      = settings_draw_header_callback,
    .draw_row         = settings_draw_row_callback,
    .select_click     = settings_select_callback,
    .select_long_click= settings_select_long_callback,
  });
  menu_layer_set_normal_colors(s_app.ui.settings_menu_layer,    get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(s_app.ui.settings_menu_layer, get_theme_color(), get_bg_color());
  menu_layer_set_click_config_onto_window(s_app.ui.settings_menu_layer, window);
  layer_add_child(w_layer, menu_layer_get_layer(s_app.ui.settings_menu_layer));
}

static void settings_window_unload(Window *window) {
  text_layer_destroy(s_app.ui.settings_header_text);
  layer_destroy(s_app.ui.settings_header_bg);
  menu_layer_destroy(s_app.ui.settings_menu_layer);

  // Refresh main window theme in case dark mode changed
  window_set_background_color(s_app.ui.main_window, get_bg_color());
  menu_layer_set_normal_colors(s_app.ui.menu_layer,    get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(s_app.ui.menu_layer, get_theme_color(), get_bg_color());

  // FIX #8: removed the unsafe workout/summary window destroy from here.
  // Those windows are destroyed in deinit() only.
}

static void push_settings_window(void) {
  if (!s_app.ui.settings_window) {
    s_app.ui.settings_window = window_create();
    window_set_window_handlers(s_app.ui.settings_window,
      (WindowHandlers){ .load = settings_window_load, .unload = settings_window_unload });
  }
  window_set_background_color(s_app.ui.settings_window, get_bg_color());
  window_stack_push(s_app.ui.settings_window, true);
}

// ----------- Help Window -----------
static void help_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds   = layer_get_bounds(w_layer);
  s_app.ui.help_text_layer = build_text_layer(
    GRect(10, 20, bounds.size.w - 20, bounds.size.h - 20), // Tweak Y to give it room
    FONT_KEY_GOTHIC_18_BOLD, GColorBlack, GTextAlignmentCenter, w_layer);
    
  text_layer_set_text(s_app.ui.help_text_layer, s_shared_text_buffer);
}
static void help_window_unload(Window *window) { text_layer_destroy(s_app.ui.help_text_layer); }

static void push_help_window(void) {
  if (!s_app.ui.help_window) {
    s_app.ui.help_window = window_create();
    window_set_window_handlers(s_app.ui.help_window,
      (WindowHandlers){ .load = help_window_load, .unload = help_window_unload });
  }
  window_set_background_color(s_app.ui.help_window, get_bg_color());
  window_stack_push(s_app.ui.help_window, true);
}

// ----------- Confirm / Edit Window -----------
static void update_edit_ui(void) {
  static char edit_buf[128];
  if (s_app.storage.slot_to_edit == s_app.storage.target_swap_slot) {
    snprintf(edit_buf, sizeof(edit_buf), "DELETE ROUTINE:\n'%s'\n\n[Up/Down] Move\n[Select] Delete",
             s_app.storage.slot_names[s_app.storage.slot_to_edit]);
  } else if (s_app.storage.slot_counts[s_app.storage.target_swap_slot] == 0) {
    snprintf(edit_buf, sizeof(edit_buf), "MOVE TO END\n\n[Up/Down] Move\n[Select] Swap");
  } else {
    snprintf(edit_buf, sizeof(edit_buf), "SWAP TO SLOT:\n'%s'\n\n[Up/Down] Move\n[Select] Swap",
             s_app.storage.slot_names[s_app.storage.target_swap_slot]);
  }
  text_layer_set_text(s_app.ui.confirm_text_layer, edit_buf);
}

static void edit_up_click(ClickRecognizerRef recognizer, void *context) {
  s_app.storage.target_swap_slot--;
  if (s_app.storage.target_swap_slot < 0)
    s_app.storage.target_swap_slot = (s_app.storage.active_slots < MAX_SLOTS)
                                     ? s_app.storage.active_slots : MAX_SLOTS - 1;
  update_edit_ui();
}
static void edit_down_click(ClickRecognizerRef recognizer, void *context) {
  s_app.storage.target_swap_slot++;
  int max = (s_app.storage.active_slots < MAX_SLOTS) ? s_app.storage.active_slots : MAX_SLOTS - 1;
  if (s_app.storage.target_swap_slot > max) s_app.storage.target_swap_slot = 0;
  update_edit_ui();
}
static void edit_select_click(ClickRecognizerRef recognizer, void *context) {
  if (s_app.storage.slot_to_edit == s_app.storage.target_swap_slot) {
    persist_delete(STORAGE_KEY_BASE + s_app.storage.slot_to_edit);
    for (int j = 0; j < MAX_EXERCISES; j++)
      persist_delete(ROUTINE_EX_BASE + (s_app.storage.slot_to_edit * MAX_EXERCISES) + j);
    persist_delete(LAST_COMPLETED_KEY_BASE + s_app.storage.slot_to_edit);
    persist_delete(GHOST_KEY_BASE + s_app.storage.slot_to_edit);
  } else {
    RoutineHeader edit_header, target_header;
    bool target_exists = persist_exists(STORAGE_KEY_BASE + s_app.storage.target_swap_slot);

    persist_read_data(STORAGE_KEY_BASE + s_app.storage.slot_to_edit, &edit_header, sizeof(RoutineHeader));
    if (target_exists)
      persist_read_data(STORAGE_KEY_BASE + s_app.storage.target_swap_slot, &target_header, sizeof(RoutineHeader));

    persist_write_data(STORAGE_KEY_BASE + s_app.storage.target_swap_slot, &edit_header, sizeof(RoutineHeader));
    if (target_exists)
      persist_write_data(STORAGE_KEY_BASE + s_app.storage.slot_to_edit, &target_header, sizeof(RoutineHeader));
    else
      persist_delete(STORAGE_KEY_BASE + s_app.storage.slot_to_edit);

    for (int j = 0; j < MAX_EXERCISES; j++) {
      Exercise edit_ex, target_ex;
      bool e_exists = (persist_read_data(ROUTINE_EX_BASE + (s_app.storage.slot_to_edit   * MAX_EXERCISES) + j, &edit_ex,   sizeof(Exercise)) > 0);
      bool t_exists = target_exists &&
                      (persist_read_data(ROUTINE_EX_BASE + (s_app.storage.target_swap_slot * MAX_EXERCISES) + j, &target_ex, sizeof(Exercise)) > 0);

      if (e_exists) persist_write_data(ROUTINE_EX_BASE + (s_app.storage.target_swap_slot * MAX_EXERCISES) + j, &edit_ex,   sizeof(Exercise));
      else          persist_delete(    ROUTINE_EX_BASE + (s_app.storage.target_swap_slot * MAX_EXERCISES) + j);

      if (t_exists) persist_write_data(ROUTINE_EX_BASE + (s_app.storage.slot_to_edit   * MAX_EXERCISES) + j, &target_ex, sizeof(Exercise));
      else          persist_delete(    ROUTINE_EX_BASE + (s_app.storage.slot_to_edit   * MAX_EXERCISES) + j);
    }

    // Swap last_completed timestamps and ghost-pacer durations along with the routine
    swap_slot_int(LAST_COMPLETED_KEY_BASE, s_app.storage.slot_to_edit, s_app.storage.target_swap_slot, target_exists);
    swap_slot_int(GHOST_KEY_BASE,          s_app.storage.slot_to_edit, s_app.storage.target_swap_slot, target_exists);
  }
  refresh_directory();
  menu_layer_reload_data(s_app.ui.menu_layer);
  // Pop confirm window and remove action window so we land back on the main menu
  window_stack_remove(s_app.ui.action_window, false);
  window_stack_pop(true);
}

static void confirm_click_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, edit_select_click);
  window_single_click_subscribe(BUTTON_ID_UP,     edit_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN,   edit_down_click);
}

static void confirm_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds   = layer_get_bounds(w_layer);
  s_app.ui.confirm_text_layer = build_text_layer(
    GRect(5, 25, bounds.size.w - 10, bounds.size.h - 25),
    FONT_KEY_GOTHIC_24_BOLD, GColorBlack, GTextAlignmentCenter, w_layer);
  update_edit_ui();
}
static void confirm_window_unload(Window *window) { text_layer_destroy(s_app.ui.confirm_text_layer); }

static void push_confirm_window(void) {
  if (!s_app.ui.confirm_window) {
    s_app.ui.confirm_window = window_create();
    window_set_click_config_provider(s_app.ui.confirm_window, confirm_click_provider);
    window_set_window_handlers(s_app.ui.confirm_window,
      (WindowHandlers){ .load = confirm_window_load, .unload = confirm_window_unload });
  }
  window_set_background_color(s_app.ui.confirm_window, get_bg_color());
  window_stack_push(s_app.ui.confirm_window, true);
}

// ----------- Exit Window -----------
static void exit_up_click(ClickRecognizerRef recognizer, void *context) {
  s_app.state.is_paused = false;
  window_stack_pop(true);
}
static void exit_select_click(ClickRecognizerRef recognizer, void *context) {
  s_app.state.is_paused = false;
  s_app.state.active    = false;
  persist_delete(ACTIVE_STATE_KEY);
  vibes_double_pulse();
  window_stack_remove(s_app.ui.exit_window,    false);
  window_stack_remove(s_app.ui.workout_window, false);
  if (s_app.settings.enable_sensation) push_sensation_window();
  else push_summary_window();
}
static void exit_down_click(ClickRecognizerRef recognizer, void *context) {
  s_app.state.is_paused   = false;
  s_app.state.active      = false;
  s_app.state.has_resume  = false;
  persist_delete(ACTIVE_STATE_KEY);
  window_stack_remove(s_app.ui.workout_window, false);
  // OPT #7: free summary and sensation windows so the next workout starts clean
  if (s_app.ui.summary_window)   { window_destroy(s_app.ui.summary_window);   s_app.ui.summary_window   = NULL; }
  if (s_app.ui.sensation_window) { window_destroy(s_app.ui.sensation_window); s_app.ui.sensation_window = NULL; }
  window_stack_pop(true);
  menu_layer_reload_data(s_app.ui.menu_layer);
}
static void exit_click_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP,     exit_up_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, exit_select_click);
  window_single_click_subscribe(BUTTON_ID_DOWN,   exit_down_click);
  window_single_click_subscribe(BUTTON_ID_BACK,   exit_up_click);
}
static void exit_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds   = layer_get_bounds(w_layer);
  s_app.ui.exit_text_layer = build_text_layer(
    GRect(5, 20, bounds.size.w - 10, bounds.size.h - 20),
    FONT_KEY_GOTHIC_24_BOLD, GColorBlack, GTextAlignmentCenter, w_layer);
  text_layer_set_text(s_app.ui.exit_text_layer, "END WORKOUT?\n\n[Up] Resume\n[Select] Save\n[Down] Discard");
}
static void exit_window_unload(Window *window) { text_layer_destroy(s_app.ui.exit_text_layer); }

static void push_exit_window(void) {
  if (!s_app.ui.exit_window) {
    s_app.ui.exit_window = window_create();
    window_set_click_config_provider(s_app.ui.exit_window, exit_click_provider);
    window_set_window_handlers(s_app.ui.exit_window,
      (WindowHandlers){ .load = exit_window_load, .unload = exit_window_unload });
  }
  window_set_background_color(s_app.ui.exit_window, get_bg_color());
  window_stack_push(s_app.ui.exit_window, true);
}

// --- Confirm Add Exercise Window ---
static void confirm_add_up_click(ClickRecognizerRef recognizer, void *context) {
  // User clicked YES - Execute the save logic!
  int slot_idx = s_app.state.adding_exercise_to_slot;
  RoutineHeader header;

  if (persist_exists(STORAGE_KEY_BASE + slot_idx)) {
    persist_read_data(STORAGE_KEY_BASE + slot_idx, &header, sizeof(RoutineHeader));

    if (header.total_exercises < MAX_EXERCISES) {
      persist_write_data(ROUTINE_EX_BASE + (slot_idx * MAX_EXERCISES) + header.total_exercises, &s_app.state.pending_exercise, sizeof(Exercise));
      header.total_exercises++;
      persist_write_data(STORAGE_KEY_BASE + slot_idx, &header, sizeof(RoutineHeader));

      // Inject into live workout if currently active
      if (s_app.state.active && s_app.state.current_slot == slot_idx) {
        s_app.state.exercises[s_app.state.total_exercises] = s_app.state.pending_exercise;
        s_app.state.total_exercises++;
        s_app.state.total_workout_sets += s_app.state.pending_exercise.target_sets;

        // FIX: Safely check if the workout window is ACTUALLY loaded on screen
        if (s_app.ui.workout_window && window_is_loaded(s_app.ui.workout_window)) {
          update_workout_ui(false);
          if (s_app.ui.progress_layer) layer_mark_dirty(s_app.ui.progress_layer);
        }
      }

      refresh_directory();

      // FIX: Only attempt to reload menus if their parent windows are currently alive
      if (s_app.ui.menu_layer && window_is_loaded(s_app.ui.main_window)) {
        menu_layer_reload_data(s_app.ui.menu_layer);
      }
      if (s_app.ui.inspector_menu_layer && window_is_loaded(s_app.ui.inspector_window)) {
        menu_layer_reload_data(s_app.ui.inspector_menu_layer);
      }

      vibes_short_pulse();
    } else {
      vibes_double_pulse(); // Max exercises reached
    }
  }
  window_stack_pop(true);
}

static void confirm_add_down_click(ClickRecognizerRef recognizer, void *context) {
  // User clicked NO / CANCEL
  window_stack_pop(true);
}

static void confirm_add_click_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, confirm_add_up_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, confirm_add_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, confirm_add_down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, confirm_add_down_click);
}

static void confirm_add_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);

  s_app.ui.confirm_add_text_layer = build_text_layer(GRect(5, 5, bounds.size.w - 10, bounds.size.h - 5), FONT_KEY_GOTHIC_24_BOLD, GColorBlack, GTextAlignmentCenter, w_layer);

  // Format the text nicely to show them what they dictated
  static char conf_buf[128];
  if (s_app.state.pending_exercise.target_weight == 0) {
    snprintf(conf_buf, sizeof(conf_buf), "ADD EXERCISE?\n%s\n%d Sets x %d Reps\n(BW)\n\n[Select] Yes\n[Back] Cancel",
             s_app.state.pending_exercise.name, s_app.state.pending_exercise.target_sets, s_app.state.pending_exercise.target_reps);
  } else {
    snprintf(conf_buf, sizeof(conf_buf), "ADD EXERCISE?\n%s\n%d Sets x %d Reps\n@ %d%s\n\n[Select] Yes\n[Back] Cancel",
             s_app.state.pending_exercise.name, s_app.state.pending_exercise.target_sets, s_app.state.pending_exercise.target_reps,
             s_app.state.pending_exercise.target_weight, s_app.settings.weight_unit_idx == 0 ? "kg" : "lbs");
  }
  text_layer_set_text(s_app.ui.confirm_add_text_layer, conf_buf);
}

static void confirm_add_window_unload(Window *window) {
  text_layer_destroy(s_app.ui.confirm_add_text_layer);
}

static void push_confirm_add_window() {
  if(!s_app.ui.confirm_add_window) {
    s_app.ui.confirm_add_window = window_create();
    window_set_click_config_provider(s_app.ui.confirm_add_window, confirm_add_click_provider);
    window_set_window_handlers(s_app.ui.confirm_add_window, (WindowHandlers) { .load = confirm_add_window_load, .unload = confirm_add_window_unload });
  }
  window_set_background_color(s_app.ui.confirm_add_window, get_bg_color());
  window_stack_push(s_app.ui.confirm_add_window, true);
}

// ----------- Sensation Window -----------
static uint16_t sensation_get_num_rows_callback(MenuLayer *ml, uint16_t section, void *data) { return 5; }
static void sensation_draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  menu_cell_basic_draw(ctx, cell_layer, s_sensation_titles[cell_index->row], s_sensation_subs[cell_index->row], NULL);
}
static void sensation_select_callback(MenuLayer *ml, MenuIndex *cell_index, void *data) {
  s_app.state.sensation = 5 - cell_index->row;
  push_summary_window();
  window_stack_remove(s_app.ui.sensation_window, false);
}
static void sensation_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds   = layer_get_bounds(w_layer);
  s_app.ui.sensation_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_app.ui.sensation_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = sensation_get_num_rows_callback,
    .draw_row     = sensation_draw_row_callback,
    .select_click = sensation_select_callback,
  });
  menu_layer_set_normal_colors(s_app.ui.sensation_menu_layer,    get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(s_app.ui.sensation_menu_layer, get_theme_color(), get_bg_color());
  menu_layer_set_click_config_onto_window(s_app.ui.sensation_menu_layer, window);
  layer_add_child(w_layer, menu_layer_get_layer(s_app.ui.sensation_menu_layer));
}
static void sensation_window_unload(Window *window) { menu_layer_destroy(s_app.ui.sensation_menu_layer); }
static void push_sensation_window(void) {
  if (!s_app.ui.sensation_window) {
    s_app.ui.sensation_window = window_create();
    window_set_window_handlers(s_app.ui.sensation_window,
      (WindowHandlers){ .load = sensation_window_load, .unload = sensation_window_unload });
  }
  window_set_background_color(s_app.ui.sensation_window, get_bg_color());
  window_stack_push(s_app.ui.sensation_window, true);
}

// ----------- Summary Window -----------
static void summary_exit_click(ClickRecognizerRef recognizer, void *context) {
  if (s_app.ui.sensation_window) { window_destroy(s_app.ui.sensation_window); s_app.ui.sensation_window = NULL; }

  if (s_app.ui.menu_layer) {
    menu_layer_reload_data(s_app.ui.menu_layer);
  }

  window_stack_pop(true);
  // summary_window itself will be destroyed by deinit or next workout start
}
static void summary_click_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP,     summary_exit_click);
  window_single_click_subscribe(BUTTON_ID_DOWN,   summary_exit_click);
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
  graphics_fill_rect(ctx, GRect(10,       y_top, box_w, box_h), 4, GCornersAll);
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, is_dark_theme() ? GColorWhite : GColorBlack));
  graphics_fill_rect(ctx, GRect(mid + 5,  y_top, box_w, box_h), 4, GCornersAll);
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorCobaltBlue, is_dark_theme() ? GColorWhite : GColorBlack));
  graphics_fill_rect(ctx, GRect(10,       y_bot, box_w, box_h), 4, GCornersAll);
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorOrange, is_dark_theme() ? GColorWhite : GColorBlack));
  graphics_fill_rect(ctx, GRect(mid + 5,  y_bot, box_w, box_h), 4, GCornersAll);
}

static void summary_window_load(Window *window) {
  tick_timer_service_unsubscribe();

  s_app.state.active = false;
  persist_delete(ACTIVE_STATE_KEY);
  s_app.settings.last_routine_slot = s_app.state.current_slot;
  persist_write_int(SETTINGS_KEY_BASE + SK_LAST_SLOT, s_app.settings.last_routine_slot);

  if (s_app.state.workout_sec > 0) {
    persist_write_int(GHOST_KEY_BASE + s_app.state.current_slot, s_app.state.workout_sec);

    persist_write_int(LAST_COMPLETED_KEY_BASE + s_app.state.current_slot, (int)time(NULL));

    s_app.storage.last_completed[s_app.state.current_slot] = (int)time(NULL);
  }

  Layer *w_layer = window_get_root_layer(window);
  GRect bounds   = layer_get_bounds(w_layer);

#if defined(PBL_ROUND)
  int y_title = (bounds.size.h * 10) / 100;
  int y_top   = (bounds.size.h * 32) / 100;
  int box_h   = (bounds.size.h * 20) / 100;
  int y_bot   = y_top + box_h + 4;
  int y_info  = (bounds.size.h * 78) / 100;
#else
  bool is_tall = (bounds.size.h > 180);
  int y_title = is_tall ? 15 : 2;
  int y_top   = is_tall ? 80 : 46;
  int box_h   = is_tall ? 42 : 36;
  int y_bot   = y_top + box_h + 4;
  int y_info  = is_tall ? 190 : 126;
#endif
  int box_w = (bounds.size.w / 2) - 15;
  int mid   = bounds.size.w / 2;

  s_app.ui.sum_title_layer = build_text_layer(GRect(0, y_title, bounds.size.w, 40),
    FONT_KEY_GOTHIC_28_BOLD, get_text_color(), GTextAlignmentCenter, w_layer);

  s_app.ui.summary_bg_layer = layer_create(bounds);
  layer_set_update_proc(s_app.ui.summary_bg_layer, summary_bg_update_proc);
  layer_add_child(w_layer, s_app.ui.summary_bg_layer);

  GColor grid_text_color = PBL_IF_COLOR_ELSE(GColorWhite, is_dark_theme() ? GColorBlack : GColorWhite);
  s_app.ui.beat_layer     = build_text_layer(GRect(10,      y_top + 2, box_w, box_h), FONT_KEY_GOTHIC_14_BOLD, grid_text_color, GTextAlignmentCenter, w_layer);
  s_app.ui.missed_layer   = build_text_layer(GRect(mid + 5, y_top + 2, box_w, box_h), FONT_KEY_GOTHIC_14_BOLD, grid_text_color, GTextAlignmentCenter, w_layer);
  s_app.ui.accuracy_layer = build_text_layer(GRect(10,      y_bot + 2, box_w, box_h), FONT_KEY_GOTHIC_14_BOLD, grid_text_color, GTextAlignmentCenter, w_layer);
  s_app.ui.density_layer  = build_text_layer(GRect(mid + 5, y_bot + 2, box_w, box_h), FONT_KEY_GOTHIC_14_BOLD, grid_text_color, GTextAlignmentCenter, w_layer);
  text_layer_set_text_color(s_app.ui.beat_layer,     grid_text_color);
  text_layer_set_text_color(s_app.ui.missed_layer,   grid_text_color);
  text_layer_set_text_color(s_app.ui.accuracy_layer, grid_text_color);
  text_layer_set_text_color(s_app.ui.density_layer,  grid_text_color);

  s_app.ui.sum_info_layer = build_text_layer(GRect(0, y_info, bounds.size.w, bounds.size.h - y_info),
    FONT_KEY_GOTHIC_14, get_text_color(), GTextAlignmentCenter, w_layer);

  int m = s_app.state.workout_sec / 60;
  int s = s_app.state.workout_sec % 60;
  static char title_buf[32];
  snprintf(title_buf, sizeof(title_buf), "Done! %02d:%02d", m, s);
  text_layer_set_text(s_app.ui.sum_title_layer, title_buf);

  // FIX #4: use int32_t for volume to prevent overflow on heavy sessions
  int sets_above = 0, sets_below = 0;
  int total_target_reps = 0, total_actual_reps = 0;
  int32_t total_volume = 0;

  for (int i = 0; i < s_app.state.total_exercises; i++) {
    int ex_misses = 0;
    for (int j = 0; j < s_app.state.exercises[i].target_sets; j++) {
      int a_r = s_app.state.exercises[i].actual_reps[j];
      int a_w = s_app.state.exercises[i].actual_weight[j];
      int t_r = s_app.state.exercises[i].target_reps;
      int t_w = s_app.state.exercises[i].target_weight;

      if (s_app.state.exercises[i].modifier == 1 && ((j + 1) % 2 == 0)) {
        if (t_w == 0) {
            t_r = s_app.state.exercises[i].actual_reps[j - 1];
            t_r = (t_r * (100 - s_app.settings.drop_set_pct)) / 100;
        } else {
            t_w = s_app.state.exercises[i].actual_weight[j - 1];
            t_w = (t_w * (100 - s_app.settings.drop_set_pct)) / 100;
        }
      }

      total_target_reps += t_r;
      total_actual_reps += a_r;
      total_volume      += (int32_t)a_r * (int32_t)a_w;   // FIX #4: 32-bit multiply

      if      (a_w > t_w || (a_w == t_w && a_r > t_r)) sets_above++;
      else if (a_w < t_w || (a_w == t_w && a_r < t_r)) { sets_below++; ex_misses++; }
    }

    if (s_app.settings.progression_mode != -1 && ex_misses == 0) {
      if (s_app.state.exercises[i].modifier == 6) {
        int increase = (s_app.state.exercises[i].target_reps * 5) / 100;
        if (increase < 1) increase = 1;
        s_app.state.exercises[i].target_reps += increase;
        s_app.state.exercises[i].dirty = true;   // OPT #5
      } else if (s_app.settings.progression_mode == 0) {
        if (s_app.state.exercises[i].target_weight > 0) {
          s_app.state.exercises[i].target_weight += s_app.settings.weight_increment;
          s_app.state.exercises[i].dirty = true; // OPT #5
        } else {
          s_app.state.exercises[i].target_reps += 1;
          s_app.state.exercises[i].dirty = true; // OPT #5
        }
      } else if (s_app.settings.progression_mode == 1) {
        s_app.state.exercises[i].target_reps += 1;
        s_app.state.exercises[i].dirty = true;   // OPT #5
      }
    }
  }

  int accuracy = (total_target_reps > 0) ? ((total_actual_reps * 100) / total_target_reps) : 0;
  // FIX #4: use int32_t arithmetic for density to avoid overflow
  int density  = (s_app.state.workout_sec > 0)
                 ? (int)((total_volume * (int32_t)60) / (int32_t)s_app.state.workout_sec) : 0;

  static char celebration_buf[80];
  if (total_volume > 0) {
    // FIX #4: safe cast – total_volume fits in int32_t; divide down for milestone check
    int32_t norm_v = (s_app.settings.weight_unit_idx == 1)
                     ? (total_volume * 45 / 100) : total_volume;
    const char *milestone = "Great session!";
    if      (norm_v >= 10000) milestone = "You lifted a Jet plane!";
    else if (norm_v >=  5000) milestone = "You lifted an Elephant!";
    else if (norm_v >=  2000) milestone = "You lifted a Rhino!";
    else if (norm_v >=  1000) milestone = "You lifted a Great White!";
    else if (norm_v >=   500) milestone = "You lifted a Grizzly Bear!";
    else if (norm_v >=   250) milestone = "You lifted a Motorcycle!";
    else if (norm_v >=   100) milestone = "You lifted a Giant Panda!";
    snprintf(celebration_buf, sizeof(celebration_buf), "Synced! %s\nTotal Tonnage: %ld %s",
             milestone, (long)total_volume, s_app.settings.weight_unit_idx == 0 ? "kg" : "lbs");
  } else if (total_actual_reps > 0) {
    snprintf(celebration_buf, sizeof(celebration_buf), "Synced! Gravity defeated.\nBody moved %d times!", total_actual_reps);
  } else {
    snprintf(celebration_buf, sizeof(celebration_buf), "Data synced!\nPress any button.");
  }

  static char beat_buf[16], missed_buf[16], acc_buf[16], den_buf[16];
  snprintf(beat_buf,   sizeof(beat_buf),   "Beat\n%d",  sets_above);
  snprintf(missed_buf, sizeof(missed_buf), "Miss\n%d",  sets_below);
  snprintf(acc_buf,    sizeof(acc_buf),    "Acc\n%d%%", accuracy);
  snprintf(den_buf,    sizeof(den_buf),    "Dens\n%d",  density);
  text_layer_set_text(s_app.ui.beat_layer,     beat_buf);
  text_layer_set_text(s_app.ui.missed_layer,   missed_buf);
  text_layer_set_text(s_app.ui.accuracy_layer, acc_buf);
  text_layer_set_text(s_app.ui.density_layer,  den_buf);

  // FIX #3: guard localtime() NULL before calling strftime
  time_t now = time(NULL);
  struct tm *tick_time = localtime(&now);
  char date_buf[32] = "Unknown Date";
  if (tick_time) strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M", tick_time);

  int avg_hr = (s_app.state.hr_samples > 0) ? (s_app.state.total_hr / s_app.state.hr_samples) : 0;

  // Use static buffers for export/sync to avoid heap pressure.        FIX #1
  static char export_buf[EXPORT_BUF_SIZE];
  static char sync_buf[EXPORT_BUF_SIZE];

  int limit   = EXPORT_BUF_SIZE;
  int written = snprintf(export_buf, limit, "%s|%s|%d|%d|%d|%d|%d|%d",
    s_app.state.routine_name, date_buf, s_app.state.workout_sec,
    s_app.state.sensation, accuracy, density, s_app.state.peak_hr, avg_hr);
  int offset = (written > 0 && written < limit) ? written : limit - 1;

  for (int i = 0; i < s_app.state.total_exercises; i++) {
    if (offset >= limit - 1) break;
    const char *mod_label = "";
    if      (s_app.state.exercises[i].modifier == 1) mod_label = " [DROP]";
    else if (s_app.state.exercises[i].modifier == 2) mod_label = " [SUPER]";
    else if (s_app.state.exercises[i].modifier == 6) mod_label = " [TIMED]";
    else if (s_app.state.exercises[i].modifier == 7) mod_label = " [GIANT]";
    written = snprintf(export_buf + offset, limit - offset, "|%s%s",
                       s_app.state.exercises[i].name, mod_label);
    offset += (written > 0 && written < limit - offset) ? written : limit - offset - 1;

    for (int j = 0; j < s_app.state.exercises[i].target_sets; j++) {
      if (offset >= limit - 1) break;
      written = snprintf(export_buf + offset, limit - offset, "|%d|%d",
                         s_app.state.exercises[i].actual_reps[j],
                         s_app.state.exercises[i].actual_weight[j]);
      offset += (written > 0 && written < limit - offset) ? written : limit - offset - 1;
    }
  }

  int s_written = snprintf(sync_buf, limit, "%s|%d|%d",
    s_app.state.routine_name, s_app.settings.progression_mode, s_app.settings.weight_increment);
  int s_off = (s_written > 0 && s_written < limit) ? s_written : limit - 1;

  for (int i = 0; i < s_app.state.total_exercises; i++) {
    // OPT #5: only write exercises whose progression actually changed
    // if (s_app.settings.progression_mode != -1 && s_app.state.exercises[i].dirty)
      persist_write_data(ROUTINE_EX_BASE + (s_app.state.current_slot * MAX_EXERCISES) + i,
                         &s_app.state.exercises[i], sizeof(Exercise));

    int base_sets = s_app.state.exercises[i].target_sets;
    if (s_app.state.exercises[i].modifier == 1)
      s_app.state.exercises[i].target_sets = base_sets / 2;

    if (s_off < limit - 1) {
      s_written = snprintf(sync_buf + s_off, limit - s_off, "|%s|%d|%d|%d|%d|%s",
        s_app.state.exercises[i].name,
        s_app.state.exercises[i].target_sets,
        s_app.state.exercises[i].target_reps,
        s_app.state.exercises[i].target_weight,
        s_app.state.exercises[i].modifier,
        s_app.state.exercises[i].comment[0] != '\0' ? s_app.state.exercises[i].comment : "-");
      s_off += (s_written > 0 && s_written < limit - s_off) ? s_written : limit - s_off - 1;
    }

    s_app.state.exercises[i].target_sets = base_sets;
  }

  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_cstring(iter, MESSAGE_KEY_WORKOUT_SUMMARY, export_buf);
    dict_write_cstring(iter, MESSAGE_KEY_ROUTINE_DATA,    sync_buf);
    app_message_outbox_send();
    text_layer_set_text(s_app.ui.sum_info_layer, celebration_buf);
  } else {
    text_layer_set_text(s_app.ui.sum_info_layer, "Sync Failed.\nCheck Bluetooth.");
  }
}

static void summary_window_unload(Window *window) {
  text_layer_destroy(s_app.ui.sum_title_layer);
  layer_destroy(s_app.ui.summary_bg_layer);
  text_layer_destroy(s_app.ui.beat_layer);
  text_layer_destroy(s_app.ui.missed_layer);
  text_layer_destroy(s_app.ui.accuracy_layer);
  text_layer_destroy(s_app.ui.density_layer);
  text_layer_destroy(s_app.ui.sum_info_layer);
}

static void push_summary_window(void) {
  if (!s_app.ui.summary_window) {
    s_app.ui.summary_window = window_create();
    window_set_click_config_provider(s_app.ui.summary_window, summary_click_provider);
    window_set_window_handlers(s_app.ui.summary_window,
      (WindowHandlers){ .load = summary_window_load, .unload = summary_window_unload });
  }
  window_set_background_color(s_app.ui.summary_window, get_bg_color());
  window_stack_push(s_app.ui.summary_window, true);
}

// ----------- Variation Window -----------
static uint16_t variation_get_num_rows_callback(MenuLayer *ml, uint16_t section, void *data) { return NUM_VARIATIONS; }
static void variation_draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  menu_cell_basic_draw(ctx, cell_layer, s_variations[cell_index->row], NULL, NULL);
}
static void variation_select_callback(MenuLayer *ml, MenuIndex *cell_index, void *data) {
  // FIX #6: guard before array access
  if (s_app.state.curr_ex_idx >= s_app.state.total_exercises) { window_stack_pop(true); return; }
  Exercise *ex = &s_app.state.exercises[s_app.state.curr_ex_idx];

  char base_name[32];
  strncpy(base_name, ex->name, sizeof(base_name) - 1);
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
    // FIX #2: strncpy with explicit null termination
    strncpy(ex->name, base_name, sizeof(ex->name) - 1);
    ex->name[sizeof(ex->name) - 1] = '\0';
  } else {
    char big_temp[64];
    snprintf(big_temp, sizeof(big_temp), "%s %s", base_name, s_variations[cell_index->row]);
    strncpy(s_app.state.exercises[s_app.state.curr_ex_idx].name, big_temp,
            sizeof(s_app.state.exercises[s_app.state.curr_ex_idx].name) - 1);
    s_app.state.exercises[s_app.state.curr_ex_idx].name[
      sizeof(s_app.state.exercises[s_app.state.curr_ex_idx].name) - 1] = '\0';
  }

  update_workout_ui(false);
  window_stack_pop(true);
}
static void variation_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds   = layer_get_bounds(w_layer);
  s_app.ui.variation_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_app.ui.variation_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = variation_get_num_rows_callback,
    .draw_row     = variation_draw_row_callback,
    .select_click = variation_select_callback,
  });
  menu_layer_set_normal_colors(s_app.ui.variation_menu_layer,    get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(s_app.ui.variation_menu_layer, get_theme_color(), get_bg_color());
  menu_layer_set_click_config_onto_window(s_app.ui.variation_menu_layer, window);
  layer_add_child(w_layer, menu_layer_get_layer(s_app.ui.variation_menu_layer));
}
static void variation_window_unload(Window *window) { menu_layer_destroy(s_app.ui.variation_menu_layer); }
static void push_variation_window(void) {
  if (!s_app.ui.variation_window) {
    s_app.ui.variation_window = window_create();
    window_set_window_handlers(s_app.ui.variation_window,
      (WindowHandlers){ .load = variation_window_load, .unload = variation_window_unload });
  }
  window_set_background_color(s_app.ui.variation_window, get_bg_color());
  window_stack_push(s_app.ui.variation_window, true);
}

// ----------- Exercise Action Window -----------
static bool is_exercise_linked_in_storage(int slot, int ex_idx, int total_exercises) {
  Exercise temp_routine[MAX_EXERCISES];
  for(int i = 0; i < total_exercises; i++) {
    persist_read_data(ROUTINE_EX_BASE + (slot * MAX_EXERCISES) + i, &temp_routine[i], sizeof(Exercise));
  }
  return (
    temp_routine[ex_idx].modifier == 2 || temp_routine[ex_idx].modifier == 7 ||
    (ex_idx > 0 && temp_routine[ex_idx - 1].modifier == 2) ||
    (ex_idx > 0 && temp_routine[ex_idx - 1].modifier == 7) ||
    (ex_idx > 1 && temp_routine[ex_idx - 2].modifier == 7)
  );
}

static void ex_action_move_up_callback(int index, void *ctx) {
  if (s_app.storage.exercise_to_edit > 0) {
    int slot = s_app.storage.slot_to_edit;
    int ex_idx = s_app.storage.exercise_to_edit;

    // BUG 1 FIX: Block breaking linked sets from the Inspector
    if (is_exercise_linked_in_storage(slot, ex_idx, s_app.storage.slot_counts[slot]) ||
        is_exercise_linked_in_storage(slot, ex_idx - 1, s_app.storage.slot_counts[slot])) {
      vibes_double_pulse();
      window_stack_pop(true);
      return;
    }

    Exercise ex1, ex2;
    persist_read_data(ROUTINE_EX_BASE + (slot * MAX_EXERCISES) + ex_idx, &ex1, sizeof(Exercise));
    persist_read_data(ROUTINE_EX_BASE + (slot * MAX_EXERCISES) + ex_idx - 1, &ex2, sizeof(Exercise));

    persist_write_data(ROUTINE_EX_BASE + (slot * MAX_EXERCISES) + ex_idx - 1, &ex1, sizeof(Exercise));
    persist_write_data(ROUTINE_EX_BASE + (slot * MAX_EXERCISES) + ex_idx, &ex2, sizeof(Exercise));

    if (s_app.state.active && s_app.state.current_slot == slot) {
      s_app.state.exercises[ex_idx - 1] = ex1;
      s_app.state.exercises[ex_idx] = ex2;

      if (s_app.state.curr_ex_idx == ex_idx) {
        s_app.state.curr_ex_idx--;
      } else if (s_app.state.curr_ex_idx == ex_idx - 1) {
        s_app.state.curr_ex_idx++;
      }
      if (s_app.ui.workout_window) update_workout_ui(false);
    }

    s_app.storage.exercise_to_edit--;
    vibes_short_pulse();
    menu_layer_reload_data(s_app.ui.inspector_menu_layer);
  }
  window_stack_pop(true);
}

static void ex_action_move_down_callback(int index, void *ctx) {
  int slot = s_app.storage.slot_to_edit;
  if (s_app.storage.exercise_to_edit < s_app.storage.slot_counts[slot] - 1) {
    int ex_idx = s_app.storage.exercise_to_edit;

    // BUG 1 FIX: Block breaking linked sets from the Inspector
    if (is_exercise_linked_in_storage(slot, ex_idx, s_app.storage.slot_counts[slot]) ||
        is_exercise_linked_in_storage(slot, ex_idx + 1, s_app.storage.slot_counts[slot])) {
      vibes_double_pulse();
      window_stack_pop(true);
      return;
    }

    Exercise ex1, ex2;
    persist_read_data(ROUTINE_EX_BASE + (slot * MAX_EXERCISES) + ex_idx, &ex1, sizeof(Exercise));
    persist_read_data(ROUTINE_EX_BASE + (slot * MAX_EXERCISES) + ex_idx + 1, &ex2, sizeof(Exercise));

    persist_write_data(ROUTINE_EX_BASE + (slot * MAX_EXERCISES) + ex_idx + 1, &ex1, sizeof(Exercise));
    persist_write_data(ROUTINE_EX_BASE + (slot * MAX_EXERCISES) + ex_idx, &ex2, sizeof(Exercise));

    if (s_app.state.active && s_app.state.current_slot == slot) {
      s_app.state.exercises[ex_idx + 1] = ex1;
      s_app.state.exercises[ex_idx] = ex2;

      if (s_app.state.curr_ex_idx == ex_idx) {
        s_app.state.curr_ex_idx++;
      } else if (s_app.state.curr_ex_idx == ex_idx + 1) {
        s_app.state.curr_ex_idx--;
      }
      if (s_app.ui.workout_window) update_workout_ui(false);
    }

    s_app.storage.exercise_to_edit++;
    vibes_short_pulse();
    menu_layer_reload_data(s_app.ui.inspector_menu_layer);
  }
  window_stack_pop(true);
}

static void ex_action_delete_callback(int index, void *ctx) {
  int slot = s_app.storage.slot_to_edit;
  int ex_idx = s_app.storage.exercise_to_edit;
  RoutineHeader header;
  persist_read_data(STORAGE_KEY_BASE + slot, &header, sizeof(RoutineHeader));

  // BUG 2 FIX: Zombie Routine Crash Prevention
  if (header.total_exercises <= 1) {
    persist_delete(STORAGE_KEY_BASE + slot);
    persist_delete(ROUTINE_EX_BASE + (slot * MAX_EXERCISES) + 0);
    persist_delete(LAST_COMPLETED_KEY_BASE + slot);
    persist_delete(GHOST_KEY_BASE + slot);
    
    if (s_app.state.active && s_app.state.current_slot == slot) {
      s_app.state.active = false;
      window_stack_remove(s_app.ui.workout_window, false);
    }
    
    vibes_double_pulse();
    refresh_directory();
    menu_layer_reload_data(s_app.ui.menu_layer);
    
    // Nuke the UI stack back to the Main Menu
    window_stack_remove(s_app.ui.ex_action_window, false);
    window_stack_remove(s_app.ui.inspector_window, false);
    window_stack_pop(true); 
    return;
  }

  // BUG 1 FIX: Block deleting linked sets from the Inspector
  if (is_exercise_linked_in_storage(slot, ex_idx, header.total_exercises)) {
    vibes_double_pulse();
    window_stack_pop(true);
    return;
  }

  Exercise deleted_ex;
  persist_read_data(ROUTINE_EX_BASE + (slot * MAX_EXERCISES) + ex_idx, &deleted_ex, sizeof(Exercise));

  for (int i = ex_idx; i < header.total_exercises - 1; i++) {
    Exercise next_ex;
    persist_read_data(ROUTINE_EX_BASE + (slot * MAX_EXERCISES) + i + 1, &next_ex, sizeof(Exercise));
    persist_write_data(ROUTINE_EX_BASE + (slot * MAX_EXERCISES) + i, &next_ex, sizeof(Exercise));
  }

  persist_delete(ROUTINE_EX_BASE + (slot * MAX_EXERCISES) + header.total_exercises - 1);
  header.total_exercises--;
  persist_write_data(STORAGE_KEY_BASE + slot, &header, sizeof(RoutineHeader));
  s_app.storage.slot_counts[slot] = header.total_exercises;

  if (s_app.state.active && s_app.state.current_slot == slot) {
    for (int i = ex_idx; i < s_app.state.total_exercises - 1; i++) {
      s_app.state.exercises[i] = s_app.state.exercises[i + 1];
    }
    s_app.state.total_exercises--;
    s_app.state.total_workout_sets -= deleted_ex.target_sets;

    if (s_app.state.curr_ex_idx > ex_idx) {
      s_app.state.curr_ex_idx--;
    } else if (s_app.state.curr_ex_idx == ex_idx) {
      if (s_app.state.curr_ex_idx >= s_app.state.total_exercises) {
        s_app.state.curr_ex_idx = s_app.state.total_exercises > 0 ? s_app.state.total_exercises - 1 : 0;
      }
      if (s_app.state.total_exercises > 0) {
        init_temp_values(&s_app.state.exercises[s_app.state.curr_ex_idx]);
      } else {
        window_stack_remove(s_app.ui.workout_window, false);
        s_app.state.active = false;
      }
    }

    if (s_app.state.active && s_app.ui.workout_window) {
      update_workout_ui(false);
      layer_mark_dirty(s_app.ui.progress_layer);
    }
  }

  vibes_double_pulse();
  menu_layer_reload_data(s_app.ui.inspector_menu_layer);
  menu_layer_reload_data(s_app.ui.menu_layer); 
  window_stack_pop(true);
}

static void ex_action_window_load(Window *window) {
  int num_items = 0;
  s_app.ui.ex_action_menu_items[num_items++] = (SimpleMenuItem){ .title = "Move Up",   .callback = ex_action_move_up_callback };
  s_app.ui.ex_action_menu_items[num_items++] = (SimpleMenuItem){ .title = "Move Down", .callback = ex_action_move_down_callback };
  s_app.ui.ex_action_menu_items[num_items++] = (SimpleMenuItem){ .title = "Delete",    .callback = ex_action_delete_callback };

  // Use a static buffer so the title doesn't disappear when the function returns!
  static char ex_title_buf[32];
  Exercise temp_ex;
  persist_read_data(ROUTINE_EX_BASE + (s_app.storage.slot_to_edit * MAX_EXERCISES) + s_app.storage.exercise_to_edit, &temp_ex, sizeof(Exercise));
  snprintf(ex_title_buf, sizeof(ex_title_buf), "%s", temp_ex.name);

  s_app.ui.ex_action_menu_sections[0] = (SimpleMenuSection){
    .title = ex_title_buf,
    .num_items = num_items, .items = s_app.ui.ex_action_menu_items,
  };

  Layer *w_layer = window_get_root_layer(window);
  GRect bounds   = layer_get_frame(w_layer);

  s_app.ui.ex_action_menu_layer = simple_menu_layer_create(bounds, window, s_app.ui.ex_action_menu_sections, 1, NULL);
  MenuLayer *internal = simple_menu_layer_get_menu_layer(s_app.ui.ex_action_menu_layer);
  menu_layer_set_normal_colors(internal,    get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(internal, get_theme_color(), get_bg_color());

  layer_add_child(w_layer, simple_menu_layer_get_layer(s_app.ui.ex_action_menu_layer));
}

static void ex_action_window_unload(Window *window) {
  simple_menu_layer_destroy(s_app.ui.ex_action_menu_layer);
}

static void push_ex_action_window(void) {
  if (!s_app.ui.ex_action_window) {
    s_app.ui.ex_action_window = window_create();
    window_set_window_handlers(s_app.ui.ex_action_window,
                               (WindowHandlers){ .load = ex_action_window_load, .unload = ex_action_window_unload });
  }
  window_set_background_color(s_app.ui.ex_action_window, get_bg_color());
  window_stack_push(s_app.ui.ex_action_window, true);
}

// ----------- Inspector Window -----------
static uint16_t inspector_get_num_rows_callback(MenuLayer *ml, uint16_t section, void *data) {
  return (uint16_t)s_app.storage.slot_counts[s_app.storage.slot_to_edit];
}
static void inspector_draw_row_callback(GContext *ctx, const Layer *cell_layer,
                                         MenuIndex *cell_index, void *data) {
  Exercise temp_ex;
  persist_read_data(ROUTINE_EX_BASE + (s_app.storage.slot_to_edit * MAX_EXERCISES) + cell_index->row,
                    &temp_ex, sizeof(Exercise));
  char subtitle[32];
  if (temp_ex.target_weight == 0)
    snprintf(subtitle, sizeof(subtitle), "%d Sets x %d Reps (BW)", temp_ex.target_sets, temp_ex.target_reps);
  else
    snprintf(subtitle, sizeof(subtitle), "%d Sets x %d Reps @ %d%s",
             temp_ex.target_sets, temp_ex.target_reps, temp_ex.target_weight,
             s_app.settings.weight_unit_idx == 0 ? "kg" : "lbs");
  menu_cell_basic_draw(ctx, cell_layer, temp_ex.name, subtitle, NULL);
}
static void inspector_select_callback(MenuLayer *ml, MenuIndex *cell_index, void *data) {
  s_app.storage.exercise_to_edit = cell_index->row;
  push_ex_action_window();
}
static void inspector_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds   = layer_get_bounds(w_layer);
  s_app.ui.inspector_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_app.ui.inspector_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = inspector_get_num_rows_callback,
    .draw_row     = inspector_draw_row_callback,
    .select_click = inspector_select_callback,
  });
  menu_layer_set_normal_colors(s_app.ui.inspector_menu_layer,    get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(s_app.ui.inspector_menu_layer, get_theme_color(), get_bg_color());
  menu_layer_set_click_config_onto_window(s_app.ui.inspector_menu_layer, window);
  layer_add_child(w_layer, menu_layer_get_layer(s_app.ui.inspector_menu_layer));
}
static void inspector_window_unload(Window *window) {
  menu_layer_destroy(s_app.ui.inspector_menu_layer);
  s_app.ui.inspector_menu_layer = NULL; // FIX: Prevent dangling pointer
}
static void push_inspector_window(void) {
  if (!s_app.ui.inspector_window) {
    s_app.ui.inspector_window = window_create();
    window_set_window_handlers(s_app.ui.inspector_window,
      (WindowHandlers){ .load = inspector_window_load, .unload = inspector_window_unload });
  }
  window_set_background_color(s_app.ui.inspector_window, get_bg_color());
  window_stack_push(s_app.ui.inspector_window, true);
}

// ----------- Action Window -----------
static void action_start_callback(int index, void *ctx) {
  window_stack_remove(s_app.ui.action_window, false);
  start_workout_from_slot(s_app.storage.slot_to_edit);
}
static void action_inspect_callback(int index, void *ctx) { push_inspector_window(); }
static void action_edit_callback(int index, void *ctx) {
  s_app.storage.target_swap_slot = s_app.storage.slot_to_edit;
  push_confirm_window();
}
static void action_window_load(Window *window) {
  int num_items = 0;
  s_app.ui.action_menu_items[num_items++] = (SimpleMenuItem){ .title = "Start Workout",  .callback = action_start_callback };
  s_app.ui.action_menu_items[num_items++] = (SimpleMenuItem){ .title = "View Exercises", .callback = action_inspect_callback };
  s_app.ui.action_menu_items[num_items++] = (SimpleMenuItem){ .title = "Add Ex. (Voice)", .callback = action_quick_add_callback };
  s_app.ui.action_menu_items[num_items++] = (SimpleMenuItem){ .title = "Move / Delete",  .callback = action_edit_callback };
  s_app.ui.action_menu_sections[0] = (SimpleMenuSection){
    .title = s_app.storage.slot_names[s_app.storage.slot_to_edit],
    .num_items = num_items, .items = s_app.ui.action_menu_items,
  };
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds   = layer_get_frame(w_layer);
  s_app.ui.action_menu_layer = simple_menu_layer_create(bounds, window, s_app.ui.action_menu_sections, 1, NULL);
  MenuLayer *internal = simple_menu_layer_get_menu_layer(s_app.ui.action_menu_layer);
  menu_layer_set_normal_colors(internal,    get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(internal, get_theme_color(), get_bg_color());
  layer_add_child(w_layer, simple_menu_layer_get_layer(s_app.ui.action_menu_layer));
}
static void action_quick_add_callback(int index, void *ctx) {
  window_stack_remove(s_app.ui.action_window, false);
  s_app.state.adding_exercise_to_slot = s_app.storage.slot_to_edit;
  if (!s_app.state.new_ex_dictation_session) {
    s_app.state.new_ex_dictation_session = dictation_session_create(128, new_exercise_dictation_callback, NULL);
  }
  dictation_session_start(s_app.state.new_ex_dictation_session);
}
static void action_window_unload(Window *window) { simple_menu_layer_destroy(s_app.ui.action_menu_layer); }
static void push_action_window(void) {
  if (!s_app.ui.action_window) {
    s_app.ui.action_window = window_create();
    window_set_window_handlers(s_app.ui.action_window,
      (WindowHandlers){ .load = action_window_load, .unload = action_window_unload });
  }
  window_set_background_color(s_app.ui.action_window, get_bg_color());
  window_stack_push(s_app.ui.action_window, true);
}

// ----------- Mega (Action Sheet) Window -----------
static void menu_var_callback(int index, void *ctx)       { push_variation_window(); }
static void menu_note_callback(int index, void *ctx) {
  if (s_app.state.curr_ex_idx < s_app.state.total_exercises &&
      s_app.state.exercises[s_app.state.curr_ex_idx].comment[0] != '\0') {
    trigger_manual_note_toast(); 
  } else {
    vibes_short_pulse();
  }
}
static void menu_add_set_callback(int index, void *ctx)   { window_stack_remove(s_app.ui.mega_window, false); add_extra_set(); }
static void menu_swap_callback(int index, void *ctx)      { window_stack_remove(s_app.ui.mega_window, false); swap_exercise(); }
static void menu_skip_callback(int index, void *ctx)      { window_stack_remove(s_app.ui.mega_window, false); perform_true_skip(); }
static void menu_finish_callback(int index, void *ctx)    { window_stack_remove(s_app.ui.mega_window, false); perform_finish_set(); }
static void menu_skip_set_callback(int index, void *ctx)  { window_stack_remove(s_app.ui.mega_window, false); perform_skip_set(); }
static void menu_voice_callback(int index, void *ctx)     { window_stack_remove(s_app.ui.mega_window, false); perform_voice_note(); }
static void mega_window_load(Window *window) {
  int n = 0;
  s_app.ui.mega_menu_items[n++] = (SimpleMenuItem){ .title = "Finish Set",             .subtitle = "Log & progress workout",    .callback = menu_finish_callback };
  s_app.ui.mega_menu_items[n++] = (SimpleMenuItem){ .title = "Add Set",                .subtitle = "One more set of this exercise", .callback = menu_add_set_callback };
  s_app.ui.mega_menu_items[n++] = (SimpleMenuItem){ .title = "View Note",
    .subtitle = (s_app.state.curr_ex_idx < s_app.state.total_exercises &&
                 s_app.state.exercises[s_app.state.curr_ex_idx].comment[0] != '\0')
                ? s_app.state.exercises[s_app.state.curr_ex_idx].comment
                : "No note attached",
    .callback = menu_note_callback };
  s_app.ui.mega_menu_items[n++] = (SimpleMenuItem){ .title = "Record Voice Note",       .subtitle = "Dictate note for this exercise", .callback = menu_voice_callback };
  s_app.ui.mega_menu_items[n++] = (SimpleMenuItem){ .title = "Variations",              .subtitle = "Add a variation",               .callback = menu_var_callback };
  s_app.ui.mega_menu_items[n++] = (SimpleMenuItem){ .title = "Do Later (Swap)",         .subtitle = "Swap with next exercise",        .callback = menu_swap_callback };
  s_app.ui.mega_menu_items[n++] = (SimpleMenuItem){ .title = "Skip Set (Log 0)",        .subtitle = "Progress to next set",           .callback = menu_skip_set_callback };
  s_app.ui.mega_menu_items[n++] = (SimpleMenuItem){ .title = "Skip Exercise (Log 0)",   .subtitle = "Progress to next exercise",      .callback = menu_skip_callback };
  s_app.ui.mega_menu_items[n++] = (SimpleMenuItem){ .title = "Add Ex. (Voice)", .subtitle = "Append to routine", .callback = menu_quick_add_callback };

  s_app.ui.mega_menu_sections[0] = (SimpleMenuSection){ .num_items = n, .items = s_app.ui.mega_menu_items };

  Layer *w_layer = window_get_root_layer(window);
  GRect bounds   = layer_get_frame(w_layer);
  s_app.ui.mega_menu_layer = simple_menu_layer_create(bounds, window, s_app.ui.mega_menu_sections, 1, NULL);
  MenuLayer *internal = simple_menu_layer_get_menu_layer(s_app.ui.mega_menu_layer);
  menu_layer_set_normal_colors(internal,    get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(internal, get_theme_color(), get_bg_color());
  layer_add_child(w_layer, simple_menu_layer_get_layer(s_app.ui.mega_menu_layer));
}
static void menu_quick_add_callback(int index, void *ctx) {
  window_stack_remove(s_app.ui.mega_window, false);
  s_app.state.adding_exercise_to_slot = s_app.state.current_slot;
  if (!s_app.state.new_ex_dictation_session) {
    s_app.state.new_ex_dictation_session = dictation_session_create(128, new_exercise_dictation_callback, NULL);
  }
  dictation_session_start(s_app.state.new_ex_dictation_session);
}
static void mega_window_unload(Window *window) { simple_menu_layer_destroy(s_app.ui.mega_menu_layer); }
static void push_mega_window(void) {
  if (!s_app.ui.mega_window) {
    s_app.ui.mega_window = window_create();
    window_set_window_handlers(s_app.ui.mega_window,
      (WindowHandlers){ .load = mega_window_load, .unload = mega_window_unload });
  }
  window_set_background_color(s_app.ui.mega_window, get_bg_color());
  window_stack_push(s_app.ui.mega_window, true);
}

// ----------- Workout Window -----------
static void progress_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  if (s_app.state.total_workout_sets <= 0) return;

  int completed  = get_completed_sets();
  GColor track_color = is_dark_theme()
    ? PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack)
    : PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite);

#if defined(PBL_ROUND)
  int angle_end  = TRIG_MAX_ANGLE * completed / s_app.state.total_workout_sets;
  int inset_margin = (bounds.size.h <= 180) ? 0 : 4;
  GRect inset_bounds = grect_inset(bounds, GEdgeInsets(inset_margin));
  graphics_context_set_fill_color(ctx, track_color);
  graphics_fill_radial(ctx, inset_bounds, GOvalScaleModeFitCircle, 6, 0, TRIG_MAX_ANGLE);
  graphics_context_set_fill_color(ctx, get_theme_color());
  graphics_fill_radial(ctx, inset_bounds, GOvalScaleModeFitCircle, 6, 0, angle_end);
#else
  graphics_context_set_fill_color(ctx, track_color);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  int width = (bounds.size.w * completed) / s_app.state.total_workout_sets;
  graphics_context_set_fill_color(ctx, get_theme_color());
  graphics_fill_rect(ctx, GRect(0, 0, width, bounds.size.h), 0, GCornerNone);
#endif
}

static void workout_bg_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_stroke_color(ctx, is_dark_theme()
    ? PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite)
    : PBL_IF_COLOR_ELSE(GColorLightGray, GColorDarkGray));
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(0, s_app.geom.line1_y), GPoint(bounds.size.w, s_app.geom.line1_y));
  graphics_draw_line(ctx, GPoint(0, s_app.geom.line2_y), GPoint(bounds.size.w, s_app.geom.line2_y));
}

static void rest_bg_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, get_bg_color());
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, is_dark_theme()
    ? PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite)
    : PBL_IF_COLOR_ELSE(GColorLightGray, GColorDarkGray));
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
  s_app.ui.box_anim = NULL;
}
static void rest_anim_stopped_handler(Animation *animation, bool finished, void *context) {
  s_app.ui.rest_anim = NULL;
  
  // Secretly update the background layers now that the overlay is fully covering them!
  if (s_app.state.is_resting) {
    update_workout_ui(false);
  }
}

// ----------- Note Toast Overlay -----------
#define NOTE_TOAST_TEXT_PADDING 0
#define NOTE_TOAST_ACCENT_H     4
#define NOTE_TOAST_H_TALL       32
#define NOTE_TOAST_H_SHORT      24
#define NOTE_TOAST_H_TALL_RND   36
#define NOTE_TOAST_H_SHORT_RND  28
#define NOTE_TOAST_ANIM_MS      200
#define NOTE_TOAST_DISMISS_MS_8   8000
#define NOTE_TOAST_DISMISS_MS_15 15000
#define NOTE_TOAST_DISMISS_MS_20 20000
#define NOTE_TOAST_SCROLL_MS    50
#define NOTE_TOAST_SCROLL_PAUSE 30  // ticks (~1.5s at 50ms)

static int note_toast_height(GRect bounds) {
#if defined(PBL_ROUND)
  return (bounds.size.h > 180) ? NOTE_TOAST_H_TALL_RND : NOTE_TOAST_H_SHORT_RND;
#else
  return (bounds.size.h > 180) ? NOTE_TOAST_H_TALL : NOTE_TOAST_H_SHORT;
#endif
}

static void note_toast_bg_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
}

static void note_toast_acc_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, get_theme_color());
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
}

static void note_toast_text_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_context_set_fill_color(ctx, GColorBlack);

  bool is_scrolling = (s_app.ui.note_toast_text_width > bounds.size.w);
  GTextAlignment align = is_scrolling ? GTextAlignmentLeft : GTextAlignmentCenter;

  // Use a fixed line-height estimate for 14pt BOLD (~17px) and center it,
  // biased 1px toward the top so the visible glyphs sit just above the geometric
  // center of the toast strip.
  int16_t line_h = 17;
  int16_t y_off = (bounds.size.h - line_h) / 2 - 1;
  if (y_off < 0) y_off = 0;
  if (y_off + line_h > bounds.size.h) y_off = bounds.size.h - line_h;

  GRect text_rect;
  if (is_scrolling) {
    // Scrolling: left-aligned in an extended rect so text can slide leftward
    text_rect = GRect(s_app.ui.note_toast_scroll_offset, y_off,
                      bounds.size.w + 200, bounds.size.h);
  } else {
    // Static: center-aligned within the text layer (which spans the full toast)
    text_rect = GRect(0, y_off, bounds.size.w, bounds.size.h);
  }

  graphics_draw_text(ctx, s_app.ui.note_toast_text, font, text_rect,
    GTextOverflowModeFill, align, NULL);
}

static void note_toast_anim_stopped_handler(Animation *animation, bool finished, void *context) {
  s_app.ui.note_toast_anim = NULL;
  if (!finished) return;
  // After slide-out completes, hide and mark not visible.
  if (!s_app.ui.note_toast_visible) {
    layer_set_hidden(s_app.ui.note_toast_layer, true);
  }
}

static void note_toast_dismiss_cb(void *ctx) {
  s_app.ui.note_toast_dismiss_timer = NULL;
  hide_note_toast(true);
}

static void update_note_toast_visibility(void) {
  // Centralised visibility/dismiss logic driven by SK_NOTE_TOAST.
  // Modes 0/1/2: auto-dismiss with 8/15/20s timer; suppressed during rest.
  // Mode 3:      permanent during active sets, hidden during rest.
  // Mode 4:      permanent during rest, hidden during active sets.
  // Mode 5:      permanent always.
  // Mode 6:      manual. replacing the note view window. note toast auto-dismisses after 8s.
  int mode = s_app.settings.note_toast_mode;
  if (mode < 0) mode = 0;
  if (mode > 6) mode = 6;

  if (s_app.ui.note_toast_dismiss_timer) {
    app_timer_cancel(s_app.ui.note_toast_dismiss_timer);
    s_app.ui.note_toast_dismiss_timer = NULL;
  }
  
  if (mode == 6) {
    hide_note_toast(false);
    return;
  }

  if (mode <= 2) {
    if (s_app.state.is_resting) {
      hide_note_toast(false);
      return;
    }
    show_note_toast();
    if (s_app.ui.note_toast_visible) {
      int dismiss_ms = (mode == 1) ? NOTE_TOAST_DISMISS_MS_15
                      : (mode == 2) ? NOTE_TOAST_DISMISS_MS_20
                      : NOTE_TOAST_DISMISS_MS_8;
      s_app.ui.note_toast_dismiss_timer = app_timer_register(dismiss_ms, note_toast_dismiss_cb, NULL);
    }
  } else if (mode == 3) {
    if (s_app.state.is_resting) hide_note_toast(false);
    else                        show_note_toast();
  } else if (mode == 4) {
    if (s_app.state.is_resting) show_note_toast();
    else                        hide_note_toast(false);
  } else {
    // mode == 5: always on (when the current exercise has a note)
    show_note_toast();
  }
}

static void trigger_manual_note_toast(void) {
  // 1. If the Mega Menu is open, safely remove it to reveal the workout window underneath
  if (s_app.ui.mega_window && window_is_loaded(s_app.ui.mega_window)) {
    window_stack_remove(s_app.ui.mega_window, true); 
  }

  // 2. Trigger the toast animation
  show_note_toast();
  
  // 3. Set the manual 8-second override timer
  if (s_app.ui.note_toast_visible) {
    if (s_app.ui.note_toast_dismiss_timer) {
      app_timer_cancel(s_app.ui.note_toast_dismiss_timer);
    }
    s_app.ui.note_toast_dismiss_timer = app_timer_register(NOTE_TOAST_DISMISS_MS_8, note_toast_dismiss_cb, NULL);
  }
}

static void note_toast_scroll_cb(void *ctx) {
  s_app.ui.note_toast_scroll_timer = NULL;
  if (!s_app.ui.note_toast_visible) return;

  if (s_app.ui.note_toast_scroll_pause_until > 0) {
    s_app.ui.note_toast_scroll_pause_until--;
    s_app.ui.note_toast_scroll_timer = app_timer_register(NOTE_TOAST_SCROLL_MS, note_toast_scroll_cb, NULL);
    return;
  }

  // We scroll the text leftward: x_offset becomes more negative each tick.
  // End condition: the right edge of the text has passed the left edge of the visible area.
  if (-s_app.ui.note_toast_scroll_offset >= s_app.ui.note_toast_text_width + NOTE_TOAST_TEXT_PADDING) {
    s_app.ui.note_toast_scroll_offset = NOTE_TOAST_TEXT_PADDING;
    s_app.ui.note_toast_scroll_pause_until = NOTE_TOAST_SCROLL_PAUSE;
  } else {
    s_app.ui.note_toast_scroll_offset -= 1;
  }

  layer_mark_dirty(s_app.ui.note_toast_text_layer);
  s_app.ui.note_toast_scroll_timer = app_timer_register(NOTE_TOAST_SCROLL_MS, note_toast_scroll_cb, NULL);
}

static void show_note_toast(void) {
  if (!s_app.ui.workout_window) return;
  if (!s_app.ui.note_toast_layer) return;  // created in workout_window_load
  if (s_app.state.curr_ex_idx >= s_app.state.total_exercises) return;
  // NOTE: Previously this function early-returned when is_resting was true.
  // That guard was correct under the old "toast only during active sets" design
  // but breaks SK_NOTE_TOAST modes 4 (Perm. Rest) and 5 (Perm. Both), which
  // intentionally want the toast visible during rest. Visibility-during-rest
  // is now decided per-mode by update_note_toast_visibility() before calling
  // this function, so this guard is no longer needed.

  Exercise *ex = &s_app.state.exercises[s_app.state.curr_ex_idx];
  if (ex->comment[0] == '\0') {
    hide_note_toast(false);
    return;
  }

  Layer *w_layer = window_get_root_layer(s_app.ui.workout_window);
  GRect bounds   = layer_get_bounds(w_layer);
  int toast_h   = note_toast_height(bounds);

  // Copy text + measure
  snprintf(s_app.ui.note_toast_text, sizeof(s_app.ui.note_toast_text), "%s", ex->comment);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GSize text_size = graphics_text_layout_get_content_size(
    s_app.ui.note_toast_text, font, GRect(0, 0, 200, 40),
    GTextOverflowModeFill, GTextAlignmentLeft);
  s_app.ui.note_toast_text_width = text_size.w;

  // Animate the container from off-screen to on-screen.
  if (s_app.ui.note_toast_anim) {
    animation_unschedule(s_app.ui.note_toast_anim);
    s_app.ui.note_toast_anim = NULL;
  }
  // Cancel timers from any prior toast
  if (s_app.ui.note_toast_dismiss_timer) {
    app_timer_cancel(s_app.ui.note_toast_dismiss_timer);
    s_app.ui.note_toast_dismiss_timer = NULL;
  }
  if (s_app.ui.note_toast_scroll_timer) {
    app_timer_cancel(s_app.ui.note_toast_scroll_timer);
    s_app.ui.note_toast_scroll_timer = NULL;
  }

  s_app.ui.note_toast_scroll_offset = NOTE_TOAST_TEXT_PADDING;
  s_app.ui.note_toast_scroll_pause_until = 0;
  s_app.ui.note_toast_visible = true;
  layer_set_hidden(s_app.ui.note_toast_layer, false);
  layer_mark_dirty(s_app.ui.note_toast_text_layer);

  GRect on_screen  = GRect(0, bounds.size.h - toast_h, bounds.size.w, toast_h);
  GRect from_rect  = layer_get_frame(s_app.ui.note_toast_layer);
  layer_set_frame(s_app.ui.note_toast_layer, on_screen);

  PropertyAnimation *anim = property_animation_create_layer_frame(
    s_app.ui.note_toast_layer, &from_rect, &on_screen);
  s_app.ui.note_toast_anim = property_animation_get_animation(anim);
  animation_set_duration(s_app.ui.note_toast_anim, NOTE_TOAST_ANIM_MS);
  animation_set_curve(s_app.ui.note_toast_anim, AnimationCurveEaseInOut);
  animation_set_handlers(s_app.ui.note_toast_anim,
    (AnimationHandlers){ .stopped = note_toast_anim_stopped_handler }, NULL);
  animation_schedule(s_app.ui.note_toast_anim);

  // Auto-dismiss timer is scheduled by update_note_toast_visibility() so the
  // chosen SK_NOTE_TOAST mode can dictate the duration (8s/15s/20s) or omit
  // it entirely (persistent modes 3/4/5).

  // Schedule scroll if text is wider than visible area
  int16_t visible_w = layer_get_bounds(s_app.ui.note_toast_text_layer).size.w;
  if (s_app.ui.note_toast_text_width > visible_w) {
    s_app.ui.note_toast_scroll_pause_until = NOTE_TOAST_SCROLL_PAUSE;
    s_app.ui.note_toast_scroll_timer =
      app_timer_register(NOTE_TOAST_SCROLL_MS, note_toast_scroll_cb, NULL);
  }
}

static void hide_note_toast(bool animated) {
  if (!s_app.ui.note_toast_layer) return;
  if (!s_app.ui.note_toast_visible && !animated) return;
  if (!s_app.ui.note_toast_visible) {
    layer_set_hidden(s_app.ui.note_toast_layer, true);
    return;
  }

  if (s_app.ui.note_toast_dismiss_timer) {
    app_timer_cancel(s_app.ui.note_toast_dismiss_timer);
    s_app.ui.note_toast_dismiss_timer = NULL;
  }
  if (s_app.ui.note_toast_scroll_timer) {
    app_timer_cancel(s_app.ui.note_toast_scroll_timer);
    s_app.ui.note_toast_scroll_timer = NULL;
  }

  s_app.ui.note_toast_visible = false;
  s_app.ui.note_toast_text[0] = '\0';

  Layer *w_layer = window_get_root_layer(s_app.ui.workout_window);
  GRect bounds   = layer_get_bounds(w_layer);
  int toast_h   = note_toast_height(bounds);

  GRect off_screen = GRect(0, bounds.size.h, bounds.size.w, toast_h);
  GRect from_rect  = layer_get_frame(s_app.ui.note_toast_layer);

  if (!animated) {
    layer_set_frame(s_app.ui.note_toast_layer, off_screen);
    layer_set_hidden(s_app.ui.note_toast_layer, true);
    return;
  }

  if (s_app.ui.note_toast_anim) {
    animation_unschedule(s_app.ui.note_toast_anim);
    s_app.ui.note_toast_anim = NULL;
  }
  PropertyAnimation *anim = property_animation_create_layer_frame(
    s_app.ui.note_toast_layer, &from_rect, &off_screen);
  s_app.ui.note_toast_anim = property_animation_get_animation(anim);
  animation_set_duration(s_app.ui.note_toast_anim, NOTE_TOAST_ANIM_MS);
  animation_set_curve(s_app.ui.note_toast_anim, AnimationCurveEaseInOut);
  animation_set_handlers(s_app.ui.note_toast_anim,
    (AnimationHandlers){ .stopped = note_toast_anim_stopped_handler }, NULL);
  animation_schedule(s_app.ui.note_toast_anim);
}

static void animate_highlight_box(bool animated) {
  Layer *w_layer = window_get_root_layer(s_app.ui.workout_window);
  GRect bounds   = layer_get_bounds(w_layer);
  int half_w     = bounds.size.w / 2;
  bool is_tall   = (bounds.size.h > 180);

  int box_y_coord = s_app.geom.labels_y - (is_tall ? 4 : 2);
  int box_height  = (s_app.geom.target_y + 22) - box_y_coord + (is_tall ? 4 : 2);
  int offset_x    = 0;

#if defined(PBL_ROUND)
  if (bounds.size.h <= 180) {
    offset_x = 10; box_y_coord = s_app.geom.labels_y + 1;
    box_height = (s_app.geom.target_y + 25) - box_y_coord;
  } else { offset_x = 20; }
#endif

  bool is_bw = (s_app.state.curr_ex_idx < s_app.state.total_exercises)
               ? (s_app.state.exercises[s_app.state.curr_ex_idx].target_weight == 0) : false;
  int center_x;

  if (is_bw) {
    center_x = bounds.size.w / 2;
  } else {
    int left_center = (half_w / 2) + offset_x;
    int right_center = (half_w + (half_w / 2)) - offset_x;

    // Determine which side is Reps and which is Weight based on settings
    int reps_center = s_app.settings.swap_reps_weight ? right_center : left_center;
    int weight_center = s_app.settings.swap_reps_weight ? left_center : right_center;

    center_x = (s_app.state.edit_mode == 0) ? reps_center : weight_center;
  }

  int layer_w = s_app.geom.highlight_box_width + 8;
  GRect target_rect = GRect(center_x - (layer_w / 2), box_y_coord, layer_w, box_height);

  if (s_app.ui.box_anim) { animation_unschedule(s_app.ui.box_anim); s_app.ui.box_anim = NULL; }

  if (animated) {
    GRect current_rect = layer_get_frame(s_app.ui.highlight_layer);
    int current_x = current_rect.origin.x;
    int target_x  = target_rect.origin.x;

    if (current_x != target_x && current_rect.size.w > 0) {
      int direction = (target_x > current_x) ? 1 : -1;
      GRect overshoot_rect = target_rect;
      overshoot_rect.origin.x += (direction * 8);

      PropertyAnimation *anim1 = property_animation_create_layer_frame(s_app.ui.highlight_layer, &current_rect, &overshoot_rect);
      Animation *a1 = property_animation_get_animation(anim1);
      animation_set_duration(a1, 150);
      animation_set_curve(a1, AnimationCurveEaseOut);

      PropertyAnimation *anim2 = property_animation_create_layer_frame(s_app.ui.highlight_layer, &overshoot_rect, &target_rect);
      Animation *a2 = property_animation_get_animation(anim2);
      animation_set_duration(a2, 100);
      animation_set_curve(a2, AnimationCurveEaseInOut);

      s_app.ui.box_anim = animation_sequence_create(a1, a2, NULL);
    } else {
      PropertyAnimation *anim1 = property_animation_create_layer_frame(s_app.ui.highlight_layer, NULL, &target_rect);
      s_app.ui.box_anim = property_animation_get_animation(anim1);
      animation_set_duration(s_app.ui.box_anim, 200);
      animation_set_curve(s_app.ui.box_anim, AnimationCurveEaseOut);
    }
    animation_set_handlers(s_app.ui.box_anim, (AnimationHandlers){ .stopped = box_anim_stopped_handler }, NULL);
    animation_schedule(s_app.ui.box_anim);
  } else {
    layer_set_frame(s_app.ui.highlight_layer, target_rect);
  }
}

static void set_rest_overlay_state(bool is_resting, bool animated) {
  Layer *w_layer = window_get_root_layer(s_app.ui.workout_window);
  GRect bounds   = layer_get_bounds(w_layer);
  int rest_box_height = s_app.geom.line2_y - s_app.geom.line1_y;

  // Re-evaluate note-toast visibility whenever the rest state changes. Each
  // SK_NOTE_TOAST mode has different rules for resting vs. active sets.
  update_note_toast_visibility();

  GRect on_screen  = GRect(0, s_app.geom.line1_y, bounds.size.w, rest_box_height);
  GRect off_screen = GRect(0, bounds.size.h,       bounds.size.w, rest_box_height);
  GRect target_rect = is_resting ? on_screen : off_screen;

  if (animated) {
    if (is_resting) layer_set_hidden(s_app.ui.rest_overlay_layer, false);
    if (s_app.ui.rest_anim) { animation_unschedule(s_app.ui.rest_anim); s_app.ui.rest_anim = NULL; }

    PropertyAnimation *anim = property_animation_create_layer_frame(s_app.ui.rest_overlay_layer, NULL, &target_rect);
    s_app.ui.rest_anim = property_animation_get_animation(anim);
    animation_set_duration(s_app.ui.rest_anim, 300);
    animation_set_curve(s_app.ui.rest_anim, AnimationCurveEaseInOut);
    animation_set_handlers(s_app.ui.rest_anim, (AnimationHandlers){ .stopped = rest_anim_stopped_handler }, NULL);
    animation_schedule(s_app.ui.rest_anim);
  } else {
    layer_set_frame(s_app.ui.rest_overlay_layer, target_rect);
    if (!is_resting) layer_set_hidden(s_app.ui.rest_overlay_layer, true);
  }
}

static void update_workout_ui(bool animate_box) {
  if (s_app.state.curr_ex_idx >= s_app.state.total_exercises) return;

  Exercise *ex   = &s_app.state.exercises[s_app.state.curr_ex_idx];
  Layer *w_layer = window_get_root_layer(s_app.ui.workout_window);
  GRect bounds   = layer_get_bounds(w_layer);

  // 1. PRE-CALCULATE TARGETS (Needed for both background and Rest Overlay)
  int active_target_weight = ex->target_weight;
  int active_target_reps   = ex->target_reps;

  if (ex->modifier == 1 && (ex->current_set % 2 == 0)) {
    if (ex->target_weight == 0) {
        active_target_reps = ex->actual_reps[ex->current_set - 2];
        active_target_reps = (active_target_reps * (100 - s_app.settings.drop_set_pct)) / 100;
    } else {
        active_target_weight = ex->actual_weight[ex->current_set - 2];
        active_target_weight = (active_target_weight * (100 - s_app.settings.drop_set_pct)) / 100;
    }
  }

  // =====================================================================
  // 2. BACKGROUND UPDATES (Always runs first so the exercise
  // name/sets/reps/weight are fully rendered before the rest
  // overlay configures its prep screen on top.)
  // =====================================================================
  bool is_tall   = (bounds.size.h > 180);
  int name_len   = strlen(ex->name);

  if (is_tall) {
    if      (name_len > 14) text_layer_set_font(s_app.ui.exercise_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    else if (name_len > 10) text_layer_set_font(s_app.ui.exercise_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    else                    text_layer_set_font(s_app.ui.exercise_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  } else {
    if (name_len > 10) text_layer_set_font(s_app.ui.exercise_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    else               text_layer_set_font(s_app.ui.exercise_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  }
  text_layer_set_text(s_app.ui.exercise_layer, ex->name);

  static char next_buf[64];
  bool is_second_half = (s_app.state.curr_ex_idx > 0 &&
                         s_app.state.exercises[s_app.state.curr_ex_idx - 1].modifier == 2);

  int giant_anchor = -1;
  int giant_pos    = 0;
  if (ex->modifier == 7 && s_app.state.curr_ex_idx + 2 < s_app.state.total_exercises) {
    giant_anchor = s_app.state.curr_ex_idx;
    giant_pos    = 0;
  } else if (s_app.state.curr_ex_idx > 0 &&
             s_app.state.exercises[s_app.state.curr_ex_idx - 1].modifier == 7 &&
             s_app.state.curr_ex_idx + 1 < s_app.state.total_exercises) {
    giant_anchor = s_app.state.curr_ex_idx - 1;
    giant_pos    = 1;
  } else if (s_app.state.curr_ex_idx >= 2 &&
             s_app.state.exercises[s_app.state.curr_ex_idx - 2].modifier == 7) {
    giant_anchor = s_app.state.curr_ex_idx - 2;
    giant_pos    = 2;
  }

  if (giant_anchor >= 0) {
    int anchor_current = s_app.state.exercises[giant_anchor].current_set;
    int anchor_target  = s_app.state.exercises[giant_anchor].target_sets;
    bool is_last_set = (giant_pos == 2) && (anchor_current >= anchor_target);
    int next_idx;
    if (is_last_set) {
      next_idx = giant_anchor + 3;
    } else {
      next_idx = giant_anchor + ((giant_pos + 1) % 3);
    }
    if (next_idx < s_app.state.total_exercises) {
      snprintf(next_buf, sizeof(next_buf), "NEXT: %s", s_app.state.exercises[next_idx].name);
    } else {
      snprintf(next_buf, sizeof(next_buf), "NEXT: FINISH!");
    }
  } else if (is_second_half && ex->current_set < ex->target_sets) {
    snprintf(next_buf, sizeof(next_buf), "NEXT: %s", s_app.state.exercises[s_app.state.curr_ex_idx - 1].name);
  } else if (s_app.state.curr_ex_idx + 1 < s_app.state.total_exercises) {
    snprintf(next_buf, sizeof(next_buf), "NEXT: %s", s_app.state.exercises[s_app.state.curr_ex_idx + 1].name);
  } else {
    snprintf(next_buf, sizeof(next_buf), "NEXT: FINISH!");
  }
  text_layer_set_text(s_app.ui.next_exercise_layer, next_buf);

  static char set_buf[32];
  if (ex->modifier == 1 && (ex->current_set % 2 == 0)) {
    snprintf(set_buf, sizeof(set_buf), "Set %d of %d (DROP)", ex->current_set, ex->target_sets);
  } else if (ex->modifier == 3) {
    snprintf(set_buf, sizeof(set_buf), "Set %d of %d (WARM)", ex->current_set, ex->target_sets);
  } else {
    snprintf(set_buf, sizeof(set_buf), "Set %d of %d", ex->current_set, ex->target_sets);
  }
  text_layer_set_text(s_app.ui.set_layer, set_buf);
  text_layer_set_text(s_app.ui.label_reps_layer, (ex->modifier == 6) ? "Secs" : "Reps");

  static char t_reps_buf[32], t_weight_buf[32], reps_buf[16], weight_buf[16];
  snprintf(t_reps_buf,   sizeof(t_reps_buf),   "Target: %d", active_target_reps);
  snprintf(reps_buf,     sizeof(reps_buf),     "%d",          s_app.state.temp_reps);
  snprintf(t_weight_buf, sizeof(t_weight_buf), "Target: %d", active_target_weight);
  snprintf(weight_buf,   sizeof(weight_buf),   "%d",          s_app.state.temp_weight);

  text_layer_set_text(s_app.ui.label_weight_layer,  s_app.settings.weight_unit_idx == 0 ? "Weight (kg)" : "Weight (lbs)");
  text_layer_set_text(s_app.ui.target_reps_layer,   t_reps_buf);
  text_layer_set_text(s_app.ui.target_weight_layer, t_weight_buf);
  text_layer_set_text(s_app.ui.actual_reps_layer,   reps_buf);
  text_layer_set_text(s_app.ui.actual_weight_layer, weight_buf);

  bool is_bw = (ex->target_weight == 0);
  layer_set_hidden(text_layer_get_layer(s_app.ui.label_weight_layer),  is_bw);
  layer_set_hidden(text_layer_get_layer(s_app.ui.actual_weight_layer), is_bw);
  layer_set_hidden(text_layer_get_layer(s_app.ui.target_weight_layer), is_bw);

  int offset_x = 0;
#if defined(PBL_ROUND)
  offset_x = (bounds.size.h <= 180) ? 10 : 20;
#endif
  int half_w = bounds.size.w / 2;

  // Determine physical X coordinates
  int left_x = offset_x;
  int right_x = half_w - offset_x;

  // Route Reps and Weight to Left/Right based on the user setting
  int reps_x = s_app.settings.swap_reps_weight ? right_x : left_x;
  int weight_x = s_app.settings.swap_reps_weight ? left_x : right_x;

  if (is_bw) {
    layer_set_frame(text_layer_get_layer(s_app.ui.label_reps_layer),  GRect(0, s_app.geom.labels_y, bounds.size.w, 20));
    layer_set_frame(text_layer_get_layer(s_app.ui.actual_reps_layer), GRect(0, s_app.geom.actual_y,  bounds.size.w, 40));
    layer_set_frame(text_layer_get_layer(s_app.ui.target_reps_layer), GRect(0, s_app.geom.target_y,  bounds.size.w, 22));
  } else {
    layer_set_frame(text_layer_get_layer(s_app.ui.label_reps_layer),  GRect(reps_x, s_app.geom.labels_y, half_w, 20));
    layer_set_frame(text_layer_get_layer(s_app.ui.actual_reps_layer), GRect(reps_x, s_app.geom.actual_y,  half_w, 40));
    layer_set_frame(text_layer_get_layer(s_app.ui.target_reps_layer), GRect(reps_x, s_app.geom.target_y,  half_w, 22));

    layer_set_frame(text_layer_get_layer(s_app.ui.label_weight_layer),  GRect(weight_x, s_app.geom.labels_y, half_w, 20));
    layer_set_frame(text_layer_get_layer(s_app.ui.actual_weight_layer), GRect(weight_x, s_app.geom.actual_y,  half_w, 40));
    layer_set_frame(text_layer_get_layer(s_app.ui.target_weight_layer), GRect(weight_x, s_app.geom.target_y,  half_w, 22));
  }

  GColor active_color   = get_theme_color();
  GColor inactive_color = is_dark_theme()
    ? PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
    : PBL_IF_COLOR_ELSE(GColorDarkGray,  GColorBlack);

  text_layer_set_text_color(s_app.ui.actual_reps_layer,   (s_app.state.edit_mode == 0) ? active_color   : inactive_color);
  text_layer_set_text_color(s_app.ui.actual_weight_layer, (s_app.state.edit_mode == 0) ? inactive_color : active_color);

  const char *label_str  = (s_app.state.edit_mode == 0) ? "Reps" : (s_app.settings.weight_unit_idx == 0 ? "Weight (kg)" : "Weight (lbs)");
  const char *actual_str = (s_app.state.edit_mode == 0) ? reps_buf   : weight_buf;
  const char *target_str = (s_app.state.edit_mode == 0) ? t_reps_buf : t_weight_buf;

  GRect measure_rect = GRect(0, 0, bounds.size.w, 40);
  GSize s1 = graphics_text_layout_get_content_size(label_str,  fonts_get_system_font(FONT_KEY_GOTHIC_14),      measure_rect, GTextOverflowModeFill, GTextAlignmentCenter);
  GSize s2 = graphics_text_layout_get_content_size(actual_str, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK), measure_rect, GTextOverflowModeFill, GTextAlignmentCenter);
  GSize s3 = graphics_text_layout_get_content_size(target_str, fonts_get_system_font(FONT_KEY_GOTHIC_18),       measure_rect, GTextOverflowModeFill, GTextAlignmentCenter);

  int max_w = s1.w;
  if (s2.w > max_w) max_w = s2.w;
  if (s3.w > max_w) max_w = s3.w;
  s_app.geom.highlight_box_width = max_w + 16;
  if (s_app.geom.highlight_box_width > half_w - 4) s_app.geom.highlight_box_width = half_w - 4;

  animate_highlight_box(animate_box);

  // Show/update note toast on exercise change. Visibility (auto-dismiss or
  // persistent) is governed by SK_NOTE_TOAST via update_note_toast_visibility.
  if (s_app.state.curr_ex_idx != s_app.state.note_toast_prev_ex_idx) {
    s_app.state.note_toast_prev_ex_idx = s_app.state.curr_ex_idx;
    update_note_toast_visibility();
  }
  // =====================================================================
  // 3. ALWAYS UPDATE REST OVERLAY (Configured after background so it
  // layers correctly over the fully rendered workout screen.)
  // =====================================================================
  text_layer_set_text_color(s_app.ui.rest_time_layer, get_theme_color());

  if (s_app.state.is_resting) {
    static char instant_rest_buf[16];
    if (s_app.settings.dynamic_hr_target > 0) {
      snprintf(instant_rest_buf, sizeof(instant_rest_buf), "--"); // Live HR will populate on the next tick
    } else {
      snprintf(instant_rest_buf, sizeof(instant_rest_buf), "%d", s_app.state.rest_seconds_remaining);
    }
    text_layer_set_text(s_app.ui.rest_time_layer, instant_rest_buf);

    // Dynamic layout adjustment for rest types
    // Ensure the timer is ALWAYS perfectly centered, full width
    GRect time_frame = layer_get_frame(text_layer_get_layer(s_app.ui.rest_time_layer));
    time_frame.origin.x = 0;
    time_frame.size.w = bounds.size.w;
    layer_set_frame(text_layer_get_layer(s_app.ui.rest_time_layer), time_frame);
    text_layer_set_text_alignment(s_app.ui.rest_time_layer, GTextAlignmentCenter);

    bool show_prep = false;

    // Evaluate if we should show the Prep weight based on the UPCOMING exercise
    if (s_app.state.curr_ex_idx < s_app.state.total_exercises) {
      // Detect if the upcoming exercise is part of a Superset (2) or Giant Set (7)
      bool is_linked = (ex->modifier == 2 || ex->modifier == 7) ||
                       (s_app.state.curr_ex_idx > 0 && (s_app.state.exercises[s_app.state.curr_ex_idx - 1].modifier == 2 || s_app.state.exercises[s_app.state.curr_ex_idx - 1].modifier == 7)) ||
                       (s_app.state.curr_ex_idx > 1 && s_app.state.exercises[s_app.state.curr_ex_idx - 2].modifier == 7);

      // Show Prep if it's the first set of ANY exercise, OR if we are constantly switching in a linked set
      if (ex->current_set == 1 || is_linked) {
        show_prep = true;
      }
    }

    if (show_prep) {
      // Define the "safe zone" dynamically for all screen sizes and shapes
      int timer_half_width = 28; 
      int dynamic_padding = bounds.size.w / 20; 
      int safe_start_x = (bounds.size.w / 2) + timer_half_width + dynamic_padding;
      int right_margin = PBL_IF_ROUND_ELSE(16, 2); 

      GRect prep_frame = layer_get_frame(text_layer_get_layer(s_app.ui.rest_prep_layer));
      prep_frame.origin.x = safe_start_x;
      prep_frame.size.w = bounds.size.w - safe_start_x - right_margin;
      
      layer_set_frame(text_layer_get_layer(s_app.ui.rest_prep_layer), prep_frame);
      text_layer_set_text_alignment(s_app.ui.rest_prep_layer, GTextAlignmentCenter);
      
      // Build and display prep text
      static char prep_buf[32];
      if (active_target_weight == 0) {
        snprintf(prep_buf, sizeof(prep_buf), "Prep:\nBW");
      } else {
        snprintf(prep_buf, sizeof(prep_buf), "Prep:\n%d%s", active_target_weight, s_app.settings.weight_unit_idx == 0 ? "kg" : "lb");
      }
      text_layer_set_text(s_app.ui.rest_prep_layer, prep_buf);
      layer_set_hidden(text_layer_get_layer(s_app.ui.rest_prep_layer), false);
      
    } else {
      // Regular set rest -> just hide the prep text
      layer_set_hidden(text_layer_get_layer(s_app.ui.rest_prep_layer), true);
    }
  }
}

static void wo_up_click(ClickRecognizerRef r, void *ctx) {
  if (s_app.state.is_resting) return;
  if (s_app.state.is_timed_active) return;
  if (s_app.state.edit_mode == 0) s_app.state.temp_reps++; else s_app.state.temp_weight++;
  update_workout_ui(false);
}
static void wo_down_click(ClickRecognizerRef r, void *ctx) {
  if (s_app.state.is_resting) return;
  if (s_app.state.is_timed_active) return;
  if (s_app.state.edit_mode == 0 && s_app.state.temp_reps   > 0) s_app.state.temp_reps--;
  else if (s_app.state.edit_mode == 1 && s_app.state.temp_weight > 0) s_app.state.temp_weight--;
  update_workout_ui(false);
}
static void wo_up_long_click(ClickRecognizerRef r, void *ctx)   { execute_shortcut(s_app.settings.shortcut_up); }
static void wo_down_long_click(ClickRecognizerRef r, void *ctx) { execute_shortcut(s_app.settings.shortcut_down); }

static void wo_select_short_click(ClickRecognizerRef r, void *ctx) {
  if (s_app.state.is_resting) { skip_rest(); return; }
  if (s_app.state.curr_ex_idx < s_app.state.total_exercises &&
      s_app.state.exercises[s_app.state.curr_ex_idx].modifier == 6) {
    s_app.state.is_timed_active = !s_app.state.is_timed_active;
  } else if (s_app.state.curr_ex_idx < s_app.state.total_exercises &&
             s_app.state.exercises[s_app.state.curr_ex_idx].target_weight == 0) {
    s_app.state.edit_mode = 0;
  } else {
    s_app.state.edit_mode = !s_app.state.edit_mode;
  }
  update_workout_ui(true);
}
static void wo_select_long_click(ClickRecognizerRef r, void *ctx)   { execute_shortcut(s_app.settings.shortcut_select); }
static void wo_select_double_click(ClickRecognizerRef r, void *ctx) {
  if (s_app.state.is_resting) return;
  push_mega_window();
}
static void wo_back_click(ClickRecognizerRef r, void *ctx) {
  s_app.state.is_paused = true;
  push_exit_window();
}

static void wo_click_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP,   wo_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, wo_down_click);
  // FIX #12: long_press_ms is read at push time (window is recreated each session), so it is fresh
  window_long_click_subscribe(BUTTON_ID_UP,     s_app.settings.long_press_ms, wo_up_long_click,     NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN,   s_app.settings.long_press_ms, wo_down_long_click,   NULL);
  window_single_click_subscribe(BUTTON_ID_SELECT, wo_select_short_click);
  window_multi_click_subscribe(BUTTON_ID_SELECT, 2, 2, 300, true, wo_select_double_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, s_app.settings.long_press_ms, wo_select_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, wo_back_click);
}

static void workout_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds   = layer_get_bounds(w_layer);
  int half_w     = bounds.size.w / 2;
  bool is_tall   = (bounds.size.h > 180);

  int top_margin, set_y, offset_x = 0;

#if defined(PBL_ROUND)
  if (bounds.size.h <= 180) {
    s_app.geom.line1_y = 46; s_app.geom.line2_y = 140;
    top_margin = 16; offset_x = 10; set_y = s_app.geom.line1_y + 1;
    s_app.geom.labels_y = s_app.geom.line1_y + 23;
    s_app.geom.actual_y = s_app.geom.labels_y + 12;
    s_app.geom.target_y = s_app.geom.actual_y + 28;
  } else {
    s_app.geom.line1_y = 75; s_app.geom.line2_y = 205;
    top_margin = 25; offset_x = 20; set_y = s_app.geom.line1_y + 1;
    s_app.geom.labels_y = s_app.geom.line1_y + 36;
    s_app.geom.actual_y = s_app.geom.labels_y + 20;
    s_app.geom.target_y = s_app.geom.actual_y + 42;
  }
#else
  s_app.geom.line1_y = is_tall ? 60 : 48;
  s_app.geom.line2_y = is_tall ? 195 : 144;
  top_margin = is_tall ? 10 : 0;
  set_y = s_app.geom.line1_y + (is_tall ? 5 : 0);
  s_app.geom.labels_y = s_app.geom.line1_y + (is_tall ? 40 : 24);
  s_app.geom.actual_y = s_app.geom.labels_y + (is_tall ? 20 : 14);
  s_app.geom.target_y = s_app.geom.actual_y + (is_tall ? 36 : 30);
#endif

  s_app.ui.workout_bg_layer = layer_create(bounds);
  layer_set_update_proc(s_app.ui.workout_bg_layer, workout_bg_update_proc);
  layer_add_child(w_layer, s_app.ui.workout_bg_layer);

  s_app.ui.highlight_layer = layer_create(GRect(0, 0, 0, 0));
  layer_set_update_proc(s_app.ui.highlight_layer, highlight_update_proc);
  layer_add_child(w_layer, s_app.ui.highlight_layer);

#if defined(PBL_ROUND)
  if (bounds.size.h <= 180) {
    s_app.ui.exercise_layer      = build_text_layer(GRect(10, top_margin, bounds.size.w - 20, 32), FONT_KEY_GOTHIC_18_BOLD, PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorBlack), GTextAlignmentCenter, w_layer);
    s_app.ui.next_exercise_layer = build_text_layer(GRect(0, 0, 0, 0), FONT_KEY_GOTHIC_14, GColorClear, GTextAlignmentCenter, w_layer);
    s_app.ui.timer_layer         = build_text_layer(GRect(10, s_app.geom.line2_y + 2, 75, 24), FONT_KEY_GOTHIC_18_BOLD, GColorBlack, GTextAlignmentRight, w_layer);
    s_app.ui.clock_layer         = build_text_layer(GRect(95, s_app.geom.line2_y + 2, 75, 24), FONT_KEY_GOTHIC_18_BOLD, GColorBlack, GTextAlignmentLeft,  w_layer);
  } else {
    s_app.ui.next_exercise_layer = build_text_layer(GRect(10, top_margin,      bounds.size.w - 20, 20), FONT_KEY_GOTHIC_14,      GColorBlack, GTextAlignmentCenter, w_layer);
    s_app.ui.exercise_layer      = build_text_layer(GRect(10, top_margin + 17, bounds.size.w - 20, 32), FONT_KEY_GOTHIC_24_BOLD, PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorBlack), GTextAlignmentCenter, w_layer);
    s_app.ui.timer_layer         = build_text_layer(GRect(20,  s_app.geom.line2_y + 5, 105, 28), FONT_KEY_GOTHIC_24_BOLD, GColorBlack, GTextAlignmentRight, w_layer);
    s_app.ui.clock_layer         = build_text_layer(GRect(135, s_app.geom.line2_y + 5, 105, 28), FONT_KEY_GOTHIC_24_BOLD, GColorBlack, GTextAlignmentLeft,  w_layer);
  }
#else
  if (is_tall) {
    s_app.ui.exercise_layer      = build_text_layer(GRect(5, top_margin,      bounds.size.w - 10, 32), FONT_KEY_GOTHIC_28_BOLD, PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorBlack), GTextAlignmentLeft, w_layer);
    s_app.ui.next_exercise_layer = build_text_layer(GRect(5, top_margin + 28, bounds.size.w - 10, 20), FONT_KEY_GOTHIC_14,      GColorBlack, GTextAlignmentLeft, w_layer);
    s_app.ui.hr_layer            = build_text_layer(GRect(5,   s_app.geom.line2_y + 5, 70, 24), FONT_KEY_GOTHIC_18_BOLD, PBL_IF_COLOR_ELSE(GColorRed, GColorBlack), GTextAlignmentLeft, w_layer);
    s_app.ui.timer_layer         = build_text_layer(GRect(75,  s_app.geom.line2_y + 5, 50, 24), FONT_KEY_GOTHIC_18_BOLD, GColorBlack, GTextAlignmentCenter, w_layer);
    s_app.ui.clock_layer         = build_text_layer(GRect(125, s_app.geom.line2_y + 5, 70, 24), FONT_KEY_GOTHIC_18_BOLD, GColorBlack, GTextAlignmentRight,  w_layer);
  } else {
    s_app.ui.exercise_layer      = build_text_layer(GRect(2, 6,  bounds.size.w - 4, 28), FONT_KEY_GOTHIC_24_BOLD, GColorBlack, GTextAlignmentLeft, w_layer);
    s_app.ui.next_exercise_layer = build_text_layer(GRect(2, 28, bounds.size.w - 4, 20), FONT_KEY_GOTHIC_14,      GColorBlack, GTextAlignmentLeft, w_layer);
    s_app.ui.hr_layer            = build_text_layer(GRect(2,  s_app.geom.line2_y + 4, 52, 20), FONT_KEY_GOTHIC_14_BOLD, GColorBlack, GTextAlignmentLeft,   w_layer);
    s_app.ui.timer_layer         = build_text_layer(GRect(54, s_app.geom.line2_y + 4, 36, 20), FONT_KEY_GOTHIC_14_BOLD, GColorBlack, GTextAlignmentCenter, w_layer);
    s_app.ui.clock_layer         = build_text_layer(GRect(90, s_app.geom.line2_y + 4, 52, 20), FONT_KEY_GOTHIC_14_BOLD, GColorBlack, GTextAlignmentRight,  w_layer);
  }
#endif

  s_app.ui.set_layer           = build_text_layer(GRect(0, set_y,             bounds.size.w, 24), is_tall ? FONT_KEY_GOTHIC_24 : FONT_KEY_GOTHIC_18, GColorBlack, GTextAlignmentCenter, w_layer);
  s_app.ui.label_reps_layer    = build_text_layer(GRect(offset_x,             s_app.geom.labels_y, half_w, 20), FONT_KEY_GOTHIC_14, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack), GTextAlignmentCenter, w_layer);
  s_app.ui.label_weight_layer  = build_text_layer(GRect(half_w - offset_x,   s_app.geom.labels_y, half_w, 20), FONT_KEY_GOTHIC_14, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack), GTextAlignmentCenter, w_layer);
  s_app.ui.actual_reps_layer   = build_text_layer(GRect(offset_x,             s_app.geom.actual_y,  half_w, 40), FONT_KEY_BITHAM_30_BLACK, GColorBlack, GTextAlignmentCenter, w_layer);
  s_app.ui.actual_weight_layer = build_text_layer(GRect(half_w - offset_x,   s_app.geom.actual_y,  half_w, 40), FONT_KEY_BITHAM_30_BLACK, GColorBlack, GTextAlignmentCenter, w_layer);
  s_app.ui.target_reps_layer   = build_text_layer(GRect(offset_x,             s_app.geom.target_y,  half_w, 22), FONT_KEY_GOTHIC_18, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack), GTextAlignmentCenter, w_layer);
  s_app.ui.target_weight_layer = build_text_layer(GRect(half_w - offset_x,   s_app.geom.target_y,  half_w, 22), FONT_KEY_GOTHIC_18, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack), GTextAlignmentCenter, w_layer);

  int rest_box_height = s_app.geom.line2_y - s_app.geom.line1_y;
  s_app.ui.rest_overlay_layer = layer_create(GRect(0, s_app.geom.line1_y, bounds.size.w, rest_box_height));
  layer_set_update_proc(s_app.ui.rest_overlay_layer, rest_bg_update_proc);

  int r_title_y, r_time_y, r_skip_y;
#if defined(PBL_ROUND)
  if (bounds.size.h <= 180) {
    r_title_y = (rest_box_height * 5)  / 100;
    r_time_y  = (rest_box_height * 30) / 100;
    r_skip_y  = (rest_box_height * 75) / 100;
  } else {
    r_title_y = 10; r_time_y = 45; r_skip_y = 105;
  }
#else
  r_title_y = is_tall ? 10 : 5;
  r_time_y  = is_tall ? 45 : 30;
  r_skip_y  = is_tall ? 100 : 75;
#endif

  s_app.ui.rest_title_layer = build_text_layer(GRect(0, r_title_y, bounds.size.w, 30),
    FONT_KEY_GOTHIC_24_BOLD, GColorBlack, GTextAlignmentCenter, s_app.ui.rest_overlay_layer);
  text_layer_set_text(s_app.ui.rest_title_layer, "REST");
  
  // The timer layer (starts full-width, centered)
  s_app.ui.rest_time_layer = build_text_layer(GRect(0, r_time_y, bounds.size.w, 45),
    FONT_KEY_BITHAM_42_BOLD, PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack), GTextAlignmentCenter, s_app.ui.rest_overlay_layer);
    
  // The prep layer (dynamically scaled for large screens)
  bool is_large_screen = (bounds.size.w >= 200);
  const char *prep_font = is_large_screen ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD;
  int prep_h = is_large_screen ? 64 : 50;
  int prep_y_off = is_large_screen ? -2 : 2;

  s_app.ui.rest_prep_layer = build_text_layer(GRect(bounds.size.w / 2, r_time_y + prep_y_off, bounds.size.w / 2, prep_h),
    prep_font, GColorBlack, GTextAlignmentLeft, s_app.ui.rest_overlay_layer);
  layer_set_hidden(text_layer_get_layer(s_app.ui.rest_prep_layer), true);
  
  s_app.ui.rest_skip_layer = build_text_layer(GRect(0, r_skip_y, bounds.size.w, 20),
    FONT_KEY_GOTHIC_18, GColorBlack, GTextAlignmentCenter, s_app.ui.rest_overlay_layer);
  text_layer_set_text(s_app.ui.rest_skip_layer, "[Select] to Skip");

  layer_add_child(w_layer, s_app.ui.rest_overlay_layer);

  // ----------- Note Toast (bottom-anchored) -----------
  int toast_h = note_toast_height(bounds);
  int toast_w = bounds.size.w;
  int text_y  = NOTE_TOAST_ACCENT_H;
  int text_h  = toast_h - NOTE_TOAST_ACCENT_H;

  GRect toast_frame = GRect(0, bounds.size.h, toast_w, toast_h);  // start off-screen
  s_app.ui.note_toast_layer = layer_create(toast_frame);
  layer_set_clips(s_app.ui.note_toast_layer, true);
  layer_set_update_proc(s_app.ui.note_toast_layer, note_toast_bg_update_proc);
  layer_add_child(w_layer, s_app.ui.note_toast_layer);

  s_app.ui.note_toast_acc_layer = layer_create(
    GRect(0, 0, toast_w, NOTE_TOAST_ACCENT_H));
  layer_set_update_proc(s_app.ui.note_toast_acc_layer, note_toast_acc_update_proc);
  layer_add_child(s_app.ui.note_toast_layer, s_app.ui.note_toast_acc_layer);

  // Scrolling text container spans the full toast width
  s_app.ui.note_toast_text_layer = layer_create(GRect(0, text_y, toast_w, text_h));
  layer_set_clips(s_app.ui.note_toast_text_layer, true);
  layer_set_update_proc(s_app.ui.note_toast_text_layer, note_toast_text_update_proc);
  layer_add_child(s_app.ui.note_toast_layer, s_app.ui.note_toast_text_layer);

  s_app.ui.note_toast_text[0] = '\0';
  s_app.ui.note_toast_text_width = 0;
  s_app.ui.note_toast_scroll_offset = NOTE_TOAST_TEXT_PADDING;
  s_app.ui.note_toast_scroll_pause_until = 0;
  s_app.ui.note_toast_dismiss_timer = NULL;
  s_app.ui.note_toast_scroll_timer = NULL;
  s_app.ui.note_toast_anim = NULL;
  s_app.ui.note_toast_visible = false;
  s_app.state.note_toast_prev_ex_idx = -1;  // ensure first update_workout_ui shows toast if applicable

#if defined(PBL_ROUND)
  s_app.ui.progress_layer = layer_create(bounds);
#else
  s_app.ui.progress_layer = layer_create(GRect(0, 0, bounds.size.w, 6));
#endif
  layer_set_update_proc(s_app.ui.progress_layer, progress_update_proc);
  layer_add_child(w_layer, s_app.ui.progress_layer);

  s_app.state.edit_mode = (s_app.state.exercises[s_app.state.curr_ex_idx].target_weight == 0) ? 0 : s_app.settings.swap_reps_weight;
  s_app.state.cached_completed_sets = 0;

#if defined(PBL_HEALTH)
  {
    HealthServiceAccessibilityMask mask = health_service_metric_accessible(
      HealthMetricHeartRateBPM, time(NULL), time(NULL));
    s_app.state.hr_supported = !(mask & HealthServiceAccessibilityMaskNotSupported);
  }
#else
  s_app.state.hr_supported = false;
#endif

  s_app.state.is_24h = clock_is_24h_style();

  if (s_app.state.rest_seconds_remaining > 0) {
    s_app.state.is_resting = true;
    set_rest_overlay_state(true, false);
  } else {
    s_app.state.is_resting = false;
    layer_set_hidden(s_app.ui.rest_overlay_layer, true);
  }

  update_workout_ui(false);
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
}

static void workout_window_unload(Window *window) {
  // FIX #10: unsubscribe here only handles abnormal exit (e.g. deinit while workout active).
  // Normal workout-end unsubscribes in summary_window_load.
  tick_timer_service_unsubscribe();

  if (s_app.ui.box_anim)  { animation_unschedule(s_app.ui.box_anim);  s_app.ui.box_anim  = NULL; }
  if (s_app.ui.rest_anim) { animation_unschedule(s_app.ui.rest_anim); s_app.ui.rest_anim = NULL; }
  if (s_app.ui.note_toast_anim) { animation_unschedule(s_app.ui.note_toast_anim); s_app.ui.note_toast_anim = NULL; }
  if (s_app.ui.note_toast_dismiss_timer) { app_timer_cancel(s_app.ui.note_toast_dismiss_timer); s_app.ui.note_toast_dismiss_timer = NULL; }
  if (s_app.ui.note_toast_scroll_timer)   { app_timer_cancel(s_app.ui.note_toast_scroll_timer);   s_app.ui.note_toast_scroll_timer   = NULL; }

  layer_destroy(s_app.ui.progress_layer);
  layer_destroy(s_app.ui.workout_bg_layer);
  text_layer_destroy(s_app.ui.timer_layer);
  text_layer_destroy(s_app.ui.clock_layer);
  text_layer_destroy(s_app.ui.exercise_layer);
  text_layer_destroy(s_app.ui.next_exercise_layer);
  text_layer_destroy(s_app.ui.set_layer);
  text_layer_destroy(s_app.ui.label_reps_layer);
  text_layer_destroy(s_app.ui.label_weight_layer);
  text_layer_destroy(s_app.ui.target_reps_layer);
  text_layer_destroy(s_app.ui.target_weight_layer);
  text_layer_destroy(s_app.ui.actual_reps_layer);
  text_layer_destroy(s_app.ui.actual_weight_layer);
#if !defined(PBL_ROUND)
  text_layer_destroy(s_app.ui.hr_layer);
#endif
  layer_destroy(s_app.ui.rest_overlay_layer);
  text_layer_destroy(s_app.ui.rest_title_layer);
  text_layer_destroy(s_app.ui.rest_time_layer);
  text_layer_destroy(s_app.ui.rest_prep_layer);
  text_layer_destroy(s_app.ui.rest_skip_layer);
  layer_destroy(s_app.ui.highlight_layer);
  layer_destroy(s_app.ui.note_toast_text_layer);
  layer_destroy(s_app.ui.note_toast_acc_layer);
  layer_destroy(s_app.ui.note_toast_layer);
}

static void push_workout_window(void) {
  if (!s_app.ui.workout_window) {
    s_app.ui.workout_window = window_create();
    window_set_click_config_provider(s_app.ui.workout_window, wo_click_provider);
    window_set_window_handlers(s_app.ui.workout_window,
      (WindowHandlers){ .load = workout_window_load, .unload = workout_window_unload });
  }
  window_set_background_color(s_app.ui.workout_window, get_bg_color());
  window_stack_push(s_app.ui.workout_window, true);
}

// ----------- Main Menu Window -----------
static uint16_t menu_get_num_rows_callback(MenuLayer *ml, uint16_t section, void *data) {
  int rows = s_app.storage.active_slots;
  if (s_app.state.has_resume)              rows++;
  if (s_app.storage.active_slots < MAX_SLOTS) rows++;
  rows++; // Settings row
  return (uint16_t)rows;
}

static void menu_draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  int i = cell_index->row;
  if (s_app.state.has_resume) {
    if (i == 0) { menu_cell_basic_draw(ctx, cell_layer, "Resume Workout", "Continue where you left off", NULL); return; }
    i--;
  }
  if (i < s_app.storage.active_slots) {
    static char subtitle[32];
    if (s_app.storage.last_completed[i] > 0) {
      char relative[16];
      format_relative_time(relative, sizeof(relative), s_app.storage.last_completed[i]);
      snprintf(subtitle, sizeof(subtitle), "%d ex • %s", s_app.storage.slot_counts[i], relative);
    } else {
      snprintf(subtitle, sizeof(subtitle), "%d exercises", s_app.storage.slot_counts[i]);
    }
    menu_cell_basic_draw(ctx, cell_layer, s_app.storage.slot_names[i], subtitle, NULL);
  } else if (i == s_app.storage.active_slots && s_app.storage.active_slots < MAX_SLOTS) {
    menu_cell_basic_draw(ctx, cell_layer, "Add New Routine", "Send to save here", NULL);
  } else {
    menu_cell_basic_draw(ctx, cell_layer, "Settings", "Timers, Units, & Themes", NULL);
  }
}

static void start_workout_from_slot(int slot_idx) {
  s_app.state.current_slot = slot_idx;

  RoutineHeader header;
  persist_read_data(STORAGE_KEY_BASE + slot_idx, &header, sizeof(RoutineHeader));
  snprintf(s_app.state.routine_name, sizeof(s_app.state.routine_name), "%s", header.name);
  s_app.state.total_exercises = header.total_exercises;

  s_app.state.total_workout_sets = 0;
  for (int j = 0; j < s_app.state.total_exercises; j++) {
    persist_read_data(ROUTINE_EX_BASE + (slot_idx * MAX_EXERCISES) + j, &s_app.state.exercises[j], sizeof(Exercise));
    s_app.state.exercises[j].current_set = 1;
    s_app.state.total_workout_sets += s_app.state.exercises[j].target_sets;
    
    for(int s = 0; s < 10; s++) {
      s_app.state.exercises[j].actual_reps[s] = 0;
      s_app.state.exercises[j].actual_weight[s] = 0;
    }
  }

  s_app.state.curr_ex_idx = 0;
  s_app.state.workout_sec = 0;
  s_app.state.peak_hr     = 0;
  s_app.state.total_hr    = 0;
  s_app.state.hr_samples  = 0;

  init_temp_values(&s_app.state.exercises[0]);

  s_app.state.is_resting              = false;
  s_app.state.rest_seconds_remaining  = 0;
  s_app.state.active                  = true;

  s_app.state.last_workout_sec = persist_exists(GHOST_KEY_BASE + slot_idx)
                                  ? persist_read_int(GHOST_KEY_BASE + slot_idx) : 0;

  // FIX #12: destroy old workout window so wo_click_provider re-reads long_press_ms on next push
  if (s_app.ui.workout_window) { window_destroy(s_app.ui.workout_window); s_app.ui.workout_window = NULL; }

  push_workout_window();
}

static void menu_select_callback(MenuLayer *ml, MenuIndex *cell_index, void *data) {
  int i = cell_index->row;
  if (s_app.state.has_resume) {
    if (i == 0) {
      ActiveState state;
      persist_read_data(ACTIVE_STATE_KEY, &state, sizeof(state));
      s_app.state.total_exercises     = state.total_exercises;
      s_app.state.total_workout_sets  = state.total_workout_sets;
      s_app.state.curr_ex_idx         = state.curr_ex_idx;
      s_app.state.workout_sec         = state.workout_sec;
      s_app.state.current_slot        = state.current_slot;
      s_app.state.peak_hr             = state.peak_hr;
      s_app.state.total_hr            = state.total_hr;
      s_app.state.hr_samples          = state.hr_samples;
      // FIX #2: safe strncpy with explicit null termination
      strncpy(s_app.state.routine_name, state.routine_name, sizeof(s_app.state.routine_name) - 1);
      s_app.state.routine_name[sizeof(s_app.state.routine_name) - 1] = '\0';

      for (int j = 0; j < s_app.state.total_exercises; j++)
        persist_read_data(ACTIVE_EX_BASE + j, &s_app.state.exercises[j], sizeof(Exercise));

      s_app.state.active     = true;
      s_app.state.has_resume = false;
      persist_delete(ACTIVE_STATE_KEY);

      s_app.state.is_resting             = false;
      s_app.state.rest_seconds_remaining = 0;

      init_temp_values(&s_app.state.exercises[s_app.state.curr_ex_idx]);

      s_app.state.last_workout_sec = persist_exists(GHOST_KEY_BASE + s_app.state.current_slot)
                                      ? persist_read_int(GHOST_KEY_BASE + s_app.state.current_slot) : 0;

      menu_layer_reload_data(s_app.ui.menu_layer);
      push_workout_window();
      return;
    }
    i--;
  }

  if      (i < s_app.storage.active_slots) start_workout_from_slot(i);
  else if (i == s_app.storage.active_slots && s_app.storage.active_slots < MAX_SLOTS) {
    snprintf(s_shared_text_buffer, sizeof(s_shared_text_buffer), "Empty Slot\n\nOpen this app's settings on your phone to send a routine here.");
    push_help_window();
  }
  else push_settings_window();
}

static void menu_select_long_callback(MenuLayer *ml, MenuIndex *cell_index, void *data) {
  int i = cell_index->row;
  if (s_app.state.has_resume) {
    if (i == 0) return;
    i--;
  }
  if (i < s_app.storage.active_slots) {
    s_app.storage.slot_to_edit = i;
    push_action_window();
  }
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds        = layer_get_bounds(window_layer);

  s_app.ui.main_header_bg = layer_create(GRect(0, 0, bounds.size.w, 40));
  layer_set_update_proc(s_app.ui.main_header_bg, header_bg_update_proc);
  layer_add_child(window_layer, s_app.ui.main_header_bg);

  s_app.ui.main_header_text = build_text_layer(GRect(0, 6, bounds.size.w, 30),
    FONT_KEY_GOTHIC_24_BOLD, GColorWhite, GTextAlignmentCenter, s_app.ui.main_header_bg);
  text_layer_set_text(s_app.ui.main_header_text, "Select Routine");

  s_app.ui.menu_layer = menu_layer_create(GRect(0, 40, bounds.size.w, bounds.size.h - 40));
  refresh_directory();
  menu_layer_set_callbacks(s_app.ui.menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows      = menu_get_num_rows_callback,
    .draw_row          = menu_draw_row_callback,
    .select_click      = menu_select_callback,
    .select_long_click = menu_select_long_callback,
  });
  menu_layer_set_normal_colors(s_app.ui.menu_layer,    get_bg_color(), get_text_color());
  menu_layer_set_highlight_colors(s_app.ui.menu_layer, get_theme_color(), get_bg_color());
  menu_layer_set_click_config_onto_window(s_app.ui.menu_layer, window);

  if (s_app.storage.active_slots > 0) {
    int next_slot = s_app.settings.last_routine_slot + 1;
    if (next_slot >= s_app.storage.active_slots) next_slot = 0;
    // Routine rows shift down by one when the Resume row is shown
    int row = next_slot + (s_app.state.has_resume ? 1 : 0);
    menu_layer_set_selected_index(s_app.ui.menu_layer, MenuIndex(0, row), MenuRowAlignCenter, false);
  }
  layer_add_child(window_layer, menu_layer_get_layer(s_app.ui.menu_layer));
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_app.ui.main_header_text);
  layer_destroy(s_app.ui.main_header_bg);
  menu_layer_destroy(s_app.ui.menu_layer);
}

// ----------- Number Picker Window -----------
static void update_timer_edit_ui(void) {
  static char val_buf[16];
  snprintf(val_buf, sizeof(val_buf), "%d:%02d", s_timer_edit_val / 60, s_timer_edit_val % 60);
  text_layer_set_text(s_timer_value_layer, val_buf);
}

static void timer_edit_up_click(ClickRecognizerRef r, void *ctx) {
  s_timer_edit_val += 5; // Increment by 5 seconds
  if (s_timer_edit_val > 600) s_timer_edit_val = 600; // Cap at 10 minutes
  update_timer_edit_ui();
}

static void timer_edit_down_click(ClickRecognizerRef r, void *ctx) {
  if (s_timer_edit_val >= 5) s_timer_edit_val -= 5;
  update_timer_edit_ui();
}

static void timer_edit_select_click(ClickRecognizerRef r, void *ctx) {
  // Save to the correct setting based on the row they clicked
  switch (s_timer_edit_row) {
    case 0: s_app.settings.set_rest_sec   = s_timer_edit_val; save_setting(SK_SET_REST, s_timer_edit_val); break;
    case 1: s_app.settings.ex_rest_sec    = s_timer_edit_val; save_setting(SK_EX_REST, s_timer_edit_val); break;
    case 2: s_app.settings.super_rest_sec = s_timer_edit_val; save_setting(SK_SUPER_REST, s_timer_edit_val); break;
    case 3: s_app.settings.drop_rest_sec  = s_timer_edit_val; save_setting(SK_DROP_REST, s_timer_edit_val); break;
  }
  vibes_short_pulse();
  menu_layer_reload_data(s_app.ui.settings_menu_layer);
  window_stack_pop(true);
}

static void timer_edit_click_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, timer_edit_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, timer_edit_down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, timer_edit_select_click);
  // Add repeating clicks so they can hold the button down to scroll fast!
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, timer_edit_up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, timer_edit_down_click);
}

static void timer_edit_window_load(Window *window) {
  Layer *w_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(w_layer);

  static char title_buf[32];
  if (s_timer_edit_row == 0) snprintf(title_buf, sizeof(title_buf), "Set Rest");
  else if (s_timer_edit_row == 1) snprintf(title_buf, sizeof(title_buf), "Exercise Rest");
  else if (s_timer_edit_row == 2) snprintf(title_buf, sizeof(title_buf), "Super Rest");
  else snprintf(title_buf, sizeof(title_buf), "Drop Rest");

  s_timer_title_layer = build_text_layer(GRect(0, 20, bounds.size.w, 30), FONT_KEY_GOTHIC_24_BOLD, get_text_color(), GTextAlignmentCenter, w_layer);
  text_layer_set_text(s_timer_title_layer, title_buf);

  s_timer_value_layer = build_text_layer(GRect(0, bounds.size.h / 2 - 25, bounds.size.w, 50), FONT_KEY_BITHAM_42_BOLD, get_theme_color(), GTextAlignmentCenter, w_layer);
  
  s_timer_hints_layer = build_text_layer(GRect(0, bounds.size.h - 40, bounds.size.w, 40), FONT_KEY_GOTHIC_14, get_text_color(), GTextAlignmentCenter, w_layer);
  text_layer_set_text(s_timer_hints_layer, "[Up/Down] Adjust\n[Select] Save");

  update_timer_edit_ui();
}

static void timer_edit_window_unload(Window *window) {
  text_layer_destroy(s_timer_title_layer);
  text_layer_destroy(s_timer_value_layer);
  text_layer_destroy(s_timer_hints_layer);
}

static void push_timer_edit_window(void) {
  if (!s_timer_edit_window) {
    s_timer_edit_window = window_create();
    window_set_click_config_provider(s_timer_edit_window, timer_edit_click_provider);
    window_set_window_handlers(s_timer_edit_window, (WindowHandlers){ .load = timer_edit_window_load, .unload = timer_edit_window_unload });
  }
  window_set_background_color(s_timer_edit_window, get_bg_color());
  window_stack_push(s_timer_edit_window, true);
}

// ========================================================================= //
//                        10. APP MESSAGE (INBOX)                            //
// ========================================================================= //
static void inbox_received_callback(DictionaryIterator *iterator, void *context) {

  // ----------- Handle full routines sent from phone -----------
  Tuple *routine_data_tuple = dict_find(iterator, MESSAGE_KEY_ROUTINE_DATA);
  if (routine_data_tuple && routine_data_tuple->type == TUPLE_CSTRING) {
    static char inbox_copy[EXPORT_BUF_SIZE];
    strncpy(inbox_copy, routine_data_tuple->value->cstring, sizeof(inbox_copy) - 1);
    inbox_copy[sizeof(inbox_copy) - 1] = '\0';

    if (strncmp(inbox_copy, "BATCH~", 6) == 0) {
      for (int i = 0; i < MAX_SLOTS; i++) {
        persist_delete(STORAGE_KEY_BASE + i);
        for (int j = 0; j < MAX_EXERCISES; j++)
          persist_delete(ROUTINE_EX_BASE + (i * MAX_EXERCISES) + j);
        persist_delete(LAST_COMPLETED_KEY_BASE + i);
      }
      s_app.storage.active_slots = 0;

      char *start   = inbox_copy + 6;
      char *end     = start;
      int slot_idx  = 0;

      while (*end != '\0' && slot_idx < MAX_SLOTS) {
        if (*end == '~') {
          *end = '\0';                // safe: modifying our own local copy
          parse_routine_string(start);
          save_routine_to_slot(slot_idx++);
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
      parse_routine_string(inbox_copy);
      
      int target_slot = s_app.storage.active_slots;
      bool slot_exists = false;
      
      // Find out if we are updating an existing routine or creating a new one
      for (int i = 0; i < s_app.storage.active_slots; i++) {
        if (strcmp(s_app.storage.slot_names[i], s_app.state.routine_name) == 0) {
          target_slot = i; 
          slot_exists = true;
          break;
        }
      }
      if (target_slot > MAX_SLOTS - 1) target_slot = MAX_SLOTS - 1;

      // PRESERVE HISTORY: If updating an existing routine, merge the old actuals!
      if (slot_exists) {
        for (int i = 0; i < s_app.state.total_exercises; i++) {
          // Look for this specific exercise name in the old saved slot
          for (int old_idx = 0; old_idx < s_app.storage.slot_counts[target_slot]; old_idx++) {
            Exercise old_ex;
            if (persist_read_data(ROUTINE_EX_BASE + (target_slot * MAX_EXERCISES) + old_idx, &old_ex, sizeof(Exercise)) > 0) {
              if (strcmp(s_app.state.exercises[i].name, old_ex.name) == 0) {
                // Match found! Copy the historical actuals into the incoming updated routine
                for (int s = 0; s < 10; s++) {
                  s_app.state.exercises[i].actual_reps[s] = old_ex.actual_reps[s];
                  s_app.state.exercises[i].actual_weight[s] = old_ex.actual_weight[s];
                }
                break; // Found the match, move on to the next incoming exercise
              }
            }
          }
        }
      }

      // Now save the routine (which contains the new targets but preserves the old history)
      save_routine_to_slot(target_slot);
    }

    refresh_directory();
    menu_layer_reload_data(s_app.ui.menu_layer);
  }

  // ----------- Handle progression settings updates -----------
  Tuple *prog_mode_tuple = dict_find(iterator, MESSAGE_KEY_PROGRESSION_MODE);
  if (prog_mode_tuple) {
    s_app.settings.progression_mode = prog_mode_tuple->value->int32;
    persist_write_int(SETTINGS_KEY_BASE + SK_PROG_MODE, s_app.settings.progression_mode);
  }

  Tuple *weight_inc_tuple = dict_find(iterator, MESSAGE_KEY_WEIGHT_INCREMENT);
  if (weight_inc_tuple) {
    s_app.settings.weight_increment = weight_inc_tuple->value->int32;
    persist_write_int(SETTINGS_KEY_BASE + SK_WEIGHT_INC, s_app.settings.weight_increment);
  }

  // ----------- Handle chunked transfer (large routines) -----------
  // Format: "N/T|data" where N=current chunk, T=total chunks
  // pkjs splits oversized payloads into CHUNK_SIZE (400 char) chunks
  // and sends them sequentially with this metadata header.
  Tuple *chunk_tuple = dict_find(iterator, MESSAGE_KEY_CHUNK_TRANSFER);
  if (chunk_tuple && chunk_tuple->type == TUPLE_CSTRING) {
    static char chunk_buf[CHUNK_BUF_SIZE];
    static int chunk_expected_total = 0;
    static int chunk_received_count = 0;
    static bool chunk_in_progress = false;

    const char *raw = chunk_tuple->value->cstring;

    // Parse "N/T|data"
    int chunk_num = 0, chunk_total = 0;
    const char *pipe = strchr(raw, '|');
    if (pipe) {
      // Manual parse of "N/T" — avoids sscanf which pulls in libc stubs
      // unavailable on gabbro platform
      const char *p = raw;
      while (*p >= '0' && *p <= '9') { chunk_num = chunk_num * 10 + (*p - '0'); p++; }
      if (*p == '/') {
        p++;
        while (*p >= '0' && *p <= '9') { chunk_total = chunk_total * 10 + (*p - '0'); p++; }
      }
      if (chunk_num > 0 && chunk_total > 0 && pipe[1] != '\0') {
        const char *data = pipe + 1;
        int data_len = strlen(data);

        // First chunk: initialize buffer
        if (chunk_num == 1) {
          chunk_buf[0] = '\0';
          chunk_expected_total = chunk_total;
          chunk_received_count = 0;
          chunk_in_progress = true;
        }

        if (chunk_in_progress && chunk_num == chunk_received_count + 1) {
          // Append data to buffer
          int cur_len = strlen(chunk_buf);
          if (cur_len + data_len < CHUNK_BUF_SIZE - 1) {
            memcpy(chunk_buf + cur_len, data, data_len);
            chunk_buf[cur_len + data_len] = '\0';
            chunk_received_count++;
          } else {
            // Buffer overflow — abort chunk transfer
            chunk_in_progress = false;
            chunk_buf[0] = '\0';
          }
        } else if (chunk_in_progress) {
          // Out-of-order chunk — abort
          chunk_in_progress = false;
          chunk_buf[0] = '\0';
        }

        // All chunks received — process the complete routine
        if (chunk_in_progress && chunk_received_count == chunk_expected_total) {
          chunk_in_progress = false;

          // Process as if it were a normal ROUTINE_DATA message
          parse_routine_string(chunk_buf);

          int target_slot = s_app.storage.active_slots;
          bool slot_exists = false;

          for (int i = 0; i < s_app.storage.active_slots; i++) {
            if (strcmp(s_app.storage.slot_names[i], s_app.state.routine_name) == 0) {
              target_slot = i;
              slot_exists = true;
              break;
            }
          }
          if (target_slot > MAX_SLOTS - 1) target_slot = MAX_SLOTS - 1;

          if (slot_exists) {
            for (int i = 0; i < s_app.state.total_exercises; i++) {
              for (int old_idx = 0; old_idx < s_app.storage.slot_counts[target_slot]; old_idx++) {
                Exercise old_ex;
                if (persist_read_data(ROUTINE_EX_BASE + (target_slot * MAX_EXERCISES) + old_idx, &old_ex, sizeof(Exercise)) > 0) {
                  if (strcmp(s_app.state.exercises[i].name, old_ex.name) == 0) {
                    for (int s = 0; s < 10; s++) {
                      s_app.state.exercises[i].actual_reps[s] = old_ex.actual_reps[s];
                      s_app.state.exercises[i].actual_weight[s] = old_ex.actual_weight[s];
                    }
                    break;
                  }
                }
              }
            }
          }

          save_routine_to_slot(target_slot);
          refresh_directory();
          menu_layer_reload_data(s_app.ui.menu_layer);
          vibes_double_pulse();
        }
      }
    }
  }

  // ----------- Catch single parsed voice exercise from phone -----------
  Tuple *new_ex_tuple = dict_find(iterator, MESSAGE_KEY_NEW_EXERCISE_DATA);
  if (new_ex_tuple) {
    char *new_ex_str = new_ex_tuple->value->cstring;

    Exercise ex;
    memset(&ex, 0, sizeof(Exercise));

    char temp[32];
    int field = 0, t_idx = 0;

    // Unpack the phone's parsed string (e.g. "Bicep Curls|3|10|15|0|-")
    for (int i = 0; new_ex_str[i] != '\0'; i++) {
      if (new_ex_str[i] == '|') {
        temp[t_idx] = '\0';
        if (field == 0) snprintf(ex.name, sizeof(ex.name), "%s", temp);
        else if (field == 1) ex.target_sets = atoi(temp);
        else if (field == 2) ex.target_reps = atoi(temp);
        else if (field == 3) ex.target_weight = atoi(temp);
        else if (field == 4) ex.modifier = atoi(temp);
        field++;
        t_idx = 0;
      } else {
        if (t_idx < 31) temp[t_idx++] = new_ex_str[i];
      }
    }
    temp[t_idx] = '\0';
    if (field == 5) {
      if (strcmp(temp, "-") == 0) ex.comment[0] = '\0';
      else snprintf(ex.comment, sizeof(ex.comment), "%s", temp);
    }

    // Initialize runtime variables
    if (ex.target_sets > 10) ex.target_sets = 10;  // actual_* arrays hold 10 sets
    if (ex.target_sets < 1)  ex.target_sets = 1;
    ex.current_set = 1;
    for(int j=0; j<10; j++) { ex.actual_reps[j] = 0; ex.actual_weight[j] = 0; }

    // Hold it in memory and ask the user!
    s_app.state.pending_exercise = ex;
    push_confirm_add_window();
  }
}

// ========================================================================= //
//                          11. APP LIFECYCLE                                //
// ========================================================================= //
static void init(void) {
  load_settings();

  // Seed the dark-theme cache before any UI is created
  { time_t now = time(NULL); struct tm *t = localtime(&now); update_dark_theme_cache(t); }

  if (!persist_exists(V5_MIGRATION_KEY)) {
    for (int i = 0; i < MAX_SLOTS; i++) persist_delete(STORAGE_KEY_BASE + i);
    persist_write_bool(V5_MIGRATION_KEY, true);
  }

  if (persist_exists(ACTIVE_STATE_KEY)) s_app.state.has_resume = true;

  s_app.ui.main_window = window_create();
  window_set_background_color(s_app.ui.main_window, get_bg_color());
  window_set_window_handlers(s_app.ui.main_window,
    (WindowHandlers){ .load = main_window_load, .unload = main_window_unload });
  window_stack_push(s_app.ui.main_window, true);

  // FIX #7: mega_window and note_window are now lazy (created on demand), not at startup.
  // They will be created the first time they are pushed, just like all other windows.

  app_message_register_inbox_received(inbox_received_callback);
  // 2048 is plenty of room for our 1300-byte routines, freeing up over 12 KB of RAM!
  app_message_open(2048, 1024);
}

static void deinit(void) {
  if (s_app.state.active) {
    ActiveState state = {
      .total_exercises   = s_app.state.total_exercises,
      .total_workout_sets= s_app.state.total_workout_sets,
      .curr_ex_idx       = s_app.state.curr_ex_idx,
      .workout_sec       = s_app.state.workout_sec,
      .current_slot      = s_app.state.current_slot,
      .peak_hr           = s_app.state.peak_hr,
      .total_hr          = s_app.state.total_hr,
      .hr_samples        = s_app.state.hr_samples,
    };
    // FIX #2: safe strncpy with explicit null termination
    strncpy(state.routine_name, s_app.state.routine_name, sizeof(state.routine_name) - 1);
    state.routine_name[sizeof(state.routine_name) - 1] = '\0';
    persist_write_data(ACTIVE_STATE_KEY, &state, sizeof(state));

    for (int j = 0; j < s_app.state.total_exercises; j++)
      persist_write_data(ACTIVE_EX_BASE + j, &s_app.state.exercises[j], sizeof(Exercise));
  }

  if (s_app.ui.summary_window)   { window_destroy(s_app.ui.summary_window);   s_app.ui.summary_window   = NULL; }
  if (s_app.ui.workout_window)   { window_destroy(s_app.ui.workout_window);   s_app.ui.workout_window   = NULL; }
  if (s_app.ui.confirm_window)   { window_destroy(s_app.ui.confirm_window);   s_app.ui.confirm_window   = NULL; }
  if (s_app.ui.help_window)      { window_destroy(s_app.ui.help_window);      s_app.ui.help_window      = NULL; }
  if (s_app.ui.settings_window)  { window_destroy(s_app.ui.settings_window);  s_app.ui.settings_window  = NULL; }
  if (s_app.ui.variation_window) { window_destroy(s_app.ui.variation_window); s_app.ui.variation_window = NULL; }
  if (s_app.ui.exit_window)      { window_destroy(s_app.ui.exit_window);      s_app.ui.exit_window      = NULL; }
  if (s_app.ui.sensation_window) { window_destroy(s_app.ui.sensation_window); s_app.ui.sensation_window = NULL; }
  if (s_app.ui.mega_window)      { window_destroy(s_app.ui.mega_window);      s_app.ui.mega_window      = NULL; }
  if (s_app.ui.action_window)    { window_destroy(s_app.ui.action_window);    s_app.ui.action_window    = NULL; }
  if (s_app.ui.inspector_window) { window_destroy(s_app.ui.inspector_window); s_app.ui.inspector_window = NULL; }
  if (s_app.ui.ex_action_window) { window_destroy(s_app.ui.ex_action_window); s_app.ui.ex_action_window = NULL; }
  if (s_app.ui.confirm_add_window) { window_destroy(s_app.ui.confirm_add_window); s_app.ui.confirm_add_window = NULL; }
  if (s_app.state.dictation_session) {
    dictation_session_destroy(s_app.state.dictation_session);
    s_app.state.dictation_session = NULL;
  }
  if (s_app.state.new_ex_dictation_session) {
    dictation_session_destroy(s_app.state.new_ex_dictation_session);
    s_app.state.new_ex_dictation_session = NULL;
  }

  window_destroy(s_app.ui.main_window);
  s_app.ui.main_window = NULL;
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
