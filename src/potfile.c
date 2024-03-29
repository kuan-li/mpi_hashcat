/**
 * Author......: See docs/credits.txt
 * License.....: MIT
 */

#include "common.h"
#include "types.h"
#include "bitops.h"
#include "convert.h"
#include "memory.h"
#include "event.h"
#include "interface.h"
#include "filehandling.h"
#include "loopback.h"
#include "outfile.h"
#include "potfile.h"
#include "locking.h"
#include "shared.h"

// get rid of this later
int sort_by_hash         (const void *v1, const void *v2, void *v3);
int sort_by_hash_no_salt (const void *v1, const void *v2, void *v3);
// get rid of this later

// this function is for potfile comparison where the potfile does not contain all the
// information requires to do a true sort_by_hash() bsearch
static int sort_by_hash_t_salt (const void *v1, const void *v2)
{
  const hash_t *h1 = (const hash_t *) v1;
  const hash_t *h2 = (const hash_t *) v2;

  const salt_t *s1 = h1->salt;
  const salt_t *s2 = h2->salt;

  const int res1 = (int) s1->salt_len - (int) s2->salt_len;

  if (res1 != 0) return (res1);

  //const int res2 = (int) s1->salt_iter - (int) s2->salt_iter;
  //
  //if (res2 != 0) return (res2);

  for (int n = 0; n < 16; n++)
  {
    if (s1->salt_buf[n] > s2->salt_buf[n]) return  1;
    if (s1->salt_buf[n] < s2->salt_buf[n]) return -1;
  }

  for (int n = 0; n < 8; n++)
  {
    if (s1->salt_buf_pc[n] > s2->salt_buf_pc[n]) return  1;
    if (s1->salt_buf_pc[n] < s2->salt_buf_pc[n]) return -1;
  }

  return 0;
}

int potfile_init (hashcat_ctx_t *hashcat_ctx)
{
  folder_config_t *folder_config = hashcat_ctx->folder_config;
  potfile_ctx_t   *potfile_ctx   = hashcat_ctx->potfile_ctx;
  user_options_t  *user_options  = hashcat_ctx->user_options;

  potfile_ctx->enabled = false;

  if (user_options->benchmark       == true) return 0;
  if (user_options->keyspace        == true) return 0;
  if (user_options->opencl_info     == true) return 0;
  if (user_options->stdout_flag     == true) return 0;
  if (user_options->speed_only      == true) return 0;
  if (user_options->progress_only   == true) return 0;
  if (user_options->usage           == true) return 0;
  if (user_options->mm_usage        == true) return 0;
  if (user_options->version         == true) return 0;
  if (user_options->potfile_disable == true) return 0;

  potfile_ctx->enabled = true;

  if (user_options->potfile_path == NULL)
  {
    potfile_ctx->fp       = NULL;

    hc_asprintf (&potfile_ctx->filename, "%s/hashcat.potfile", folder_config->profile_dir);
  }
  else
  {
    potfile_ctx->filename = hcstrdup (user_options->potfile_path);
    potfile_ctx->fp       = NULL;
  }

  // keep all hashes if --username was combined with --left or --show

  potfile_ctx->keep_all_hashes = false;

  if (user_options->username == true)
  {
    if ((user_options->show == true) || (user_options->left == true))
    {
      potfile_ctx->keep_all_hashes = true;
    }
  }

  // keep all hashes if -m 3000 was combined with --left or --show

  if (user_options->hash_mode == 3000)
  {
    if ((user_options->show == true) || (user_options->left == true))
    {
      potfile_ctx->keep_all_hashes = true;
    }
  }

  // starting from here, we should allocate some scratch buffer for later use

  u8 *out_buf = (u8 *) hcmalloc (HCBUFSIZ_LARGE);

  potfile_ctx->out_buf = out_buf;

  // we need two buffers in parallel

  u8 *tmp_buf = (u8 *) hcmalloc (HCBUFSIZ_LARGE);

  potfile_ctx->tmp_buf = tmp_buf;

  // old potfile detection

  if (user_options->potfile_path == NULL)
  {
    char *potfile_old;

    hc_asprintf (&potfile_old, "%s/hashcat.pot", folder_config->profile_dir);

    if (hc_path_exist (potfile_old) == true)
    {
      event_log_warning (hashcat_ctx, "Old potfile detected: %s", potfile_old);
      event_log_warning (hashcat_ctx, "New potfile is: %s ", potfile_ctx->filename);
      event_log_warning (hashcat_ctx, NULL);
    }

    hcfree (potfile_old);
  }

  return 0;
}

