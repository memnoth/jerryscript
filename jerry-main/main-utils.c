/* Copyright JS Foundation and other contributors, http://js.foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jerryscript.h"
#include "jerryscript-ext/debugger.h"
#include "jerryscript-ext/handler.h"
#include "jerryscript-port.h"
#include "jerryscript-port-default.h"

#include "main-utils.h"
#include "main-options.h"

/**
 * Max line size that will be printed on a Syntax Error
 */
#define SYNTAX_ERROR_MAX_LINE_LENGTH 256

/**
 * Register a JavaScript function in the global object.
 */
static void
main_register_global_function (const char *name_p, /**< name of the function */
                               jerry_external_handler_t handler_p) /**< function callback */
{
  jerry_value_t result_val = jerryx_handler_register_global ((const jerry_char_t *) name_p, handler_p);
  assert (!jerry_value_is_error (result_val));
  jerry_release_value (result_val);
} /* main_register_global_function */

/**
 * Inits the engine and the debugger
 */
void
main_init_engine (main_args_t *arguments_p) /** main arguments */
{
  jerry_init (arguments_p->init_flags);

  if (arguments_p->option_flags & OPT_FLAG_DEBUG_SERVER)
  {
    bool protocol = false;

    if (!strcmp (arguments_p->debug_protocol, "tcp"))
    {
      protocol = jerryx_debugger_tcp_create (arguments_p->debug_port);
    }
    else
    {
      assert (!strcmp (arguments_p->debug_protocol, "serial"));
      protocol = jerryx_debugger_serial_create (arguments_p->debug_serial_config);
    }

    if (!strcmp (arguments_p->debug_channel, "rawpacket"))
    {
      jerryx_debugger_after_connect (protocol && jerryx_debugger_rp_create ());
    }
    else
    {
      assert (!strcmp (arguments_p->debug_channel, "websocket"));
      jerryx_debugger_after_connect (protocol && jerryx_debugger_ws_create ());
    }
  }

  main_register_global_function ("assert", jerryx_handler_assert);
  main_register_global_function ("gc", jerryx_handler_gc);
  main_register_global_function ("print", jerryx_handler_print);
  main_register_global_function ("resourceName", jerryx_handler_resource_name);
} /* main_init_engine */

/**
 * Print an error value.
 *
 * Note: the error value will be released.
 */
