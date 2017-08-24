extern "C" {
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "postgres.h"
}
#include "file.h"


struct plv8u_file_status *stat_file (const char *filename) {
  int ret;
  static struct stat stat_buf;
  struct plv8u_file_status *status = (struct plv8u_file_status *) palloc(sizeof(struct plv8u_file_status));

  ret = stat(filename, &stat_buf);

  if (ret == -1) {
    status->error = errno;

    return status;
  }

  /* start creating the json return result */
  status->error = 0;
  status->stat_buf = &stat_buf;

  return status;
}