void potfile_destroy (hashcat_ctx_t *hashcat_ctx)
{
  potfile_ctx_t *potfile_ctx = hashcat_ctx->potfile_ctx;

  if (potfile_ctx->enabled == false) return;

  hcfree (potfile_ctx->out_buf);
  hcfree (potfile_ctx->tmp_buf);

  memset (potfile_ctx, 0, sizeof (potfile_ctx_t));
}

int potfile_read_open (hashcat_ctx_t *hashcat_ctx)
{
  potfile_ctx_t *potfile_ctx = hashcat_ctx->potfile_ctx;

  if (potfile_ctx->enabled == false) return 0;

  potfile_ctx->fp = fopen (potfile_ctx->filename, "rb");

  if (potfile_ctx->fp == NULL)
  {
    event_log_error (hashcat_ctx, "%s: %s", potfile_ctx->filename, strerror (errno));

    return -1;
  }

  return 0;
}

void potfile_read_close (hashcat_ctx_t *hashcat_ctx)
{
  potfile_ctx_t *potfile_ctx = hashcat_ctx->potfile_ctx;

  if (potfile_ctx->enabled == false) return;

  if (potfile_ctx->fp == NULL) return;

  fclose (potfile_ctx->fp);
}

int potfile_write_open (hashcat_ctx_t *hashcat_ctx)
{
  potfile_ctx_t *potfile_ctx = hashcat_ctx->potfile_ctx;

  if (potfile_ctx->enabled == false) return 0;

  FILE *fp = fopen (potfile_ctx->filename, "ab");

  if (fp == NULL)
  {
    event_log_error (hashcat_ctx, "%s: %s", potfile_ctx->filename, strerror (errno));

    return -1;
  }

  potfile_ctx->fp = fp;

  return 0;
}

void potfile_write_close (hashcat_ctx_t *hashcat_ctx)
{
  potfile_ctx_t *potfile_ctx = hashcat_ctx->potfile_ctx;

  if (potfile_ctx->enabled == false) return;

  fclose (potfile_ctx->fp);
}

void potfile_write_append (hashcat_ctx_t *hashcat_ctx, const char *out_buf, u8 *plain_ptr, unsigned int plain_len)
{
  const hashconfig_t   *hashconfig   = hashcat_ctx->hashconfig;
  const potfile_ctx_t  *potfile_ctx  = hashcat_ctx->potfile_ctx;
  const user_options_t *user_options = hashcat_ctx->user_options;

  if (potfile_ctx->enabled == false) return;

  u8 *tmp_buf = potfile_ctx->tmp_buf;

  int tmp_len = 0;

  if (1)
  {
    const size_t out_len = strlen (out_buf);

    memcpy (tmp_buf + tmp_len, out_buf, out_len);

    tmp_len += out_len;

    tmp_buf[tmp_len] = hashconfig->separator;

    tmp_len += 1;
  }

  if (1)
  {
    const bool always_ascii = (hashconfig->hash_type & OPTS_TYPE_PT_ALWAYS_ASCII) ? true : false;

    if ((user_options->outfile_autohex == true) && (need_hexify (plain_ptr, plain_len, hashconfig->separator, always_ascii) == true))
    {
      tmp_buf[tmp_len++] = '$';
      tmp_buf[tmp_len++] = 'H';
      tmp_buf[tmp_len++] = 'E';
      tmp_buf[tmp_len++] = 'X';
      tmp_buf[tmp_len++] = '[';

      exec_hexify ((const u8 *) plain_ptr, plain_len, (u8 *) tmp_buf + tmp_len);

      tmp_len += plain_len * 2;

      tmp_buf[tmp_len++] = ']';
    }
    else
    {
      memcpy (tmp_buf + tmp_len, plain_ptr, plain_len);

      tmp_len += plain_len;
    }
  }

  tmp_buf[tmp_len] = 0;

  lock_file (potfile_ctx->fp);

  fprintf (potfile_ctx->fp, "%s" EOL, tmp_buf);

  fflush (potfile_ctx->fp);

  if (unlock_file (potfile_ctx->fp))
  {
    event_log_error (hashcat_ctx, "%s: Failed to unlock file.", potfile_ctx->filename);
  }
}

