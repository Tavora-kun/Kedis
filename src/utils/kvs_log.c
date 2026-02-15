#include "../../include/kvs_log.h"
#include <time.h>
#include <stdio.h>

static const char* level_str[] = {"DEBUG", "INFO", "WARNING", "ERROR"};
static const char *level_color[] = {C_DEBUG, C_INFO, C_WARN, C_ERROR};
extern kv_config g_config;

void kvs_log_raw(int level, const char* msg) {
  time_t now = time(NULL);
  struct tm* tm_info = localtime(&now);
  char time_buf[26];
  int log_to_stdout = g_config.logfile[0] == '\0';
  FILE *fp;

  fp = log_to_stdout ? stdout : fopen(g_config.logfile, "a");
  if (!fp) return;

  strftime(time_buf, 26, "%Y-%m-%d %H:%M:%S", tm_info);

  if (log_to_stdout && isatty(fileno(fp))) {
    fprintf(fp, "%s[%s] [%s] %s%s\n", level_color[level], time_buf, level_str[level], msg, C_RESET);
  } else {
    fprintf(fp, "[%s] [%s] %s\n", time_buf, level_str[level], msg);
  }

  fflush(fp);
  if (!log_to_stdout) fclose(fp);
}

void kvs_log(int level, const char* fmt, ...) {
  va_list ap;
  char msg[4096];

  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  kvs_log_raw(level, msg);
}