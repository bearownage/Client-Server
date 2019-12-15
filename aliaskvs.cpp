#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <tuple>
#include <unistd.h>
pthread_mutex_t locker;
extern int alias_seen;
int recursion_counter = 0;
struct map_entry {
  char alias[128];
  char key[128];
  int32_t offset_entry;
};

void alias_insert(struct map_entry *entry);
char *alias_get(struct map_entry *entry);
bool alias_search(struct map_entry *entry);
char *name_resolve(char *name);
void alias_kvs_write_entry(ssize_t fd, struct map_entry entry) {
  //printf("%s %s %d\n", entry.key, entry.alias, entry.offset_entry);
  //printf("alias_kvs_write_entry\n");
  alias_insert(&entry);
  if (pwrite(fd, &entry, sizeof(map_entry), alias_seen * sizeof(map_entry)) <
      0) {
    perror("Alias write error");
    return;
  }
  alias_seen++;
}
/*updates a name entry*/
void alias_update_entry(ssize_t fd, map_entry *new_entry,
                        map_entry *old_entry) {
  // printf("Updating entry! %d\n", old_entry->offset_entry);
  if (pwrite(fd, new_entry, sizeof(map_entry),
             old_entry->offset_entry * sizeof(map_entry)) < 0) {
    perror("error");
    return;
  }
}

/*Since put and get could both resolve names, we need this entrance function*/
char *lookup(char *alias) {
  pthread_mutex_lock(&locker);
  //printf("Counter when starts %d\n", recursion_counter);
  char *httpname = name_resolve(alias);
  pthread_mutex_unlock(&locker);
  return httpname;
}
/*Resolves the names recursively*/
char *name_resolve(char *name) {
  recursion_counter++;
  if (strlen(name) == 40) {
    char *resolved = (char *)malloc(41 * sizeof(char));
    strncpy(resolved, name, strlen(name) + 1);
    recursion_counter = 0;
    // printf("Returning this name!: %s %zu\n", resolved, strlen(resolved));
    return resolved;
  } else if (recursion_counter >= 8) {
    recursion_counter = 0;
    perror("loop detected maybe\n");
    return nullptr;
  } else {
    // printf("Searching...\n");
    struct map_entry search;
    strcpy(search.alias, name);
    if (alias_search(&search)) {
      char *find = alias_get(&search);
      return name_resolve(find);
    } else {
      recursion_counter = 0;
      return nullptr;
    }
  }
  recursion_counter = 0;
  return nullptr;
}