void potfile_update_hash (hashcat_ctx_t *hashcat_ctx, hash_t *found, char *line_pw_buf, int line_pw_len)
{
  const loopback_ctx_t *loopback_ctx = hashcat_ctx->loopback_ctx;

  if (found == NULL) return;

  char *pw_buf = line_pw_buf;
  int   pw_len = line_pw_len;

  found->pw_buf = (char *) hcmalloc (pw_len + 1);
  found->pw_len = pw_len;

  memcpy (found->pw_buf, pw_buf, pw_len);

  found->pw_buf[found->pw_len] = 0;

  found->cracked = 1;

  // if enabled, update also the loopback file

  if (loopback_ctx->fp != NULL)
  {
    loopback_write_append (hashcat_ctx, (u8 *) pw_buf, (unsigned int) pw_len);
  }
}

void potfile_update_hashes (hashcat_ctx_t *hashcat_ctx, hash_t *hash_buf, hash_t *hashes_buf, u32 hashes_cnt, int (*compar) (const void *, const void *, void *), char *line_pw_buf, int line_pw_len)
{
  const hashconfig_t *hashconfig = hashcat_ctx->hashconfig;

  // linear search

  for (u32 hash_pos = 0; hash_pos < hashes_cnt; hash_pos++)
  {
    if (compar ((void *) &hashes_buf[hash_pos], (void *) hash_buf, (void *) hashconfig) == 0)
    {
      potfile_update_hash (hashcat_ctx, &hashes_buf[hash_pos], line_pw_buf, line_pw_len);
    }
  }
}

