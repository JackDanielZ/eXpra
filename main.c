#define EFL_BETA_API_SUPPORT
#define EFL_EO_API_SUPPORT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <syslog.h>

#include <Eina.h>
#include <Ecore.h>
#include <Ecore_Con.h>
#include <Elementary.h>

#define _EET_ENTRY "config"

#define PRINT _printf

typedef struct
{
   Eina_Stringshare *machine_name;
   unsigned int id;

   Eina_Tmpstr *screenshot_tmp_file;
   Ecore_Exe *screenshot_get_exe;
   Ecore_Timer *screenshot_timer;
   Ecore_Exe *attach_exe;

   Eo *screenshot_icon;
   Eo *attach_bt;
   Eo *detach_bt;

   Eina_Bool screenshot_available :1;
   Eina_Bool to_delete :1;
} Session_Info;

typedef struct
{
   Eina_Stringshare *name;

   Eina_List *sessions; /* List of Session_Info */

   Ecore_Timer *sessions_get_timer;
   Ecore_Exe *sessions_get_exe;
   Eina_Strbuf *sessions_get_buffer;
} Machine_Info;

typedef struct
{
   Evas_Object *o_icon;

   Eina_List *machs; /* List of Machine_Info */

   Eo *main_box;
   Eo *table;
   Eo *screenshot_win;
} Instance;

typedef struct
{
   Eina_List *machine_names; /* List of strings */
} Config;

static Config *_config = NULL;
static Eet_Data_Descriptor *_config_edd = NULL;

static int
_printf(const char *fmt, ...)
{
   static FILE *fp = NULL;
   char printf_buf[1024];
   va_list args;
   int printed;

   if (!fp)
     {
        char path[1024];
        sprintf(path, "%s/"APP_NAME"/log", efreet_config_home_get());
        fp = fopen(path, "a");
     }

   va_start(args, fmt);
   printed = vsprintf(printf_buf, fmt, args);
   va_end(args);

   fwrite(printf_buf, 1, strlen(printf_buf), fp);
   fflush(fp);

   return printed;
}

static void
_config_eet_load()
{
   if (_config_edd) return;
   Eet_Data_Descriptor_Class eddc;

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Config);
   _config_edd = eet_data_descriptor_stream_new(&eddc);
   EET_DATA_DESCRIPTOR_ADD_LIST_STRING(_config_edd, Config, "machine_names", machine_names);
}

static void
_config_save()
{
   char path[1024];
   sprintf(path, "%s/"APP_NAME"/config", efreet_config_home_get());
   _config_eet_load();
   Eet_File *file = eet_open(path, EET_FILE_MODE_WRITE);
   eet_data_write(file, _config_edd, _EET_ENTRY, _config, EINA_TRUE);
   eet_close(file);
}

static Eina_Bool
_mkdir(const char *dir)
{
   if (!ecore_file_exists(dir))
     {
        Eina_Bool success = ecore_file_mkdir(dir);
        if (!success)
          {
             PRINT("Cannot create a config folder \"%s\"\n", dir);
             return EINA_FALSE;
          }
     }
   return EINA_TRUE;
}

static void
_config_init(void)
{
   char path[1024];

   sprintf(path, "%s/"APP_NAME, efreet_config_home_get());
   if (!_mkdir(path)) return;

   _config_eet_load();
   sprintf(path, "%s/"APP_NAME"/config", efreet_config_home_get());
   Eet_File *file = eet_open(path, EET_FILE_MODE_READ);
   if (!file)
   {
     PRINT("New config\n");

     _config = calloc(1, sizeof(Config));
     _config->machine_names = eina_list_append(_config->machine_names, "MACH1");
     _config->machine_names = eina_list_append(_config->machine_names, "MACH2");
     _config_save();
   }
   else
   {
     _config = eet_data_read(file, _config_edd, _EET_ENTRY);
     eet_close(file);
   }
}

static void
_config_shutdown()
{
   Eina_Stringshare *mach_name;
   EINA_LIST_FREE(_config->machine_names, mach_name)
   {
     eina_stringshare_del(mach_name);
   }
   free(_config);
   _config = NULL;
}

