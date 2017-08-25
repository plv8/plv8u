#pragma once

#include <sys/stat.h>


struct plv8u_file_status {
  int error;
  struct stat *stat_buf;
  char *contents;
};

struct plv8u_file_status *read_file (const char *);
struct plv8u_file_status *stat_file (const char *);