int potfile_remove_parse (hashcat_ctx_t *hashcat_ctx)
{
  const hashconfig_t   *hashconfig   = hashcat_ctx->hashconfig;
  const hashes_t       *hashes       = hashcat_ctx->hashes;
  const potfile_ctx_t  *potfile_ctx  = hashcat_ctx->potfile_ctx;

  if (potfile_ctx->enabled == false) return 0;

  // if no potfile exists yet we don't need to do anything here

  if (hc_path_exist (potfile_ctx->filename) == false) return 0;

  hash_t *hashes_buf = hashes->hashes_buf;
  u32     hashes_cnt = hashes->hashes_cnt;

  // no solution for these special hash types (for instane because they use hashfile in output etc)

  if  (hashconfig->hash_mode ==  5200)  return 0;
  if ((hashconfig->hash_mode >=  6200)
   && (hashconfig->hash_mode <=  6299)) return 0;
  if  (hashconfig->hash_mode ==  9000)  return 0;
  if ((hashconfig->hash_mode >= 13700)
   && (hashconfig->hash_mode <= 13799)) return 0;
  if  (hashconfig->hash_mode == 14600)  return 0;

  hash_t hash_buf;

  hash_buf.digest    = hcmalloc (hashconfig->dgst_size);
  hash_buf.salt      = NULL;
  hash_buf.esalt     = NULL;
  hash_buf.hook_salt = NULL;
  hash_buf.hash_info = NULL;
  hash_buf.cracked   = 0;

  if (hashconfig->is_salted)
  {
    hash_buf.salt = (salt_t *) hcmalloc (sizeof (salt_t));
  }

  if (hashconfig->esalt_size)
  {
    hash_buf.esalt = hcmalloc (hashconfig->esalt_size);
  }

  if (hashconfig->hook_salt_size)
  {
    hash_buf.hook_salt = hcmalloc (hashconfig->hook_salt_size);
  }

  // this is usually detected by weak-hash-check
  // but not if bitslice

  if (hashconfig->hash_mode == 3000)
  {
    int parser_status = hashconfig->parse_func ((u8 *) LM_WEAK_HASH, 16, &hash_buf, hashconfig);

    if (parser_status == PARSER_OK)
    {
      if (potfile_ctx->keep_all_hashes == true)
      {
        potfile_update_hashes (hashcat_ctx, &hash_buf, hashes_buf, hashes_cnt, sort_by_hash_no_salt, NULL, 0);
      }
      else
      {
        hash_t *found = (hash_t *) hc_bsearch_r (&hash_buf, hashes_buf, hashes_cnt, sizeof (hash_t), sort_by_hash_no_salt, (void *) hashconfig);

        potfile_update_hash (hashcat_ctx, found, NULL, 0);
      }
    }
  }

  const int rc = potfile_read_open (hashcat_ctx);

  if (rc == -1) return -1;

  char *line_buf = (char *) hcmalloc (HCBUFSIZ_LARGE);

  while (!feof (potfile_ctx->fp))
  {
    int line_len = fgetl (potfile_ctx->fp, line_buf);

    if (line_len == 0) continue;

    char *last_separator = strrchr (line_buf, hashconfig->separator);

    if (last_separator == NULL) continue; // ??

    char *line_pw_buf = last_separator + 1;

    int line_pw_len = line_buf + line_len - line_pw_buf;

    char *line_hash_buf = line_buf;

    int line_hash_len = last_separator - line_buf;

    line_hash_buf[line_hash_len] = 0;

    if (line_hash_len == 0) continue;

    // we should allow length 0 passwords (detected by weak hash check)
    //if (line_pw_len == 0) continue;

    if (hashconfig->is_salted)
    {
      memset (hash_buf.salt, 0, sizeof (salt_t));
    }

    if (hashconfig->esalt_size)
    {
      memset (hash_buf.esalt, 0, hashconfig->esalt_size);
    }

    if (hashconfig->hook_salt_size)
    {
      memset (hash_buf.hook_salt, 0, hashconfig->hook_salt_size);
    }

    hash_t *found = NULL;

    if (hashconfig->hash_mode == 6800)
    {
      if (line_hash_len < 64) // 64 = 16 * u32 in salt_buf[]
      {
        // manipulate salt_buf
        memcpy (hash_buf.salt->salt_buf, line_hash_buf, line_hash_len);

        hash_buf.salt->salt_len = line_hash_len;

        found = (hash_t *) bsearch (&hash_buf, hashes_buf, hashes_cnt, sizeof (hash_t), sort_by_hash_t_salt);
      }
    }
    else if (hashconfig->hash_mode == 2500)
    {
      // here we have in line_hash_buf: hash:macap:macsta:essid:password

      char *sep_pos = strrchr (line_hash_buf, ':');

      if (sep_pos == NULL) continue;

      sep_pos[0] = 0;

      char *hash_pos = line_hash_buf;

      const size_t hash_len = strlen (hash_pos);

      if (hash_len != 32 + 1 + 12 + 1 + 12) continue;

      char *essid_pos = sep_pos + 1;

      int essid_len = (int) strlen (essid_pos);

      if (is_hexify ((const u8 *) essid_pos, (const int) essid_len) == true)
      {
        essid_len = exec_unhexify ((const u8 *) essid_pos, (int) essid_len, (u8 *) essid_pos, (int) essid_len);
      }

      if (essid_len > 32) continue;

      if (hashconfig->is_salted)
      {
        // this should be always true, but we need it to make scan-build happy

        memcpy (hash_buf.salt->salt_buf, essid_pos, essid_len);

        hash_buf.salt->salt_len  = essid_len;
        hash_buf.salt->salt_iter = ROUNDS_WPA2 - 1;

        u32 hash[4];

        hash[0] = hex_to_u32 ((const u8 *) &hash_pos[ 0]);
        hash[1] = hex_to_u32 ((const u8 *) &hash_pos[ 8]);
        hash[2] = hex_to_u32 ((const u8 *) &hash_pos[16]);
        hash[3] = hex_to_u32 ((const u8 *) &hash_pos[24]);

        hash[0] = byte_swap_32 (hash[0]);
        hash[1] = byte_swap_32 (hash[1]);
        hash[2] = byte_swap_32 (hash[2]);
        hash[3] = byte_swap_32 (hash[3]);

        u32 *digest = (u32 *) hash_buf.digest;

        digest[0] = hash[0];
        digest[1] = hash[1];
        digest[2] = hash[2];
        digest[3] = hash[3];
      }

      found = (hash_t *) hc_bsearch_r (&hash_buf, hashes_buf, hashes_cnt, sizeof (hash_t), sort_by_hash, (void *) hashconfig);
    }
    else
    {
      int parser_status = hashconfig->parse_func ((u8 *) line_hash_buf, line_hash_len, &hash_buf, hashconfig);

      if (parser_status == PARSER_OK)
      {
        if (hashconfig->is_salted)
        {
          if (potfile_ctx->keep_all_hashes == true)
          {
            potfile_update_hashes (hashcat_ctx, &hash_buf, hashes_buf, hashes_cnt, sort_by_hash, line_pw_buf, line_pw_len);

            continue;
          }

          found = (hash_t *) hc_bsearch_r (&hash_buf, hashes_buf, hashes_cnt, sizeof (hash_t), sort_by_hash, (void *) hashconfig);
        }
        else
        {
          if (potfile_ctx->keep_all_hashes == true)
          {
            potfile_update_hashes (hashcat_ctx, &hash_buf, hashes_buf, hashes_cnt, sort_by_hash_no_salt, line_pw_buf, line_pw_len);

            continue;
          }

          found = (hash_t *) hc_bsearch_r (&hash_buf, hashes_buf, hashes_cnt, sizeof (hash_t), sort_by_hash_no_salt, (void *) hashconfig);
        }
      }
    }

    potfile_update_hash (hashcat_ctx, found, line_pw_buf, line_pw_len);
  }

  hcfree (line_buf);

  potfile_read_close (hashcat_ctx);

  if (hashconfig->esalt_size)
  {
    hcfree (hash_buf.esalt);
  }

  if (hashconfig->hook_salt_size)
  {
    hcfree (hash_buf.hook_salt);
  }

  if (hashconfig->is_salted)
  {
    hcfree (hash_buf.salt);
  }

  hcfree (hash_buf.digest);

  return 0;
}