static Eo *
_image_create(Eo *parent, const char *path, Eo **wref)
{
   Eo *o = wref ? *wref : NULL;
   if (!o)
   {
     o = elm_image_add(parent);
     evas_object_size_hint_weight_set(o, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
     evas_object_size_hint_align_set(o, EVAS_HINT_FILL, EVAS_HINT_FILL);
     evas_object_size_hint_aspect_set(o, EVAS_ASPECT_CONTROL_BOTH, 1, 1);
     elm_image_preload_disabled_set(o, EINA_FALSE);
     if (path) elm_image_file_set(o, path, NULL);
     if (wref) efl_wref_add(o, wref);
     evas_object_show(o);
   }
   return o;
}

static Eo *
_box_create(Eo *parent, Eina_Bool horiz, Eo **wref)
{
   Eo *o = elm_box_add(parent);
   elm_box_horizontal_set(o, horiz);
   evas_object_size_hint_weight_set(o, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(o, EVAS_HINT_FILL, EVAS_HINT_FILL);
   efl_gfx_entity_visible_set(o, EINA_TRUE);
   if (wref) efl_wref_add(o, wref);
   return o;
}

static Eo *
_label_create(Eo *parent, const char *text, Eo **wref)
{
   Eo *label = wref ? *wref : NULL;
   if (!label)
     {
        label = elm_label_add(parent);
        evas_object_size_hint_align_set(label, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(label, 0.0, 0.0);
        evas_object_show(label);
        if (wref) efl_wref_add(label, wref);
     }
   elm_object_text_set(label, text);
   return label;
}

static Eo *
_button_create(Eo *parent, const char *text, Eo *icon, Eo **wref, Evas_Smart_Cb cb_func, void *cb_data)
{
   Eo *bt = wref ? *wref : NULL;
   if (!bt)
     {
        bt = elm_button_add(parent);
        evas_object_size_hint_align_set(bt, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(bt, 0.0, 0.0);
        evas_object_show(bt);
        if (wref) efl_wref_add(bt, wref);
        if (cb_func) evas_object_smart_callback_add(bt, "clicked", cb_func, cb_data);
     }
   elm_object_text_set(bt, text);
   elm_object_part_content_set(bt, "icon", icon);
   return bt;
}

static Eo *
_separator_create(Eo *parent, Eina_Bool horiz, Eo **wref)
{
   Eo *sp = wref ? *wref : NULL;
   if (!sp)
     {
        sp = elm_separator_add(parent);
        elm_separator_horizontal_set(sp, horiz);
        evas_object_show(sp);
        if (wref) efl_wref_add(sp, wref);
     }
   return sp;
}

static void
_table_dimensions_get(Instance *inst, unsigned int *rows, unsigned int *colums)
{
  Eina_List *itr;
  unsigned int count = 0;
  Machine_Info *mach;
  EINA_LIST_FOREACH(inst->machs, itr, mach)
  {
    count += eina_list_count(mach->sessions);
  }
  *rows = 0;
  *colums = 0;
  while (*rows * *colums < count)
  {
    *rows += 1;
    if (*rows * *colums < count) *colums += 1;
  }
}

static void
_screenshot_mouse_in_cb(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
#if 1
  (void)data;
#else
  Session_Info *session = data;
  Instance *inst;
  E_Dialog *dialog;
  Evas *evas;
  Evas_Object *img;
  int mw, mh;
  int w, h;

  inst = efl_key_data_get(session->screenshot_icon, "instance");
  if (inst->screenshot_dialog) return;

  inst->screenshot_dialog = dialog = e_dialog_new(NULL, "E", "connman_request_input");
  if (!dialog) return;

  e_dialog_resizable_set(dialog, 1);

  e_dialog_title_set(dialog, "Input requested");
  e_dialog_border_icon_set(dialog, "dialog-ask");

  evas = evas_object_evas_get(dialog->win);
  ecore_evas_screen_geometry_get(ecore_evas_ecore_evas_get(evas), NULL, NULL, &w, &h);

  img = e_widget_image_add_from_file(evas, session->screenshot_tmp_file, 0.8 * w, 0.8 * h);
  evas_object_show(img);

  e_widget_size_min_get(img, &mw, &mh);

  if (mw < 260) mw = 260;
  if (mh < 130) mh = 130;
  e_dialog_content_set(dialog, img, mw, mh);

  e_dialog_show(dialog);

  e_dialog_button_focus_num(dialog, 0);
//  elm_win_center(dialog->win, 1, 1);
  elm_win_size_base_set(dialog->win, 0.8 * w, 0.8 * h);
  elm_win_borderless_set(dialog->win, EINA_TRUE);
#endif
}

static void
_screenshot_mouse_out_cb(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
#if 1
  (void) data;
#else
  Session_Info *session = data;
  Instance *inst = efl_key_data_get(session->screenshot_icon, "instance");

  e_object_del(E_OBJECT(inst->screenshot_dialog));
  inst->screenshot_dialog = NULL;
#endif
}

static void
_attach_cb(void *data, Evas_Object *bt EINA_UNUSED, void *event_info EINA_UNUSED)
{
  char cmd[256];
  Session_Info *session = data;

  sprintf(cmd, "xpra attach ssh://%s/%d", session->machine_name, session->id);
  session->attach_exe = ecore_exe_pipe_run(cmd, ECORE_EXE_NONE, session);
  efl_wref_add(session->attach_exe, &(session->attach_exe));

  elm_object_disabled_set(session->attach_bt, EINA_TRUE);
  elm_object_disabled_set(session->detach_bt, EINA_FALSE);
}

static void
_detach_cb(void *data, Evas_Object *bt EINA_UNUSED, void *event_info EINA_UNUSED)
{
  char cmd[256];
  Session_Info *session = data;

  sprintf(cmd, "xpra detach ssh://%s/%d", session->machine_name, session->id);
  ecore_exe_pipe_run(cmd, ECORE_EXE_NONE, NULL);

  elm_object_disabled_set(session->attach_bt, EINA_FALSE);
  elm_object_disabled_set(session->detach_bt, EINA_TRUE);
}

static void
_kill_cb(void *data, Evas_Object *bt EINA_UNUSED, void *event_info EINA_UNUSED)
{
  char cmd[256];
  Session_Info *session = data;

  sprintf(cmd, "xpra stop ssh://%s/%d", session->machine_name, session->id);
  ecore_exe_pipe_run(cmd, ECORE_EXE_NONE, NULL);
}

static void
_session_free(Session_Info *session)
{
   eina_stringshare_del(session->machine_name);

   efl_del(session->screenshot_get_exe);
   ecore_timer_del(session->screenshot_timer);

   efl_del(session->screenshot_icon);

   free(session);
}

static void
_box_update(Instance *inst)
{
#define NB_COLS_PER_ELT 10

  Eo *o, *o2;
  Machine_Info *mach;
  Eina_List *itr1, *itr2;
  unsigned int total_rows = 0, total_colums = 0, row = 0, column = 0;

  if (!inst->main_box) return;

  _table_dimensions_get(inst, &total_rows, &total_colums);

  total_colums = ( total_colums * NB_COLS_PER_ELT ) + ( total_colums - 1 );
  total_rows += ( total_rows - 1 );

  efl_del(inst->table);

  (void)total_colums;

  o = elm_table_add(inst->main_box);
  evas_object_size_hint_align_set(o, EVAS_HINT_FILL, EVAS_HINT_FILL);
  evas_object_size_hint_weight_set(o, EVAS_HINT_EXPAND, 0.0);
  elm_table_padding_set(o, 20, 0);
  elm_box_pack_end(inst->main_box, o);
  evas_object_show(o);
  efl_wref_add(o, &inst->table);

  EINA_LIST_FOREACH(inst->machs, itr1, mach)
  {
    Session_Info *session;
    EINA_LIST_FOREACH(mach->sessions, itr2, session)
    {
      char id_str[256];
      sprintf(id_str, "%s:%d", session->machine_name, session->id);
      o = _box_create(inst->table, EINA_TRUE, NULL);
      elm_table_pack(inst->table, o, column, row, NB_COLS_PER_ELT, 1);
      column += NB_COLS_PER_ELT;
      elm_box_pack_end(o, _image_create(o, NULL, &(session->screenshot_icon)));
      efl_key_data_set(session->screenshot_icon, "instance", inst);

      evas_object_event_callback_add(session->screenshot_icon, EVAS_CALLBACK_MOUSE_IN,
          _screenshot_mouse_in_cb, session);
      evas_object_event_callback_add(session->screenshot_icon, EVAS_CALLBACK_MOUSE_OUT,
          _screenshot_mouse_out_cb, session);

      if (session->screenshot_available)
      {
        elm_image_file_set(session->screenshot_icon, session->screenshot_tmp_file, NULL);
      }
      else
      {
        elm_image_file_set(session->screenshot_icon, PREFIX"/share/"APP_NAME"/not_available.png", NULL);
      }
      o2 = _box_create(o, EINA_FALSE, NULL);
      elm_box_pack_end(o, o2);
      elm_box_pack_end(o2, _label_create(o2, id_str, NULL));
      o2 = _box_create(o, EINA_FALSE, NULL);
      elm_box_pack_end(o2, _button_create(o2, "Attach", NULL, &(session->attach_bt), _attach_cb, session));
      elm_box_pack_end(o2, _button_create(o2, "Detach", NULL, &(session->detach_bt), _detach_cb, session));
      if (session->attach_exe)
      {
        elm_object_disabled_set(session->attach_bt, EINA_TRUE);
      }
      else
      {
        elm_object_disabled_set(session->detach_bt, EINA_TRUE);
      }
      elm_box_pack_end(o2, _button_create(o2, "Kill", NULL, NULL, _kill_cb, session));
      elm_box_pack_end(o, o2);

      if (column >= total_colums)
      {
        row++;
        elm_table_pack(inst->table, _separator_create(inst->table, EINA_TRUE, NULL), 0, row, total_colums, 1);
        row++;
        column = 0;
      }
      else {
        elm_table_pack(inst->table, _separator_create(inst->table, EINA_FALSE, NULL), column, row, 1, 1);
        column++;
      }
    }
  }
}

static Eina_Bool
_sessions_get_cb(void *data)
{
  Machine_Info *mach = data;

  if (!mach->sessions_get_exe)
  {
    char cmd[1024];

    sprintf(cmd, "ssh %s xpra list", mach->name);
    mach->sessions_get_exe = ecore_exe_pipe_run(cmd, ECORE_EXE_PIPE_READ | ECORE_EXE_PIPE_ERROR, mach);
    efl_wref_add(mach->sessions_get_exe, &(mach->sessions_get_exe));

    efl_key_data_set(mach->sessions_get_exe, "type", "sessions_get");
  }

  return EINA_TRUE;
}

static Eina_Bool
_screenshot_get_cb(void *data)
{
  Session_Info *session = data;

  if (!session->screenshot_get_exe)
  {
    char cmd[1024];
    int fd;

    if (session->screenshot_tmp_file) unlink(session->screenshot_tmp_file);

    fd = eina_file_mkstemp("xpra_shot_XXXXXX.png", &(session->screenshot_tmp_file));
    if (fd < 0) return EINA_TRUE;
    close(fd);

    sprintf(cmd, "xpra screenshot %s ssh://%s/%d",
        session->screenshot_tmp_file, session->machine_name, session->id);
    session->screenshot_get_exe = ecore_exe_pipe_run(cmd, ECORE_EXE_NONE, session);
    efl_wref_add(session->screenshot_get_exe, &(session->screenshot_get_exe));

    efl_key_data_set(session->screenshot_get_exe, "type", "screenshot_get");
  }

  return EINA_TRUE;
}

static Session_Info *
_find_session_in_machine_by_id(Machine_Info *mach, unsigned int id)
{
  Eina_List *itr;
  Session_Info *s;
  EINA_LIST_FOREACH(mach->sessions, itr, s)
  {
    if (s->id == id) return s;
  }
  s = calloc(1, sizeof(Session_Info));
  s->id = id;
  s->machine_name = eina_stringshare_add(mach->name);
  mach->sessions = eina_list_append(mach->sessions, s);
  return s;
}

static Eina_Bool
_cmd_end_cb(void *data, int _type EINA_UNUSED, void *event)
{
   Instance *inst = data;
   Ecore_Exe_Event_Del *event_info = (Ecore_Exe_Event_Del *)event;
   Ecore_Exe *exe = event_info->exe;
   const char *type = efl_key_data_get(exe, "type");

   if (!type) return ECORE_CALLBACK_PASS_ON;

   if (!strcmp(type, "sessions_get"))
   {
     Machine_Info *mach = ecore_exe_data_get(exe);
     if (event_info->exit_code == 0)
     {
       Eina_List *itr, *itr2;
       Eina_Bool has_changed = EINA_FALSE;
       Session_Info *session;
       const char *str_to_find = "LIVE session at :";
       char *p = strstr(eina_strbuf_string_get(mach->sessions_get_buffer), str_to_find);

       EINA_LIST_FOREACH(mach->sessions, itr, session) session->to_delete = EINA_TRUE;

       while (p)
       {
         unsigned int id = 0;
         p += strlen(str_to_find);
         id = strtol(p, NULL, 10);

         session = _find_session_in_machine_by_id(mach, id);
         session->to_delete = EINA_FALSE;
         if (!session->screenshot_timer)
         {
           session->screenshot_timer = ecore_timer_add(60.0, _screenshot_get_cb, session);
           efl_wref_add(session->screenshot_timer, &(session->screenshot_timer));
           _screenshot_get_cb(session);
           has_changed = EINA_TRUE;
         }

         p = strstr(p, str_to_find);
       }
       eina_strbuf_reset(mach->sessions_get_buffer);

       EINA_LIST_FOREACH_SAFE(mach->sessions, itr, itr2, session)
       {
         if (session->to_delete) {
           has_changed = EINA_TRUE;
           mach->sessions = eina_list_remove(mach->sessions, session);
           _session_free(session);
         }
       }

       if (has_changed) _box_update(inst);
     }
   }
   else if (!strcmp(type, "screenshot_get"))
   {
     Session_Info *session = ecore_exe_data_get(exe);
     if (event_info->exit_code == 0)
     {
       elm_image_file_set(session->screenshot_icon, session->screenshot_tmp_file, NULL);
       session->screenshot_available = EINA_TRUE;
     }
   }
   return ECORE_CALLBACK_DONE;
}

static Eina_Bool
_cmd_output_cb(void *data EINA_UNUSED, int _type EINA_UNUSED, void *event)
{
   Ecore_Exe_Event_Data *event_data = (Ecore_Exe_Event_Data *)event;
   Ecore_Exe *exe = event_data->exe;
   const char *type = efl_key_data_get(exe, "type");

   if (!type) return ECORE_CALLBACK_PASS_ON;

   if (!strcmp(type, "sessions_get"))
   {
     Machine_Info *mach = ecore_exe_data_get(exe);
     eina_strbuf_append(mach->sessions_get_buffer, event_data->data);
   }

   return ECORE_CALLBACK_DONE;
}

int main(int argc, char **argv)
{
   Instance *inst;
   Eina_List *itr;
   Eina_Stringshare *mach_name;
   Eo *win;

   ecore_init();
   ecore_con_init();
   ecore_con_url_init();
   efreet_init();
   elm_init(argc, argv);

   elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_LAST_WINDOW_CLOSED);

   _config_init();

   inst = calloc(1, sizeof(Instance));

   ecore_event_handler_add(ECORE_EXE_EVENT_DATA, _cmd_output_cb, inst);
   ecore_event_handler_add(ECORE_EXE_EVENT_ERROR, _cmd_output_cb, inst);
   ecore_event_handler_add(ECORE_EXE_EVENT_DEL, _cmd_end_cb, inst);

   EINA_LIST_FOREACH(_config->machine_names, itr, mach_name)
   {
     Machine_Info *mach = calloc(1, sizeof(*mach));
     mach->name = eina_stringshare_add(mach_name);
     mach->sessions_get_timer = ecore_timer_add(10.0, _sessions_get_cb, mach);
     mach->sessions_get_buffer = eina_strbuf_new();

     _sessions_get_cb(mach);

     inst->machs = eina_list_append(inst->machs, mach);
   }

   win = elm_win_add(NULL, "main", ELM_WIN_BASIC);

   inst->main_box = elm_box_add(win);
   evas_object_size_hint_align_set(inst->main_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(inst->main_box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_show(inst->main_box);
   elm_win_resize_object_add(win, inst->main_box);

   _box_update(inst);

   evas_object_show(win);

   elm_run();

   free(inst);
   _config_shutdown();

   elm_shutdown();
   efreet_shutdown();
   ecore_con_url_shutdown();
   ecore_con_shutdown();
   ecore_shutdown();
}

