/**
 * Author......: See docs/credits.txt
 * License.....: MIT
 */

#include "common.h"
#include "types.h"
#include "interface.h"
#include "timer.h"
#include "event.h"
#include "memory.h"
#include "filehandling.h"
#include "ext_OpenCL.h"
#include "tuningdb.h"
#include "thread.h"
#include "opencl.h"
#include "hashes.h"
#include "shared.h"

static int sort_by_tuning_db_alias (const void *v1, const void *v2)
{
  const tuning_db_alias_t *t1 = (const tuning_db_alias_t *) v1;
  const tuning_db_alias_t *t2 = (const tuning_db_alias_t *) v2;

  const int res1 = strcmp (t1->device_name, t2->device_name);

  if (res1 != 0) return (res1);

  return 0;
}

static int sort_by_tuning_db_entry (const void *v1, const void *v2)
{
  const tuning_db_entry_t *t1 = (const tuning_db_entry_t *) v1;
  const tuning_db_entry_t *t2 = (const tuning_db_entry_t *) v2;

  const int res1 = strcmp (t1->device_name, t2->device_name);

  if (res1 != 0) return (res1);

  const int res2 = t1->attack_mode
                 - t2->attack_mode;

  if (res2 != 0) return (res2);

  const int res3 = t1->hash_type
                 - t2->hash_type;

  if (res3 != 0) return (res3);

  return 0;
}

int tuning_db_init (hashcat_ctx_t *hashcat_ctx)
{
  folder_config_t *folder_config = hashcat_ctx->folder_config;
  tuning_db_t     *tuning_db     = hashcat_ctx->tuning_db;
  user_options_t  *user_options  = hashcat_ctx->user_options;

  tuning_db->enabled = false;

  if (user_options->keyspace    == true) return 0;
  if (user_options->left        == true) return 0;
  if (user_options->opencl_info == true) return 0;
  if (user_options->show        == true) return 0;
  if (user_options->usage       == true) return 0;
  if (user_options->mm_usage    == true) return 0;
  if (user_options->version     == true) return 0;

  tuning_db->enabled = true;

  char *tuning_db_file;

  hc_asprintf (&tuning_db_file, "%s/%s", folder_config->shared_dir, TUNING_DB_FILE);

  FILE *fp = fopen (tuning_db_file, "rb");

  if (fp == NULL)
  {
    event_log_error (hashcat_ctx, "%s: %s", tuning_db_file, strerror (errno));

    return -1;
  }

  hcfree (tuning_db_file);

  int num_lines = count_lines (fp);

  // a bit over-allocated

  tuning_db->alias_buf = (tuning_db_alias_t *) hccalloc (num_lines + 1, sizeof (tuning_db_alias_t));
  tuning_db->alias_cnt = 0;

  tuning_db->entry_buf = (tuning_db_entry_t *) hccalloc (num_lines + 1, sizeof (tuning_db_entry_t));
  tuning_db->entry_cnt = 0;

  rewind (fp);

  int line_num = 0;

  char *buf = (char *) hcmalloc (HCBUFSIZ_LARGE);

  while (!feof (fp))
  {
    char *line_buf = fgets (buf, HCBUFSIZ_LARGE - 1, fp);

    if (line_buf == NULL) break;

    line_num++;

    const int line_len = in_superchop (line_buf);

    if (line_len == 0) continue;

    if (line_buf[0] == '#') continue;

    // start processing

    char *token_ptr[7] = { NULL };

    int token_cnt = 0;

    char *saveptr;

    char *next = strtok_r (line_buf, "\t ", &saveptr);

    token_ptr[token_cnt] = next;

    token_cnt++;

    while ((next = strtok_r (NULL, "\t ", &saveptr)) != NULL)
    {
      token_ptr[token_cnt] = next;

      token_cnt++;
    }

    if (token_cnt == 2)
    {
      char *device_name = token_ptr[0];
      char *alias_name  = token_ptr[1];

      tuning_db_alias_t *alias = &tuning_db->alias_buf[tuning_db->alias_cnt];

      alias->device_name = hcstrdup (device_name);
      alias->alias_name  = hcstrdup (alias_name);

      tuning_db->alias_cnt++;
    }
    else if (token_cnt == 6)
    {
      if ((token_ptr[1][0] != '0') &&
          (token_ptr[1][0] != '1') &&
          (token_ptr[1][0] != '3') &&
          (token_ptr[1][0] != '*'))
      {
        event_log_warning (hashcat_ctx, "Tuning-db: Invalid attack_mode '%c' in Line '%d'", token_ptr[1][0], line_num);

        continue;
      }

      if ((token_ptr[3][0] != '1') &&
          (token_ptr[3][0] != '2') &&
          (token_ptr[3][0] != '4') &&
          (token_ptr[3][0] != '8') &&
          (token_ptr[3][0] != 'N'))
      {
        event_log_warning (hashcat_ctx, "Tuning-db: Invalid vector_width '%c' in Line '%d'", token_ptr[3][0], line_num);

        continue;
      }

      char *device_name = token_ptr[0];

      int attack_mode      = -1;
      int hash_type        = -1;
      int vector_width     = -1;
      int kernel_accel     = -1;
      int kernel_loops     = -1;

      if (token_ptr[1][0] != '*') attack_mode      = atoi (token_ptr[1]);
      if (token_ptr[2][0] != '*') hash_type        = atoi (token_ptr[2]);
      if (token_ptr[3][0] != 'N') vector_width     = atoi (token_ptr[3]);

      if (token_ptr[4][0] != 'A')
      {
        kernel_accel = atoi (token_ptr[4]);

        if ((kernel_accel < 1) || (kernel_accel > 1024))
        {
          event_log_warning (hashcat_ctx, "Tuning-db: Invalid kernel_accel '%d' in Line '%d'", kernel_accel, line_num);

          continue;
        }
      }
      else
      {
        kernel_accel = 0;
      }

      if (token_ptr[5][0] != 'A')
      {
        kernel_loops = atoi (token_ptr[5]);

        if ((kernel_loops < 1) || (kernel_loops > 1024))
        {
          event_log_warning (hashcat_ctx, "Tuning-db: Invalid kernel_loops '%d' in Line '%d'", kernel_loops, line_num);

          continue;
        }
      }
      else
      {
        kernel_loops = 0;
      }

      tuning_db_entry_t *entry = &tuning_db->entry_buf[tuning_db->entry_cnt];

      entry->device_name  = hcstrdup (device_name);
      entry->attack_mode  = attack_mode;
      entry->hash_type    = hash_type;
      entry->vector_width = vector_width;
      entry->kernel_accel = kernel_accel;
      entry->kernel_loops = kernel_loops;

      tuning_db->entry_cnt++;
    }
    else
    {
      event_log_warning (hashcat_ctx, "Tuning-db: Invalid number of token in Line '%d'", line_num);

      continue;
    }
  }

  hcfree (buf);

  fclose (fp);

  // todo: print loaded 'cnt' message

  // sort the database

  qsort (tuning_db->alias_buf, tuning_db->alias_cnt, sizeof (tuning_db_alias_t), sort_by_tuning_db_alias);
  qsort (tuning_db->entry_buf, tuning_db->entry_cnt, sizeof (tuning_db_entry_t), sort_by_tuning_db_entry);

  return 0;
}

