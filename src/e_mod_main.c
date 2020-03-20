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

#include <e.h>
#include <Eina.h>
#include <Ecore.h>
#include <Ecore_Con.h>

#include "e_mod_main.h"

#define _EET_ENTRY "config"

#define PRINT _printf

static E_Module *_module = NULL;

typedef struct
{
   unsigned int id;
   Eina_Stringshare *screenshot_filename;
   Ecore_Exe *screenshot_exe;
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
   E_Gadcon_Client *gcc;
   Evas_Object *o_icon;

   Eina_List *machs; /* List of Machine_Info */
} Instance;

typedef struct
{
   Eina_List *machine_names;
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
        sprintf(path, "%s/eXpra/log", efreet_config_home_get());
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
   sprintf(path, "%s/eXpra/config", efreet_config_home_get());
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

   sprintf(path, "%s/eXpra", efreet_config_home_get());
   if (!_mkdir(path)) return;

   _config_eet_load();
   sprintf(path, "%s/eXpra/config", efreet_config_home_get());
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

    efl_key_data_set(mach->sessions_get_exe, "eXpra_type", "sessions_get");
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
  mach->sessions = eina_list_append(mach->sessions, s);
  return s;
}

static Eina_Bool
_cmd_end_cb(void *data EINA_UNUSED, int _type EINA_UNUSED, void *event)
{
   Ecore_Exe_Event_Del *event_info = (Ecore_Exe_Event_Del *)event;
   Ecore_Exe *exe = event_info->exe;
   const char *type = efl_key_data_get(exe, "eXpra_type");

   if (!type) return ECORE_CALLBACK_PASS_ON;

   if (!strcmp(type, "sessions_get"))
   {
     Machine_Info *mach = ecore_exe_data_get(exe);
     if (event_info->exit_code == 0)
     {
       const char *str_to_find = "LIVE session at :";
       char *p = strstr(eina_strbuf_string_get(mach->sessions_get_buffer), str_to_find);
       while (p)
       {
         Session_Info *session;
         unsigned int id = 0;
         p += strlen(str_to_find);
         id = strtol(p, NULL, 10);

         session = _find_session_in_machine_by_id(mach, id);
         if (!session->screenshot_exe)
         {
         }

         p = strstr(p, str_to_find);
       }
       eina_strbuf_reset(mach->sessions_get_buffer);
     }
     else
     {
       // Delete all the sessions and GUI objects related to the machine
     }
   }
   return ECORE_CALLBACK_DONE;
}

static Eina_Bool
_cmd_output_cb(void *data EINA_UNUSED, int _type EINA_UNUSED, void *event)
{
   Ecore_Exe_Event_Data *event_data = (Ecore_Exe_Event_Data *)event;
   Ecore_Exe *exe = event_data->exe;
   const char *type = efl_key_data_get(exe, "eXpra_type");

   if (!type) return ECORE_CALLBACK_PASS_ON;

   if (!strcmp(type, "sessions_get"))
   {
     Machine_Info *mach = ecore_exe_data_get(exe);
     eina_strbuf_append(mach->sessions_get_buffer, event_data->data);
   }

   return ECORE_CALLBACK_DONE;
}

static Eo *
_icon_create(Eo *parent, const char *path, Eo **wref)
{
   Eo *ic = wref ? *wref : NULL;
   if (!ic)
     {
        ic = elm_icon_add(parent);
        elm_icon_standard_set(ic, path);
        evas_object_show(ic);
        if (wref) efl_wref_add(ic, wref);
     }
   return ic;
}

#if 0
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
#endif

static Instance *
_instance_create()
{
   Instance *inst = calloc(1, sizeof(Instance));

   return inst;
}

static void
_instance_delete(Instance *inst)
{
   if (inst->o_icon) evas_object_del(inst->o_icon);

   free(inst);
}

static void
_button_cb_mouse_down(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
//   Instance *inst;
   Evas_Event_Mouse_Down *ev;

//   inst = data;
   ev = event_info;

   if (ev->button == 3)
     {
     }
}

static E_Gadcon_Client *
_gc_init(E_Gadcon *gc, const char *name, const char *id, const char *style)
{
   Instance *inst;
   E_Gadcon_Client *gcc;
   char buf[4096];
   Eina_List *itr;
   Eina_Stringshare *mach_name;

   _config_init();
   snprintf(buf, sizeof(buf), "%s/icon.png", e_module_dir_get(_module));

   inst = _instance_create();
   inst->o_icon = _icon_create(gc->evas, buf, NULL);

   gcc = e_gadcon_client_new(gc, name, id, style, inst->o_icon);
   gcc->data = inst;
   inst->gcc = gcc;

   evas_object_event_callback_add(inst->o_icon, EVAS_CALLBACK_MOUSE_DOWN,
				  _button_cb_mouse_down, inst);

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

   return gcc;
}

static void
_gc_shutdown(E_Gadcon_Client *gcc)
{
   _instance_delete(gcc->data);
   _config_shutdown();
}

static void
_gc_orient(E_Gadcon_Client *gcc, E_Gadcon_Orient orient EINA_UNUSED)
{
   e_gadcon_client_aspect_set(gcc, 32, 16);
   e_gadcon_client_min_size_set(gcc, 32, 16);
}

static const char *
_gc_label(const E_Gadcon_Client_Class *client_class EINA_UNUSED)
{
   return "eXpra";
}

static Evas_Object *
_gc_icon(const E_Gadcon_Client_Class *client_class EINA_UNUSED, Evas *evas)
{
   char buf[4096];

   if (!_module) return NULL;

   snprintf(buf, sizeof(buf), "%s/icon.png", e_module_dir_get(_module));

   return _icon_create(evas, buf, NULL);
}

static const char *
_gc_id_new(const E_Gadcon_Client_Class *client_class)
{
   char buf[32];
   static int id = 0;
   sprintf(buf, "%s.%d", client_class->name, ++id);
   return eina_stringshare_add(buf);
}

EAPI E_Module_Api e_modapi =
{
   E_MODULE_API_VERSION, "eXpra"
};

static const E_Gadcon_Client_Class _gc_class =
{
   GADCON_CLIENT_CLASS_VERSION, "eXpra",
   {
      _gc_init, _gc_shutdown, _gc_orient, _gc_label, _gc_icon, _gc_id_new, NULL, NULL
   },
   E_GADCON_CLIENT_STYLE_PLAIN
};

EAPI void *
e_modapi_init(E_Module *m)
{
   ecore_init();
   ecore_con_init();
   ecore_con_url_init();
   efreet_init();

   _module = m;
   e_gadcon_provider_register(&_gc_class);

   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   e_gadcon_provider_unregister(&_gc_class);

   _module = NULL;
   efreet_shutdown();
   ecore_con_url_shutdown();
   ecore_con_shutdown();
   ecore_shutdown();
   return 1;
}

EAPI int
e_modapi_save(E_Module *m EINA_UNUSED)
{
   return 1;
}