int potfile_handle_show (hashcat_ctx_t *hashcat_ctx)
{
  hashconfig_t  *hashconfig  = hashcat_ctx->hashconfig;
  hashes_t      *hashes      = hashcat_ctx->hashes;
  potfile_ctx_t *potfile_ctx = hashcat_ctx->potfile_ctx;

  hash_t *hashes_buf  = hashes->hashes_buf;

  u32     salts_cnt = hashes->salts_cnt;
  salt_t *salts_buf = hashes->salts_buf;

  if (hashconfig->hash_mode == 3000)
  {
    for (u32 salt_idx = 0; salt_idx < salts_cnt; salt_idx++)
    {
      salt_t *salt_buf = salts_buf + salt_idx;

      u32 digests_cnt = salt_buf->digests_cnt;

      for (u32 digest_idx = 0; digest_idx < digests_cnt; digest_idx++)
      {
        const u32 hashes_idx = salt_buf->digests_offset + digest_idx;

        u32 *digests_shown = hashes->digests_shown;

        hash_t *hash1 = &hashes_buf[hashes_idx];
        hash_t *hash2 = NULL;

        int split_neighbor = -1;

        // find out if at least one of the parts has been cracked

        if (hash1->hash_info->split->split_origin == SPLIT_ORIGIN_LEFT)
        {
          split_neighbor = hash1->hash_info->split->split_neighbor;

          hash2 = &hashes_buf[split_neighbor];

          if ((digests_shown[hashes_idx] == 0) && (digests_shown[split_neighbor] == 0)) continue;
        }
        else if (hash1->hash_info->split->split_origin == SPLIT_ORIGIN_NONE)
        {
          if (digests_shown[hashes_idx] == 0) continue;
        }
        else
        {
          // SPLIT_ORIGIN_RIGHT are not handled this way

          continue;
        }

        u8 *out_buf = potfile_ctx->out_buf;

        out_buf[0] = 0;

        ascii_digest (hashcat_ctx, (char *) out_buf + 0, HCBUFSIZ_LARGE - 0, salt_idx, digest_idx);

        if (hash2)
        {
          ascii_digest (hashcat_ctx, (char *) out_buf + 16, HCBUFSIZ_LARGE - 16, salt_idx, split_neighbor);
        }

        // user
        unsigned char *username = NULL;

        u32 user_len = 0;

        user_t *user = hash1->hash_info->user;

        if (user)
        {
          username = (unsigned char *) (user->user_name);

          user_len = user->user_len;

          username[user_len] = 0;
        }

        u8 *tmp_buf = potfile_ctx->tmp_buf;

        tmp_buf[0] = 0;

        u8 mixed_buf[20] = { 0 };

        u8 mixed_len = 0;

        if (digests_shown[hashes_idx] == 1)
        {
          memcpy (mixed_buf + mixed_len, hash1->pw_buf, hash1->pw_len);

          mixed_len += hash1->pw_len;
        }
        else
        {
          memcpy (mixed_buf + mixed_len, LM_MASKED_PLAIN, strlen (LM_MASKED_PLAIN));

          mixed_len += strlen (LM_MASKED_PLAIN);
        }

        if (hash2)
        {
          if (digests_shown[split_neighbor] == 1)
          {
            memcpy (mixed_buf + mixed_len, hash2->pw_buf, hash2->pw_len);

            mixed_len += hash2->pw_len;
          }
          else
          {
            memcpy (mixed_buf + mixed_len, LM_MASKED_PLAIN, strlen (LM_MASKED_PLAIN));

            mixed_len += strlen (LM_MASKED_PLAIN);
          }
        }

        const int tmp_len = outfile_write (hashcat_ctx, (char *) out_buf, (u8 *) mixed_buf, mixed_len, 0, username, user_len, (char *) tmp_buf);

        EVENT_DATA (EVENT_POTFILE_HASH_SHOW, tmp_buf, tmp_len);
      }
    }
  }
  else
  {
    for (u32 salt_idx = 0; salt_idx < salts_cnt; salt_idx++)
    {
      salt_t *salt_buf = salts_buf + salt_idx;

      u32 digests_cnt = salt_buf->digests_cnt;

      for (u32 digest_idx = 0; digest_idx < digests_cnt; digest_idx++)
      {
        const u32 hashes_idx = salt_buf->digests_offset + digest_idx;

        u32 *digests_shown = hashes->digests_shown;

        if (digests_shown[hashes_idx] == 0) continue;

        hash_t *hash = &hashes_buf[hashes_idx];

        u8 *out_buf = potfile_ctx->out_buf;

        out_buf[0] = 0;

        ascii_digest (hashcat_ctx, (char *) out_buf, HCBUFSIZ_LARGE, salt_idx, digest_idx);

        // user
        unsigned char *username = NULL;

        u32 user_len = 0;

        if (hash->hash_info != NULL)
        {
          user_t *user = hash->hash_info->user;

          if (user)
          {
            username = (unsigned char *) (user->user_name);

            user_len = user->user_len;

            username[user_len] = 0;
          }
        }

        u8 *tmp_buf = potfile_ctx->tmp_buf;

        tmp_buf[0] = 0;

        const int tmp_len = outfile_write (hashcat_ctx, (char *) out_buf, (u8 *) hash->pw_buf, hash->pw_len, 0, username, user_len, (char *) tmp_buf);

        EVENT_DATA (EVENT_POTFILE_HASH_SHOW, tmp_buf, tmp_len);
      }
    }
  }

  return 0;
}

