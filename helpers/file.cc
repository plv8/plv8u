extern "C" {
#include <sys/stat.h>

#include "postgres.h"
}
#include <string>
#include <iostream>
#include <fstream>

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

struct plv8u_file_status *read_file (const char *filename) {
  size_t size;
  char *memblock;

  struct plv8u_file_status *stat_status = stat_file(filename);

  if (stat_status->error) {
    return stat_status;
  }

  struct plv8u_file_status *status = (struct plv8u_file_status *) palloc(sizeof(struct plv8u_file_status));

  std::ifstream file (filename);

  if (file.is_open()) {
    size = stat_status->stat_buf->st_size;

    memblock = (char *) palloc(size + 1);
    file.seekg (0, std::ios::beg);
    file.read (memblock, (std::streampos) size);
    file.close();

    status->error = 0;
    status->contents = memblock;
  } else {
    status->error = errno;
  }

  pfree(stat_status);
  return status;
}