void
main_print_unhandled_exception (jerry_value_t error_value) /**< error value */
{
  assert (jerry_value_is_error (error_value));
  error_value = jerry_get_value_from_error (error_value, true);

  jerry_char_t err_str_buf[256];

  jerry_value_t err_str_val = jerry_value_to_string (error_value);
  jerry_size_t err_str_size = jerry_get_utf8_string_size (err_str_val);

  if (err_str_size >= 256)
  {
    const char msg[] = "[Error message too long]";
    err_str_size = sizeof (msg) / sizeof (char) - 1;
    memcpy (err_str_buf, msg, err_str_size + 1);
  }
  else
  {
    jerry_size_t string_end = jerry_string_to_utf8_char_buffer (err_str_val, err_str_buf, err_str_size);
    assert (string_end == err_str_size);
    err_str_buf[string_end] = 0;

    if (jerry_is_feature_enabled (JERRY_FEATURE_ERROR_MESSAGES)
        && jerry_get_error_type (error_value) == JERRY_ERROR_SYNTAX)
    {
      jerry_char_t *string_end_p = err_str_buf + string_end;
      unsigned int err_line = 0;
      unsigned int err_col = 0;
      char *path_str_p = NULL;
      char *path_str_end_p = NULL;

      /* 1. parse column and line information */
      for (jerry_char_t *current_p = err_str_buf; current_p < string_end_p; current_p++)
      {
        if (*current_p == '[')
        {
          current_p++;

          if (*current_p == '<')
          {
            break;
          }

          path_str_p = (char *) current_p;
          while (current_p < string_end_p && *current_p != ':')
          {
            current_p++;
          }

          path_str_end_p = (char *) current_p++;

          err_line = (unsigned int) strtol ((char *) current_p, (char **) &current_p, 10);

          current_p++;

          err_col = (unsigned int) strtol ((char *) current_p, NULL, 10);
          break;
        }
      } /* for */

      if (err_line != 0 && err_col > 0 && err_col < SYNTAX_ERROR_MAX_LINE_LENGTH)
      {
        uint32_t curr_line = 1;
        uint32_t pos = 0;

        /* Temporarily modify the error message, so we can use the path. */
        *path_str_end_p = '\0';

        size_t source_size;
        uint8_t *source_p = jerry_port_read_source (path_str_p, &source_size);

        /* Revert the error message. */
        *path_str_end_p = ':';

        /* 2. seek and print */
        while (pos < source_size && curr_line < err_line)
        {
          if (source_p[pos] == '\n')
          {
            curr_line++;
          }

          pos++;
        }

        uint32_t char_count = 0;
        uint8_t ch;

        do
        {
          ch = source_p[pos++];
          jerry_port_log (JERRY_LOG_LEVEL_ERROR, "%c", ch);
        }
        while (ch != '\n' && char_count++ < SYNTAX_ERROR_MAX_LINE_LENGTH);

        jerry_port_release_source (source_p);

        while (--err_col)
        {
          jerry_port_log (JERRY_LOG_LEVEL_ERROR, "~");
        }

        jerry_port_log (JERRY_LOG_LEVEL_ERROR, "^\n\n");
      }
    }
  }

  jerry_port_log (JERRY_LOG_LEVEL_ERROR, "%s\n", err_str_buf);
  jerry_release_value (err_str_val);

  if (jerry_value_is_object (error_value))
  {
    jerry_value_t stack_str = jerry_create_string ((const jerry_char_t *) "stack");
    jerry_value_t backtrace_val = jerry_get_property (error_value, stack_str);
    jerry_release_value (stack_str);

    if (jerry_value_is_array (backtrace_val))
    {
      uint32_t length = jerry_get_array_length (backtrace_val);

      /* This length should be enough. */
      if (length > 32)
      {
        length = 32;
      }

      for (uint32_t i = 0; i < length; i++)
      {
        jerry_value_t item_val = jerry_get_property_by_index (backtrace_val, i);

        if (jerry_value_is_string (item_val))
        {
          jerry_size_t str_size = jerry_get_utf8_string_size (item_val);

          if (str_size >= 256)
          {
            printf ("%6u: [Backtrace string too long]\n", i);
          }
          else
          {
            jerry_size_t string_end = jerry_string_to_utf8_char_buffer (item_val, err_str_buf, str_size);
            assert (string_end == str_size);
            err_str_buf[string_end] = 0;

            printf ("%6u: %s\n", i, err_str_buf);
          }
        }

        jerry_release_value (item_val);
      }
    }

    jerry_release_value (backtrace_val);
  }

  jerry_release_value (error_value);
} /* main_print_unhandled_exception */

/**
 * Runs the source code received by jerry_debugger_wait_for_client_source.
 *
 * @return result fo the source code execution
 */
jerry_value_t
main_wait_for_source_callback (const jerry_char_t *resource_name_p, /**< resource name */
                               size_t resource_name_size, /**< size of resource name */
                               const jerry_char_t *source_p, /**< source code */
                               size_t source_size, /**< source code size */
                               void *user_p) /**< user pointer */
{
  (void) user_p; /* unused */
  jerry_value_t ret_val = jerry_parse (resource_name_p,
                                       resource_name_size,
                                       source_p,
                                       source_size,
                                       JERRY_PARSE_NO_OPTS);

  if (!jerry_value_is_error (ret_val))
  {
    jerry_value_t func_val = ret_val;
    ret_val = jerry_run (func_val);
    jerry_release_value (func_val);
  }

  return ret_val;
} /* main_wait_for_source_callback */

/**
 * Check that value contains the reset abort value.
 *
 * Note: if the value is the reset abort value, the value is release.
 *
 * return true, if reset abort
 *        false, otherwise
 */
bool
main_is_value_reset (jerry_value_t value) /**< jerry value */
{
  if (!jerry_value_is_abort (value))
  {
    return false;
  }

  jerry_value_t abort_value = jerry_get_value_from_error (value, false);

  if (!jerry_value_is_string (abort_value))
  {
    jerry_release_value (abort_value);
    return false;
  }

  static const char restart_str[] = "r353t";

  jerry_size_t str_size = jerry_get_string_size (abort_value);
  bool is_reset = false;

  if (str_size == sizeof (restart_str) - 1)
  {
    JERRY_VLA (jerry_char_t, str_buf, str_size);
    jerry_string_to_char_buffer (abort_value, str_buf, str_size);

    is_reset = memcmp (restart_str, (char *) (str_buf), str_size) == 0;

    if (is_reset)
    {
      jerry_release_value (value);
    }
  }

  jerry_release_value (abort_value);
  return is_reset;
} /* main_is_value_reset */