int potfile_handle_left (hashcat_ctx_t *hashcat_ctx)
{
  hashconfig_t  *hashconfig  = hashcat_ctx->hashconfig;
  hashes_t      *hashes      = hashcat_ctx->hashes;
  potfile_ctx_t *potfile_ctx = hashcat_ctx->potfile_ctx;

  hash_t *hashes_buf = hashes->hashes_buf;

  u32     salts_cnt = hashes->salts_cnt;
  salt_t *salts_buf = hashes->salts_buf;

  if (hashconfig->hash_mode == 3000)
  {
    for (u32 salt_idx = 0; salt_idx < salts_cnt; salt_idx++)
    {
      salt_t *salt_buf = salts_buf + salt_idx;

      u32 digests_cnt = salt_buf->digests_cnt;

      for (u32 digest_idx = 0; digest_idx < digests_cnt; digest_idx++)
      {
        const u32 hashes_idx = salt_buf->digests_offset + digest_idx;

        u32 *digests_shown = hashes->digests_shown;

        hash_t *hash1 = &hashes_buf[hashes_idx];
        hash_t *hash2 = NULL;

        int split_neighbor = -1;

        // find out if at least one of the parts has been cracked

        if (hash1->hash_info->split->split_origin == SPLIT_ORIGIN_LEFT)
        {
          split_neighbor = hash1->hash_info->split->split_neighbor;

          hash2 = &hashes_buf[split_neighbor];

          if ((digests_shown[hashes_idx] == 1) && (digests_shown[split_neighbor] == 1)) continue;
        }
        else if (hash1->hash_info->split->split_origin == SPLIT_ORIGIN_NONE)
        {
          if (digests_shown[hashes_idx] == 1) continue;
        }
        else
        {
          // SPLIT_ORIGIN_RIGHT are not handled this way

          continue;
        }

        u8 *out_buf = potfile_ctx->out_buf;

        out_buf[0] = 0;

        ascii_digest (hashcat_ctx, (char *) out_buf + 0, HCBUFSIZ_LARGE - 0, salt_idx, digest_idx);

        if (hash2)
        {
          ascii_digest (hashcat_ctx, (char *) out_buf + 16, HCBUFSIZ_LARGE - 16, salt_idx, split_neighbor);
        }

        // user
        unsigned char *username = NULL;

        u32 user_len = 0;

        user_t *user = hash1->hash_info->user;

        if (user)
        {
          username = (unsigned char *) (user->user_name);

          user_len = user->user_len;

          username[user_len] = 0;
        }

        u8 *tmp_buf = potfile_ctx->tmp_buf;

        tmp_buf[0] = 0;

        const int tmp_len = outfile_write (hashcat_ctx, (char *) out_buf, NULL, 0, 0, username, user_len, (char *) tmp_buf);

        EVENT_DATA (EVENT_POTFILE_HASH_LEFT, tmp_buf, tmp_len);
      }
    }
  }
  else
  {
    for (u32 salt_idx = 0; salt_idx < salts_cnt; salt_idx++)
    {
      salt_t *salt_buf = salts_buf + salt_idx;

      u32 digests_cnt = salt_buf->digests_cnt;

      for (u32 digest_idx = 0; digest_idx < digests_cnt; digest_idx++)
      {
        const u32 hashes_idx = salt_buf->digests_offset + digest_idx;

        u32 *digests_shown = hashes->digests_shown;

        if (digests_shown[hashes_idx] == 1) continue;

        u8 *out_buf = potfile_ctx->out_buf;

        out_buf[0] = 0;

        ascii_digest (hashcat_ctx, (char *) out_buf, HCBUFSIZ_LARGE, salt_idx, digest_idx);

        hash_t *hash = &hashes_buf[hashes_idx];

        // user
        unsigned char *username = NULL;

        u32 user_len = 0;

        if (hash->hash_info != NULL)
        {
          user_t *user = hash->hash_info->user;

          if (user)
          {
            username = (unsigned char *) (user->user_name);

            user_len = user->user_len;

            username[user_len] = 0;
          }
        }

        u8 *tmp_buf = potfile_ctx->tmp_buf;

        tmp_buf[0] = 0;

        const int tmp_len = outfile_write (hashcat_ctx, (char *) out_buf, NULL, 0, 0, username, user_len, (char *) tmp_buf);

        EVENT_DATA (EVENT_POTFILE_HASH_LEFT, tmp_buf, tmp_len);
      }
    }
  }

  return 0;
}
