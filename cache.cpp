#include <errno.h>
#include <list>
#include <map>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <tuple>
#include <unistd.h>

#define BUFFER_SIZE 4096

struct block_data {
  char data[4096];
  ssize_t len;
  std::string key;
};

std::list<block_data *> LRU_cache;
std::map<std::string, block_data *> LRU_map;
ssize_t kvs_write_entry(ssize_t fd, char *http, int32_t offset,
                        int32_t object_size, int32_t length, int32_t block_nr);

void kvs_write(ssize_t fd, ssize_t offset, char buffer[BUFFER_SIZE]);

char *kvs_read(ssize_t fd, char *httpname, ssize_t block_nr);

ssize_t kvs_read_length(char *httpname, ssize_t block_nr);
void init_list(int size) {
  struct block_data *starter = (block_data *)malloc(size * 4096);
  starter->key = "0123456789012345678901234567890123456789,-1";
  snprintf(starter->data, 4096, "test test test");
  for (int8_t i = 0; i < size; i++) {
    LRU_cache.push_front(starter);
  }
}

/*This function pushes the new entry to the front
 * and deletes the tail from both the list and the map
 * if an entry already exists within the cache, move it to the front*/
void push_to_front(struct block_data *entry, int8_t flag) {
  /*If the cache does not have this block just delete the back*/
  if (flag == 0) {
    struct block_data *delete_block = (block_data *)malloc(8192);
    delete_block = LRU_cache.back();
    /*just check if it's one of the defaulted init entries to prevent a
     * posssible fault*/
    if (strcmp(delete_block->key.c_str(),
               "0123456789012345678901234567890123456789,-1") == 0) {
      LRU_cache.pop_back();
      LRU_cache.push_front(entry);
    } else {
      LRU_map.erase(delete_block->key);
      LRU_cache.pop_back();
      LRU_cache.push_front(entry);
    }
  }
  /*Moves the block already in the cache to the front with the new potential
   * data*/
  if (flag == 1) {
    std::list<block_data *>::iterator it;
    for (it = LRU_cache.begin(); it != LRU_cache.end(); it++) {
      if (strcmp((*it)->key.c_str(), entry->key.c_str()) == 0) {
        // printf("about to erase!: %s\n", (*it)->key.c_str());
        LRU_cache.erase(it);
        break;
      }
    }
    LRU_cache.push_front(entry);
  }
}

/*adds it to a cache potentially and
 * write it to disk*/
void add_and_write(ssize_t fd, const char *entry, char buffer[BUFFER_SIZE],
                   ssize_t read_bytes, ssize_t object_size, ssize_t block_nr,
                   ssize_t offset, char *name) {
  std::map<std::string, block_data *>::iterator it;
  std::string key(entry);
  it = LRU_map.find(entry);
  struct block_data *entry_key = (block_data *)malloc(8192);
  // snprintf(entry_key->data, read_bytes, "%s", buffer);
  strncpy(entry_key->data, buffer, read_bytes);
  // memcpy(entry_key->data, buffer, read_bytes);
  entry_key->len = read_bytes;
  entry_key->key = entry;
  /*if map does not already contain the entry, add it, ,unless, no*/
  if (it != LRU_map.end()) {
    /*Should update it again just to be safe since the data could change*/
    /*Delete the previous entry and add a possible newer version the entry
     * here*/
    kvs_write_entry(fd, name, offset, object_size, read_bytes, block_nr);
    kvs_write(fd, offset, buffer);
    LRU_map.erase(it);
    LRU_map.insert(std::pair<std::string, block_data *>(entry, entry_key));
    push_to_front(entry_key, 1);
    return;
  } else {
    ssize_t write_offset =
        kvs_write_entry(fd, name, offset, object_size, read_bytes, block_nr);
    kvs_write(fd, write_offset, buffer);
    /*add the completely new entry here*/
    LRU_map.insert(std::pair<std::string, block_data *>(entry, entry_key));
    push_to_front(entry_key, 0);
    // printf("add end of add and write\n");
    return;
  }
}

/*Getting blocks from cache*/
block_data *get_from_cache(char *httpname, ssize_t block_nr, ssize_t fd) {
  char temp_key[200];
  snprintf(temp_key, 200, "%s,%zd", httpname, block_nr);
  std::string key(temp_key);
  std::map<std::string, block_data *>::iterator it;
  it = LRU_map.find(key);
  /*if you found a block*/
  if (it != LRU_map.end()) {
    // printf("Found a block\n");
    push_to_front((*it).second, 1);
    return ((*it).second);
  } else {
    // printf("a new entry for get\n");
    /*Have to add a new entry to the hash map*/
    char *temp = kvs_read(fd, httpname, block_nr);
    ssize_t read_bytes = kvs_read_length(httpname, block_nr);
    // ssize_t read_bytes = pread(fd, buffer, sizeof(buffer), bytes_left);
    struct block_data *entry_key = (block_data *)malloc(4096);
    // snprintf(entry_key->data, read_bytes, "%s", buffer);
    strncpy(entry_key->data, temp, read_bytes);
    entry_key->len = read_bytes;
    entry_key->key = key;
    LRU_map.insert(std::pair<std::string, block_data *>(key, entry_key));
    push_to_front(entry_key, 0);
    return entry_key;
  }
}
