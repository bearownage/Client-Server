#include <stdio.h>
#include <string.h>
#include <string>
#include <unistd.h>
#define SIZE 800000
#define MAPSIZE 10000;
/* This function replicates linear probing
 * Code based off of
 * https://github.com/satylogin/general-adt-and-algorithms/blob/master/hashing-linear-probing
 */
struct kvs_entry {
  int32_t block_nr;
  int32_t offset_entry;
  int64_t offset;
  int64_t total_length;
  int32_t length;
  char name[41];
};

struct kvs_entry *hash[SIZE];
void kvs_update_entry(ssize_t fd, kvs_entry *new_entry,
                      struct kvs_entry *old_entry);
void print_hash() {
  for (int32_t i = 0; i < SIZE; i++) {
    if (hash[i]->block_nr != -1) {
      printf("Key at %d: %s %d %d\n", i, hash[i]->name, hash[i]->block_nr,
             hash[i]->length);
    } else {
      // printf("%d\n",i);
    }
  }
}

void init_hash_function() {
  for (int32_t i = 0; i < SIZE; i++) {
    hash[i] = (kvs_entry *)malloc(sizeof(kvs_entry));
    hash[i]->block_nr = -1;
  }
}

/* Gives the key an index into the hash array*/
int hash_function(char name[41], int32_t block_nr) {
  int32_t sum = 0;
  for (uint8_t i = 0; i < strlen(name); i++) {
    sum += (int)name[i];
  }
  sum += block_nr;
  return sum % SIZE;
}

/*inserts a struct into the hash table*/
void insert(struct kvs_entry *entry) {
  /*preallocate the entry so it updates*/
  kvs_entry *insert_entry = (kvs_entry *)malloc(sizeof(kvs_entry));
  insert_entry->block_nr = entry->block_nr;
  insert_entry->length = entry->length;
  insert_entry->total_length = entry->total_length;
  insert_entry->offset = entry->offset;
  insert_entry->offset_entry = entry->offset_entry;
  strncpy(insert_entry->name, entry->name, 41);
  ssize_t count = 0;
  ssize_t key = hash_function(entry->name, entry->block_nr);
  /*See if the first spot is a default entry that has been unused*/
  if (hash[key]->block_nr <= -1) {
    free(hash[key]);
    hash[key] = NULL;
    hash[key] = insert_entry;
  } else {
    /*loop till we find a new entry*/
    while (hash[key]->block_nr != -1) {
      key = (key + 1) % SIZE;
      count++;
      if (count == SIZE) {
        perror("Table is full");
        return;
      }
    }
    free(hash[key]);
    hash[key] = NULL;
    hash[key] = insert_entry;
  }
}

/* Updates the entry already in the hash and returns the offet
 * Make sure it updates in the kvs as well.
 * */
ssize_t update(ssize_t fd, struct kvs_entry *entry) {
  /*initialize entry*/
  kvs_entry *update_entry = (kvs_entry *)malloc(sizeof(kvs_entry));
  // printf("\nUpdate name:%s\nUpdate Block_nr:%d\n", entry->name,
  //       entry->block_nr);
  update_entry->block_nr = entry->block_nr;
  update_entry->length = entry->length;
  update_entry->total_length = entry->total_length;
  strcpy(update_entry->name, entry->name);
  int32_t count = 0;
  ssize_t key = hash_function(entry->name, entry->block_nr);
  /*first check place*/
  if ((strcmp(hash[key]->name, update_entry->name) == 0) &&
      hash[key]->block_nr == update_entry->block_nr) {
    /*add info to the new entry*/
    update_entry->offset = hash[key]->offset;
    update_entry->offset_entry = hash[key]->offset_entry;
    kvs_update_entry(fd, update_entry, hash[key]);
    free(hash[key]);
    hash[key] = NULL;
    hash[key] = update_entry;
    return hash[key]->offset;
  } else {
    /*loop through rest of the entries*/
    while ((strcmp(hash[key]->name, update_entry->name) != 0) ||
           (hash[key]->block_nr != update_entry->block_nr)) {
      key = (key + 1) % SIZE;
      count++;
      if (count == SIZE) {
        perror("Table is full");
        return 0;
      }
    }
    /*add info to the new entry*/
    update_entry->offset_entry = hash[key]->offset_entry;
    update_entry->offset = hash[key]->offset;
    kvs_update_entry(fd, update_entry, hash[key]);
    // printf("Before filesize %d\n", hash[key]->total_length);
    free(hash[key]);
    hash[key] = NULL;
    hash[key] = update_entry;
    // printf("Update filesize %d\n", hash[key]->total_length);
    return hash[key]->offset;
  }
}
/*Searches for an entry, if found return true
 * if not found, return false*/
bool search(struct kvs_entry *entry) {
  // printf("\nSearch name:%s\nSearch Block_nr:%d\n", entry->name,
  //       entry->block_nr);

  ssize_t count = 0;
  ssize_t key = hash_function(entry->name, entry->block_nr);
  if ((strcmp(hash[key]->name, entry->name) == 0) &&
      hash[key]->block_nr == entry->block_nr) {
    // printf("Search found!\n");
    return true;
  } else {
    while ((strcmp(hash[key]->name, entry->name) != 0) ||
           (hash[key]->block_nr != entry->block_nr)) {
      key = (key + 1) % SIZE;
      count++;
      // printf("%zd\n", count);
      if (count == SIZE) {
        // perror("Search cannot find entry");
        return false;
      }
    }
    // printf("search found!");
    return true;
  }
}

/*Gets a kvs entry from the hashtable*/
kvs_entry *get(struct kvs_entry *entry) {
  ssize_t count = 0;
  // printf("\nGet name: %s\nGet block_nr: %d\n", entry->name, entry->block_nr);
  /*Hash string function to get index*/
  ssize_t key = hash_function(entry->name, entry->block_nr);
  if ((strcmp(hash[key]->name, entry->name) == 0) &&
      hash[key]->block_nr == entry->block_nr) {

    // printf("Get Size: %d\n", hash[key]->length);
    return hash[key];
  } else {
    while ((strcmp(hash[key]->name, entry->name) != 0) ||
           (hash[key]->block_nr != entry->block_nr)) {
      key = (key + 1) % SIZE;
      count++;
      if (count == SIZE) {
        perror("Get cannot find entry\n");
        return NULL;
      }
    }
    // printf("Get Size: %d\n", hash[key]->length);
    return hash[key];
  }
}
