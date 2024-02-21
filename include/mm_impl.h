/**
 * Author......: likuan
 * License.....: MIT
 */

#ifndef _HNU_MM_H
#define _HNU_MM_H

#include "types.h"

long left_size (mm_extend_fd_t *mfd, FILE *fd);
int mm_feof (mm_extend_fd_t *mfd, FILE *fd);
int get_entry_cnt(char *dict_file, mm_extend_fd_t * mfd);
int straight_divide_workload (hashcat_ctx_t *hashcat_ctx);
mm_extend_fd_t * create_mm_fd();
void destory_mm_fd(mm_extend_fd_t * mfd);
void update_log(hashcat_ctx_t * hashcat_ctx, bool last);
char * get_time_started_absolute (const hashcat_ctx_t *hashcat_ctx);
char * get_time_started_relative (const hashcat_ctx_t *hashcat_ctx);

void create_err_log(hashcat_ctx_t * hashcat_ctx, const char* host, const char * err_msg);

typedef enum mm_attack_mode_enum
{
  MM_STRAIGHT,
  MM_BRUTAL_FORCE
} mm_attack_mode_t;

void mm_logfile_append (hashcat_ctx_t *hashcat_ctx, const char *fmt, ...);

/// default time interval set to 30 sec
#define DEFAULT_MM_LOG_INTERVAL 30
#define HOSTNAME_DISPLAY_LEN    32

#endif // _MONITOR_H
