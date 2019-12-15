#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <tuple>
#include <unistd.h>

#define BUFFER_SIZE 4096
/*This file does the kvs operations
 * such as reading or writing to the
 * KVS file. IT also updates some of
 * the data already written to the KVS
 * file and gives us the filesize of an
 * object within the KVS
 */
struct kvs_entry {
  int32_t block_nr;
  int32_t offset_entry;
  int64_t offset;
  int64_t total_length;
  int32_t length;
  char name[41];
};

extern int32_t entries_seen;

void insert(struct kvs_entry *entry);
ssize_t update(ssize_t fd, struct kvs_entry *entry);
bool search(struct kvs_entry *entry);
kvs_entry *get(struct kvs_entry *entry);

ssize_t kvs_write_entry(ssize_t fd, char *http, int32_t offset,
                        int32_t object_size, int32_t length, int32_t block_nr) {
  // printf("Entries seen atm: %d\n", entries_seen);
  struct kvs_entry entry;
  // printf("size of kvs entry: %zd\n", sizeof(kvs_entry));
  /*initialzing the index entry with the data*/
  strcpy(entry.name, http);
  entry.length = length;
  entry.offset_entry = entries_seen;
  entry.offset = offset;
  entry.block_nr = block_nr;

  /*If it's the first block, its total_length attribute gets set*/
  if (block_nr == 0) {
    // printf("Writing object size of %d to kvs block 0\n", object_size);
    /*If value is already present, update it*/
    entry.total_length = object_size;
    if (search(&entry) == true) {
      // printf("seen object block0!\n");
      ssize_t new_offset = update(fd, &entry);
      return new_offset;
    }
  } else {
    entry.total_length = 0;
    /*If value is already present, update it*/
    if (search(&entry) == true) {
      ssize_t new_offset = update(fd, &entry);
      return new_offset;
    }
  }

  /*New entry that gets inserterd
   * enries_seen global variable is protected by a lock
   * */
  insert(&entry);
  if (pwrite(fd, &entry, sizeof(entry), entries_seen * sizeof(entry)) < 0) {
    perror("write entry error");
  }
  entries_seen++;
  return offset;
}

/*Writes the data*/
void kvs_write(ssize_t fd, ssize_t offset, char buffer[BUFFER_SIZE]) {
  if (pwrite(fd, buffer, 4096, offset) < 0) {
    perror("kvs_write error\n");
    return;
  }
  return;
}

/*Updates the entry in the index of the kvs store,
 * this function is linked to Update in linearprobe.cpp*/
void kvs_update_entry(ssize_t fd, kvs_entry *new_entry,
                      struct kvs_entry *old_entry) {
  if (pwrite(fd, new_entry, sizeof(kvs_entry),
             old_entry->offset_entry * sizeof(kvs_entry)) < 0) {
    // perror("Updating entry in kvs error");
  }
}

/*Reads data from the kvs store when it is called from the cache*/
char *kvs_read(ssize_t fd, char *httpname, ssize_t block_nr) {
  struct kvs_entry find;
  strcpy(find.name, httpname);
  find.block_nr = block_nr;
  struct kvs_entry *found_entry = get(&find);
  if (found_entry == nullptr) {
    perror("Read Null entry error?");
    return NULL;
  }

  char buffer[BUFFER_SIZE];
  // printf("Reading something from the kvs!\n");
  if (pread(fd, buffer, found_entry->length, found_entry->offset) < 0) {
    perror("Reading from kvs error");
  }
  // write(1, buffer, found_entry->length);
  char *data = new char[BUFFER_SIZE];
  /*avoid heap overflow*/
  strncpy(data, buffer, found_entry->length);
  return data;
}

/*Seperate function to get the length so we are not writing more
 * data than is there back to the client*/
ssize_t kvs_read_length(char *httpname, ssize_t block_nr) {
  struct kvs_entry find;
  strcpy(find.name, httpname);
  find.block_nr = block_nr;
  struct kvs_entry *found_entry = get(&find);
  // printf("Reading the size from the kvs! %d\n", found_entry->length);
  return found_entry->length;
}

/*So unlike in the design doc, KV info only gets the filesize for the
 * GET requests, updating objects or creating them into the hash or the
 * KVS is done by either kvs_write_entry or kvs_update_entry*/
ssize_t kvs_info(/*struct kvs_entry entry*/ char *object_name, ssize_t length) {
  struct kvs_entry find;
  strcpy(find.name, object_name);
  find.block_nr = 0;
  if (length == -1) {
    struct kvs_entry *found_entry = get(&find);
    if (found_entry == NULL) {
      perror("entry could not be found");
      return -2;
    } else {
      return found_entry->total_length;
    }
  }
  return 0;
}

// Independent test code Ima just leave here
/*int main() {
  const char *test = "test1";
  ssize_t fd = open(test, O_RDWR);
  char *name = (char *)malloc(41);
  strcpy(name, "0123456789012345678901234567890123456789");
  int32_t offset = 0;
  int32_t block_nr = 69;
  int32_t length = 11123123;
  kvs_write_entry(fd, name, offset, length, block_nr);

  strcpy(name, "0123456789012345678901234567890123456788");
  offset = 4;
  block_nr = 70;
  length = 12233;
  kvs_write_entry(fd, name, offset, length, block_nr);

  strcpy(name, "0123456789012345678901234567890123456787");
  offset = 3;
  block_nr = 2;
  length = 133;
  kvs_write_entry(fd, name, offset, length, block_nr);
  struct kvs_entry find;
  ssize_t where = 0;
  while (ssize_t read_bytes = pread(fd, &find, sizeof(kvs_entry), where)) {
    if (find.length <= 0) {
      perror("no more entries\n");
      break;
    } else {
      printf("name: %s\nblock nr: %d\nlength: %d\n", find.name, find.block_nr,
             find.length);
      where += read_bytes;
      // break;
    }
  }
}*/
