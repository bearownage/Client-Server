#include <stdio.h>
#include <string.h>
#include <string>
#include <unistd.h>
#define MAPSIZE 10000

struct map_entry {
  char alias[128];
  char key[128];
  int32_t offset_entry;
};

struct map_entry *map[MAPSIZE];
void alias_update_entry(ssize_t fd, map_entry *new_entry, map_entry *old_entry);

void init_alias_function() {
  for (int32_t i = 0; i < MAPSIZE; i++) {
    map[i] = (map_entry *)malloc(sizeof(map_entry));
    map[i]->offset_entry = -1;
  }
}

void print_alias() {
  for (int16_t i = 0; i < MAPSIZE; i++) {
    if (map[i]->offset_entry != -1) {
      printf("Alias: %s Key: %s\n", map[i]->alias, map[i]->key);
    }
  }
}

int hash_function(char alias[87]) {
  int32_t sum = 0;
  for (uint8_t i = 0; i < strlen(alias); i++) {
    sum += (int)alias[i];
  }
  return sum % MAPSIZE;
}

/*inserts a struct into the hash table*/
void alias_insert(struct map_entry *entry) {
  /*preallocate the entry so it updates*/
  map_entry *insert_entry = (map_entry *)malloc(sizeof(map_entry));
  strcpy(insert_entry->alias, entry->alias);
  strcpy(insert_entry->key, entry->key);
  insert_entry->offset_entry = entry->offset_entry;
  // printf("inserting entry: Alias: %s Key: %s Offset: %d\n",
  // insert_entry->alias,
  //       insert_entry->key, insert_entry->offset_entry);
  ssize_t count = 0;
  ssize_t key = hash_function(entry->alias);
  /*See if the first spot is a default entry that has been unused*/
  if (map[key]->offset_entry == -1) {
    free(map[key]);
    map[key] = NULL;
    map[key] = insert_entry;
  } else {
    /*loop till we find a new entry*/
    while (map[key]->offset_entry != -1) {
      key = (key + 1) % MAPSIZE;
      count++;
      if (count == MAPSIZE) {
        perror("Table is full for insert");
        return;
      }
    }
    free(map[key]);
    map[key] = NULL;
    map[key] = insert_entry;
  }
}
/*Updates an existing alias*/
void alias_update(ssize_t fd, struct map_entry *entry) {
  /*preallocate the entry so it updates*/
  map_entry *insert_entry = (map_entry *)malloc(sizeof(map_entry));
  strcpy(insert_entry->alias, entry->alias);
  strcpy(insert_entry->key, entry->key);
  // printf("Updating entry: Alias: %s Key: %s\n", insert_entry->alias,
  //       insert_entry->key);
  ssize_t count = 0;
  ssize_t key = hash_function(entry->alias);
  /*See if the first spot is a default entry that has been unused*/
  if (strcmp(map[key]->alias, entry->alias) == 0) {
    insert_entry->offset_entry = map[key]->offset_entry;
    alias_update_entry(fd, insert_entry, map[key]);
    free(map[key]);
    map[key] = NULL;
    map[key] = insert_entry;
  } else {
    /*loop till we find a new entry*/
    while (strcmp(map[key]->alias, entry->alias) != -0) {
      key = (key + 1) % MAPSIZE;
      count++;
      if (count == MAPSIZE) {
        perror("Table is full");
        return;
      }
    }
    map[key]->offset_entry = insert_entry->offset_entry;
    alias_update_entry(fd, insert_entry, map[key]);
    free(map[key]);
    map[key] = NULL;
    map[key] = insert_entry;
  }
}

/*Searches to see if an alias exists*/
bool alias_search(struct map_entry *entry) {
  /*preallocate the entry so it updates*/
  ssize_t count = 0;
  ssize_t key = hash_function(entry->alias);
  /*See if the first spot is a default entry that has been unused*/
  if (strcmp(map[key]->alias, entry->alias) == 0) {
    return true;
  } else {
    /*loop till we find a new entry*/
    while (strcmp(map[key]->alias, entry->alias) != -0) {
      key = (key + 1) % MAPSIZE;
      count++;
      if (count == MAPSIZE) {
        perror("Table is full");
        return false;
      }
    }
    return true;
  }
}

/*Checks to see if any known alias matches a new key*/
bool key_search(struct map_entry *entry) {
  /*preallocate the entry so it updates*/
  ssize_t count = 0;
  ssize_t key = hash_function(entry->key);
  /*See if the first spot is a default entry that has been unused*/
  if (strcmp(map[key]->alias, entry->key) == 0) {
    return true;
  } else {
    /*loop till we find a new entry*/
    while (strcmp(map[key]->alias, entry->key) != 0) {
      key = (key + 1) % MAPSIZE;
      count++;
      if (count == MAPSIZE) {
        perror("Table is full");
        return false;
      }
    }
    return true;
  }
}

/*Returns the key for an alias*/
char *alias_get(struct map_entry *entry) {
  /*preallocate the entry so it updates*/
  ssize_t count = 0;
  ssize_t key = hash_function(entry->alias);
  /*See if the first spot is a default entry that has been unused*/
  if (strcmp(map[key]->alias, entry->alias) == 0) {
    return map[key]->key;
  } else {
    /*loop till we find a new entry*/
    while (strcmp(map[key]->alias, entry->alias) != -0) {
      key = (key + 1) % MAPSIZE;
      count++;
      if (count == MAPSIZE) {
        perror("Table is full");
        return NULL;
      }
    }
    return map[key]->key;
  }
}