void tuning_db_destroy (hashcat_ctx_t *hashcat_ctx)
{
  tuning_db_t *tuning_db = hashcat_ctx->tuning_db;

  if (tuning_db->enabled == false) return;

  int i;

  for (i = 0; i < tuning_db->alias_cnt; i++)
  {
    tuning_db_alias_t *alias = &tuning_db->alias_buf[i];

    hcfree (alias->device_name);
    hcfree (alias->alias_name);
  }

  for (i = 0; i < tuning_db->entry_cnt; i++)
  {
    tuning_db_entry_t *entry = &tuning_db->entry_buf[i];

    hcfree ((void *)entry->device_name);
  }

  hcfree (tuning_db->alias_buf);
  hcfree (tuning_db->entry_buf);

  memset (tuning_db, 0, sizeof (tuning_db_t));
}

tuning_db_entry_t *tuning_db_search (hashcat_ctx_t *hashcat_ctx, const char *device_name, const cl_device_type device_type, int attack_mode, const int hash_type)
{
  tuning_db_t *tuning_db = hashcat_ctx->tuning_db;

  static tuning_db_entry_t s;

  // first we need to convert all spaces in the device_name to underscore

  char *device_name_nospace = hcstrdup (device_name);

  int device_name_length = strlen (device_name_nospace);

  int i;

  for (i = 0; i < device_name_length; i++)
  {
    if (device_name_nospace[i] == ' ') device_name_nospace[i] = '_';
  }

  // find out if there's an alias configured

  tuning_db_alias_t a;

  a.device_name = device_name_nospace;

  tuning_db_alias_t *alias = bsearch (&a, tuning_db->alias_buf, tuning_db->alias_cnt, sizeof (tuning_db_alias_t), sort_by_tuning_db_alias);

  char *alias_name = (alias == NULL) ? NULL : alias->alias_name;

  // attack-mode 6 and 7 are attack-mode 1 basically

  if (attack_mode == 6) attack_mode = 1;
  if (attack_mode == 7) attack_mode = 1;

  // bsearch is not ideal but fast enough

  s.device_name = device_name_nospace;
  s.attack_mode = attack_mode;
  s.hash_type   = hash_type;

  tuning_db_entry_t *entry = NULL;

  // this will produce all 2^3 combinations required

  for (i = 0; i < 8; i++)
  {
    s.device_name = (i & 1) ? "*" : device_name_nospace;
    s.attack_mode = (i & 2) ?  -1 : attack_mode;
    s.hash_type   = (i & 4) ?  -1 : hash_type;

    entry = bsearch (&s, tuning_db->entry_buf, tuning_db->entry_cnt, sizeof (tuning_db_entry_t), sort_by_tuning_db_entry);

    if (entry != NULL) break;

    // in non-wildcard mode do some additional checks:

    if ((i & 1) == 0)
    {
      // in case we have an alias-name

      if (alias_name != NULL)
      {
        s.device_name = alias_name;

        entry = bsearch (&s, tuning_db->entry_buf, tuning_db->entry_cnt, sizeof (tuning_db_entry_t), sort_by_tuning_db_entry);

        if (entry != NULL) break;
      }

      // or by device type

      if (device_type & CL_DEVICE_TYPE_CPU)
      {
        s.device_name = "DEVICE_TYPE_CPU";
      }
      else if (device_type & CL_DEVICE_TYPE_GPU)
      {
        s.device_name = "DEVICE_TYPE_GPU";
      }
      else if (device_type & CL_DEVICE_TYPE_ACCELERATOR)
      {
        s.device_name = "DEVICE_TYPE_ACCELERATOR";
      }

      entry = bsearch (&s, tuning_db->entry_buf, tuning_db->entry_cnt, sizeof (tuning_db_entry_t), sort_by_tuning_db_entry);

      if (entry != NULL) break;
    }
  }

  // free converted device_name

  hcfree (device_name_nospace);

  return entry;
}
