#include "mirror_log.h"
#include <time.h>
#include <stdio.h>

static const char* level_str[] = {"DEBUG", "INFO", "WARNING", "ERROR"};
static const char *level_color[] = {C_DEBUG, C_INFO, C_WARN, C_ERROR};

void mirror_log_raw(int level, const char* msg) {
  time_t now = time(NULL);
  struct tm* tm_info = localtime(&now);
  char time_buf[26];
  FILE *fp;

  fp = LOG_TO_STDOUT ? stdout : fopen(LOG_FILE, "a");
  if (!fp) return;

  strftime(time_buf, 26, "%Y-%m-%d %H:%M:%S", tm_info);

  if (LOG_TO_STDOUT && isatty(fileno(fp))) {
    fprintf(fp, "%s[%s] [%s] %s%s\n", level_color[level], time_buf, level_str[level], msg, C_RESET);
  } else {
    fprintf(fp, "[%s] [%s] %s\n", time_buf, level_str[level], msg);
  }

  fflush(fp);
  if (!LOG_TO_STDOUT) fclose(fp);
}

void mirror_log(int level, const char* fmt, ...) {
  va_list ap;
  char msg[4096];

  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  mirror_log_raw(level, msg);
}