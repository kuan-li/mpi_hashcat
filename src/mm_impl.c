/**
 * Author......: likuan
 * License.....: MIT
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "mm_impl.h"
#include "memory.h"
#include "shared.h"
#include "locking.h"
#include "hashcat.h"

long left_size (mm_extend_fd_t *mfd, FILE *fd)
{
  long cur_pos = ftell (fd);
  if ((unsigned long)cur_pos > mfd->words_end * mfd->word_base)
  {
    return 0;
  }
  return (1 + mfd->words_end ) * mfd->word_base - cur_pos;
}

int mm_feof (mm_extend_fd_t *mfd, FILE* fd)
{
   long cur_pos = ftell (fd) / mfd->word_base;
   if ((unsigned long)cur_pos > mfd -> words_end)
   {
     return 1;
   }
   return 0;
}

/// parse words_cnt and word_base for dict_file and fill them into mfd
int get_entry_cnt(char *dict_file, mm_extend_fd_t * mfd)
{
  /// input check
  if (NULL == dict_file || mfd == NULL)
  {
    printf ("input check failed: get_entry_cnt\n");
    return -1;
  }

  /// parse the filename to get the unit len
  char *pch= strrchr (dict_file, '.');
  int wbase = atoi (pch + 1);

  /// if err happened
  if (wbase <= 0)
  {
    printf ("wordbase parse failed, dict_file: %s\n", dict_file);
    return -1;
  }

  unsigned long num = 0;
  unsigned long file_size = 0;
  struct stat statbuff;
  if (stat (dict_file, &statbuff) >= 0)
  {
    file_size = statbuff.st_size;
    num = file_size / wbase;
  }
  else
  {
    printf ("status check failed, dict_file: %s\n", dict_file);
    return -1;
  }
  mfd->words_cnt = num;
  mfd->word_base = wbase;

  return 1;
}

/// divide all dict words among different mpi processes
int straight_divide_workload (hashcat_ctx_t *hashcat_ctx)
{
  /// how many words in all files
  status_ctx_t   *status_ctx = hashcat_ctx->status_ctx;
  straight_ctx_t *straight_ctx = hashcat_ctx->straight_ctx;

  unsigned long total_cnts = 0;
  for (uint pos = 0; pos < straight_ctx->dicts_cnt; pos++)
  {
    total_cnts += hashcat_ctx->fd_list[pos].words_cnt;
  }

  unsigned long cnt_per_process = total_cnts / hashcat_ctx->total_proc_cnt;
  unsigned long start_mark = hashcat_ctx->cur_proc_id * cnt_per_process;
  unsigned long end_mark = (hashcat_ctx->cur_proc_id + 1) * cnt_per_process;
  unsigned long tmp_mark = 0;

  /// initialize
  for (uint pos = 0; pos < straight_ctx->dicts_cnt; pos++)
  {
    hashcat_ctx->fd_list[pos].words_start = 0;
    hashcat_ctx->fd_list[pos].words_end = 0;
    hashcat_ctx->fd_list[pos].is_valid = -1;
  }

  /// [start, end], feof
  for (uint pos = 0; pos < straight_ctx->dicts_cnt; pos++)
  {
    if (tmp_mark <= start_mark &&
        tmp_mark + hashcat_ctx->fd_list[pos].words_cnt > start_mark)
    {
      hashcat_ctx->fd_list[pos].is_valid = 1;
      hashcat_ctx->fd_list[pos].words_start = start_mark - tmp_mark;
      if (tmp_mark + hashcat_ctx->fd_list[pos].words_cnt > end_mark)
      {
        hashcat_ctx->fd_list[pos].words_end = end_mark - tmp_mark - 1;
        status_ctx->words_cnt += hashcat_ctx->fd_list[pos].words_end - hashcat_ctx->fd_list[pos].words_start + 1;
      }
      else
      {
        hashcat_ctx->fd_list[pos].words_end = hashcat_ctx->fd_list[pos].words_cnt - 1;
        status_ctx->words_cnt += hashcat_ctx->fd_list[pos].words_end - hashcat_ctx->fd_list[pos].words_start + 1;
        for (uint tmp = pos + 1; tmp < straight_ctx->dicts_cnt; tmp++)
        {
          hashcat_ctx->fd_list[tmp].is_valid = 1;
          tmp_mark += hashcat_ctx->fd_list[tmp - 1].words_cnt;
          if (tmp_mark + hashcat_ctx->fd_list[tmp].words_cnt > end_mark)
          {
            hashcat_ctx->fd_list[tmp].words_end = end_mark - tmp_mark - 1;
            status_ctx->words_cnt += hashcat_ctx->fd_list[tmp].words_end - hashcat_ctx->fd_list[tmp].words_start + 1;
            break;
          }
          else
          {
            hashcat_ctx->fd_list[tmp].words_end = hashcat_ctx->fd_list[tmp].words_cnt - 1;
            status_ctx->words_cnt += hashcat_ctx->fd_list[tmp].words_end - hashcat_ctx->fd_list[tmp].words_start + 1;
          }
        }
      }
      break;
    }
    tmp_mark += hashcat_ctx->fd_list[pos].words_cnt;
  }

  /// fix the last part
  if (hashcat_ctx->cur_proc_id + 1 == hashcat_ctx->total_proc_cnt)
  {
    status_ctx->words_cnt += hashcat_ctx->fd_list[straight_ctx->dicts_cnt - 1].words_cnt - hashcat_ctx->fd_list[straight_ctx->dicts_cnt - 1].words_end - 1;
    hashcat_ctx->fd_list[straight_ctx->dicts_cnt - 1].words_end = hashcat_ctx->fd_list[straight_ctx->dicts_cnt - 1].words_cnt - 1;
  }

  return 1;
}

mm_extend_fd_t * create_mm_fd()
{
  mm_extend_fd_t *mfd = hccalloc(1,sizeof(mm_extend_fd_t));
  return mfd;
}

void destory_mm_fd(mm_extend_fd_t * mfd)
{
  hcfree(mfd);
}

void update_log(hashcat_ctx_t * hashcat_ctx, bool last)
{
  logfile_ctx_t   *logfile_ctx   = hashcat_ctx->logfile_ctx;
  user_options_t  *user_options  = hashcat_ctx->user_options;

  time_t nowtime = time (NULL);
  struct tm *nowt = localtime (&nowtime);

  hc_asprintf (&logfile_ctx->logfile,  "%s/%d-%04d-%02d-%02d-%02d-%02d-%02d",
            user_options->mm_log_dir, hashcat_ctx->cur_proc_id, nowt->tm_year + 1900, nowt->tm_mon + 1,
            nowt->tm_mday, nowt->tm_hour, nowt->tm_min, nowt->tm_sec);

  hashcat_status_t *hashcat_status = (hashcat_status_t *) hcmalloc (sizeof (hashcat_status_t));

  const int rc_status = hashcat_get_status (hashcat_ctx, hashcat_status);

  if (rc_status == -1)
  {
    hcfree (hashcat_status);
    return;
  }

  mm_logfile_append(hashcat_ctx, "{\"started\":\"%s ( %s s )\", \"estimated\":\"%s ( %s s )\", \"progress\":\" %lu/%lu (%.02f\%)\",\"speed\":\" %s H/s\",\"hashtype\":\" %s \"",
                                     hashcat_status->time_started_absolute,
                                     hashcat_status->time_started_relative,
                                     hashcat_status->time_estimated_absolute,
                                     hashcat_status->time_estimated_relative,
                                     hashcat_status->progress_cur_relative_skip,
                                     hashcat_status->progress_end_relative_skip,
                                     hashcat_status->progress_finished_percent,
                                     hashcat_status->speed_sec_all,
                                     hashcat_status->hash_type);

  if (hashcat_ctx->cracked[0] == 1)
  {
    /// cracked by me
    if ( NULL == hashcat_ctx->mm_crack_buf)
    {
      /// something must be wrong
      mm_logfile_append(hashcat_ctx, ",\"status:\":\"wrong\"}\n");
    }
    else
    {
      /// crack by me
      char * mm_p = strchr(hashcat_ctx->mm_crack_buf, ':');
      *mm_p =0;
      mm_logfile_append(hashcat_ctx, ",\"status:\":\"cracked\",\"result\":\"%s\"} \n", mm_p + 1);
      *mm_p =':';
      hashcat_ctx->crack_log_done = true;
    }
  }
  else if(last)
  {
    mm_logfile_append(hashcat_ctx, ",\"status:\":\"finished\"}\n");
  }
  else
  {
    mm_logfile_append(hashcat_ctx, ",\"status:\":\"running\"}\n");
  }

  hcfree (hashcat_status);
}

void create_err_log(hashcat_ctx_t * hashcat_ctx, const char * host, const char * err_msg)
{
  logfile_ctx_t   *logfile_ctx   = hashcat_ctx->logfile_ctx;
  user_options_t  *user_options  = hashcat_ctx->user_options;

  time_t nowtime = time (NULL);
  struct tm *nowt = localtime (&nowtime);

  hc_asprintf (&logfile_ctx->logfile,  "%s/%d-%04d-%02d-%02d-%02d-%02d-%02d",
            user_options->mm_log_dir, hashcat_ctx->cur_proc_id, nowt->tm_year + 1900, nowt->tm_mon + 1,
            nowt->tm_mday, nowt->tm_hour, nowt->tm_min, nowt->tm_sec);

  bool too_early = false;

  hashcat_status_t *hashcat_status = (hashcat_status_t *) hcmalloc (sizeof (hashcat_status_t));

  const int rc_status = hashcat_get_status (hashcat_ctx, hashcat_status);

  if (rc_status == -1)
  {
    hcfree (hashcat_status);
    too_early = true;
  }

  u64 progress_cur =  too_early? 0: hashcat_status->progress_cur_relative_skip;
  u64 progress_end =  too_early? 0: hashcat_status->progress_end_relative_skip;
  double progress_fininshed = too_early? 0 :hashcat_status->progress_finished_percent;

  mm_logfile_append(hashcat_ctx, "{\"started\":\"%s ( %s s )\",\"progress\":\" %lu/%lu (%.02f\%)\", \"status:\":\"error\", \"reason\":\"(%s) %s\"}\n", 
                    get_time_started_absolute(hashcat_ctx), get_time_started_relative(hashcat_ctx), 
                    progress_cur, progress_end, progress_fininshed,
                    host, err_msg);
}

void mm_logfile_append (hashcat_ctx_t *hashcat_ctx, const char *fmt, ...)
{
  logfile_ctx_t *logfile_ctx = hashcat_ctx->logfile_ctx;

  //if (logfile_ctx->enabled == false) return;

  FILE *fp = fopen (logfile_ctx->logfile, "ab");

  if (fp == NULL)
  {
    //event_log_error (hashcat_ctx, "%s: %s", logfile_ctx->logfile, strerror (errno));

    return;
  }

  lock_file (fp);

  va_list ap;

  va_start (ap, fmt);

  vfprintf (fp, fmt, ap);

  va_end (ap);

  //fwrite (EOL, strlen (EOL), 1, fp);

  fflush (fp);

  fclose (fp);
}

char * get_time_started_absolute (const hashcat_ctx_t *hashcat_ctx)
{
  const time_t time_start = hashcat_ctx->runtime_start;

  char buf[32] = { 0 };

  char *start = ctime_r (&time_start, buf);

  const size_t start_len = strlen (start);

  if (start[start_len - 1] == '\n') start[start_len - 1] = 0;
  if (start[start_len - 2] == '\r') start[start_len - 2] = 0;

  return strdup (start);
}

char * get_time_started_relative (const hashcat_ctx_t *hashcat_ctx)
{
  time_t time_now;
  time (&time_now);

  const time_t time_start = hashcat_ctx->runtime_start;

  #if defined (_WIN)
  __time64_t sec_run = time_now - time_start;
  #else
  time_t sec_run = time_now - time_start;
  #endif

  struct tm *tmp;

  #if defined (_WIN)
  tmp = _gmtime64 (&sec_run);
  #else
  struct tm tm;

  tmp = gmtime_r (&sec_run, &tm);
  #endif

  char *display_run = (char *) malloc (HCBUFSIZ_TINY);

  snprintf(display_run, HCBUFSIZ_TINY-1, " %lu ", sec_run);

  return display_run;
}
