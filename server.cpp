#include <errno.h>
#include <list>
#include <map>
#include <netdb.h>
#include <pthread.h>
#include <queue>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define BUFFER_SIZE 4096
#define SIZE 800000
#define MAPSIZE 10000
struct block_data {
  char data[4096];
  ssize_t len;
  std::string key;
};

struct kvs_entry {
  int32_t block_nr;
  int32_t offset_entry;
  int64_t offset;
  int64_t total_length;
  int32_t length;
  char name[41];
};

struct map_entry {
  char alias[128];
  char key[128];
  int32_t offset_entry;
};

std::queue<int> connection;
/*Use for the LRU cache*/
/*Data structures used for the cache*/
pthread_mutex_t lock;
pthread_mutex_t writer;
pthread_cond_t cv;
pthread_cond_t init;
pthread_cond_t read_lock;
/*for condition variables*/
int8_t cond = 0;
int8_t init_cond = 0;
int8_t threads_running = 0;
// int8_t writers = 0;
/*Initial file descriptor*/
ssize_t alias_fd = 0;
ssize_t fd = 0;
/*Name of the object file that is determined at run time*/
char *object_name;
char *alias_name;
int32_t entries_seen = 0;
int32_t alias_seen = 0;
int32_t writers = 0;
void init_hash_function();
void insert(struct kvs_entry *entry);
bool search(struct kvs_entry *entry);
void init_list(int32_t size);
void print_hash();
void push_to_front(struct block_data *entry, int8_t flag);

void add_and_write(ssize_t fd, const char *entry, char buffer[BUFFER_SIZE],
                   ssize_t read_bytes, ssize_t filesize, ssize_t block_nr,
                   ssize_t offset, char *name);

block_data *get_from_cache(char *httpname, ssize_t block_nr, ssize_t fd);
void *dispatch(void *);
void alias_insert(struct map_entry *entry);
bool alias_search(struct map_entry *entry);
bool key_search(struct map_entry *entry);
void alias_update(ssize_t fd, struct map_entry *entry);
void alias_kvs_write_entry(ssize_t fd, struct map_entry entry);
void print_alias();
void init_alias_function();
ssize_t kvs_info(char *object_name, ssize_t length);
char *lookup(char *alias);
void thread_close(int cl) {
  pthread_mutex_lock(&lock);
  threads_running--;
  close(cl);
  pthread_cond_signal(&cv);
  pthread_mutex_unlock(&lock);
  dispatch(NULL);
}
// Handles response
// Creates file or return data for client
void handle_response(int cl) {
  /*receive the header for parsing*/
  char header[4096];
  if (recv(cl, header, sizeof(header), 0) < 0) {
    return;
  }

  char *saveptr1;
  char *response = strtok_r(header, " ", &saveptr1);
  if (strcmp(response, "PUT") == 0) {
    char *httpname;
    httpname = strtok_r(NULL, " ", &saveptr1);
    // printf("%s\n", httpname);
    response = strtok_r(NULL, " ", &saveptr1);
    ssize_t object_size = atoi(strtok_r(NULL, " ", &saveptr1));
    // printf("Incoming Content Length: %zd\n", object_size);
    char httparray[128];
    /*check if httpname is valid length*/
    strncpy(httparray, httpname, strlen(httpname));
    if (httparray[0] == '/') {
      httpname = strtok_r(httpname, "/", &saveptr1);
    }

    if (strlen(httpname) != 40) {
      httpname = lookup(httpname);
    }

    char const *response_message;
    if (httpname == nullptr) {
      response_message = "HTTP/1.1 404 File does not exist\r\n\r\n";
      send(cl, response_message, strlen(response_message), 0);
      thread_close(cl);
    }

    /*Used to generate a 201 code after writing*/
    int8_t create = 0;
    if (kvs_info(httpname, -1) == -2) {
      create = 1;
    }

    pthread_mutex_lock(&lock);
    char buffer[BUFFER_SIZE];
    /*Writing to httpname from filename data*/
    // printf("%d\n", writers);
    ssize_t bytes_left = object_size;
    ssize_t counter = 0;
    ssize_t bytes_written = 0;
    while (ssize_t read_bytes = recv(cl, buffer, sizeof(buffer), 0)) {
      // printf("Read block: %zd from client\n", counter);
      char key[200];
      struct stat file;
      ssize_t filesize = 0;
      /*updating the file size as we go on*/
      if ((stat(object_name, &file)) == 0) {
        filesize = file.st_size;
      }
      snprintf(key, 200, "%s,%zd", httpname, counter);
      add_and_write(fd, key, buffer, read_bytes, object_size, counter, filesize,
                    httpname);
      bytes_written = bytes_written + read_bytes;
      counter++;
      bytes_left = bytes_left - read_bytes;
      if (bytes_left <= 0) {
        break;
      }
    }
    /*leave like this or else it breaks everything*/
    pthread_mutex_unlock(&lock);
    // printf("End of loop!\n");
    if (strlen(httpname) != 40) {
      response_message = "HTTP/1.1 400 Bad Request\r\n\r\n";
      send(cl, response_message, strlen(response_message), 0);
      thread_close(cl);
    }
    /*check if the resource is already created or not*/
    if (create == 1) {
      response_message = "HTTP/1.1 201 CREATED\r\n\r\n";
      send(cl, response_message, strlen(response_message), 0);
      // printf("sending response!\n");
      thread_close(cl);
    } else {
      response_message = "HTTP/1.1 200 OK\r\n\r\n";
      // write(1, response_message, strlen(response_message));
      send(cl, response_message, strlen(response_message), 0);
      // printf("sending response!\n");
      thread_close(cl);
    }

    // dispatch(NULL);
  } else if (strcmp(response, "GET") == 0) {
    char *httpname;
    char httparray[128];
    /*check if httpname is valid length*/
    httpname = strtok_r(NULL, " ", &saveptr1);
    strncpy(httparray, httpname, strlen(httpname));
    if (httparray[0] == '/') {
      httpname = strtok_r(httpname, "/", &saveptr1);
    }

    // printf("Received GET: %s\n", httpname);

    char const *response_message;
    char *final_name;
    /*If name is not 40 characters long, we must resolve it*/
    if (strlen(httpname) != 40) {
      final_name = lookup(httpname);
    } else {
      final_name = httpname;
    }

    // printf("Final name; %s\n", final_name);
    // printf("here!\n");
    if (final_name == nullptr) {
      response_message = "HTTP/1.1 404 File not found\r\n\r\n";
      fprintf(stderr, "file does not exist: %s\n", strerror(errno));
      send(cl, response_message, 4096, 0);
      thread_close(cl);
    }

    ssize_t filesize = kvs_info(final_name, -1);
    char getheader[4096];
    response_message = "HTTP/1.1 200 OK\r\n";
    snprintf(getheader, 4096, "%sContent-Length: %zd\r\n\r\n", response_message,
             filesize);
    send(cl, getheader, strlen(getheader), 0);
    /*file exists but no read permissions, 403 error*/
    pthread_mutex_lock(&lock);
    /*Since we know file size, we just have to keep track how much relative data
     * has been sent so far*/
    ssize_t filesize_left = filesize;
    ssize_t counter = 0;
    /*Writing to client for GET Only interacts with the Cache*/
    while (filesize_left > 0) {
      // printf("filesize left: %zd\n", filesize_left);
      struct block_data *block = get_from_cache(final_name, counter, fd);
      if (send(cl, block->data, block->len, 0) < 0) {
        perror("send error\n");
      }
      filesize_left = filesize_left - block->len;
      counter++;
    }
    threads_running--;
    close(cl);
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&lock);
    dispatch(NULL);
  } else if (strcmp(response, "PATCH") == 0) {
    char *value; // can also be key
    char *alias;
    char valuearray[128];
    char aliasarray[128];
    value = strtok_r(NULL, " ", &saveptr1);
    value = strtok_r(NULL, " ", &saveptr1);
    value = strtok_r(NULL, " ", &saveptr1);
    strncpy(valuearray, value, strlen(value));
    alias = strtok_r(NULL, " ", &saveptr1);
    /*eliminate /r/n on alias*/
    strncpy(aliasarray, alias, strlen(alias) - 2);
    // printf("%s %zu", aliasarray, strlen(aliasarray));
    if (aliasarray[0] == '/') {
      alias = strtok_r(aliasarray, "/", &saveptr1);
    }

    strncpy(alias, aliasarray, strlen(alias));

    if (valuearray[0] == '/') {
      value = strtok_r(value, "/", &saveptr1);
    }
    // printf("Httpname or Alias: %s %zd\n", value, strlen(value));
    // printf("Alias: %s %zd\n", alias, strlen(alias));
    char const *response_message;
    /*Number of characters excluding null cant be greater than 126*/
    if ((strlen(value) + strlen(alias)) > 126) {
      response_message = "HTTP/1.1 400 BAD REQUEST\r\n\r\n";
      fprintf(stderr, "BAD REQUEST: %s\n", strerror(errno));
      send(cl, response_message, 4096, 0);
      thread_close(cl);
    }

    // pthread_mutex_lock(&lock);
    struct map_entry entry;
    strncpy(entry.alias, alias, strlen(alias));
    strncpy(entry.key, value, strlen(value));
    entry.offset_entry = alias_seen;
    /*If an alias of this type already exists, update that entry to new key*/
    if (alias_search(&entry) == true) {
      /*basically have to check that the updated entry is a valid key
       * This does a key check or an httpname check
       * */
      if (key_search(&entry) == false) {
        if (kvs_info(value, -1) == -2) {
          // printf("No matching key that exists!\n");
          response_message = "HTTP/1.1 404 File not found\r\n\r\n";
          fprintf(stderr, "Key does not exist: %s\n", strerror(errno));
          send(cl, response_message, 4096, 0);
          thread_close(cl);
        } else {
          // printf("Writing new entry to an alias\n");
          alias_update(alias_fd, &entry);
          response_message = "HTTP/1.1 200 OK\r\n\r\n";
          send(cl, response_message, 4096, 0);
          thread_close(cl);
        }
      }
      // printf("Writing new entry to an alias\n");
      alias_update(alias_fd, &entry);
      response_message = "HTTP/1.1 201 CREATED\r\n\r\n";
      send(cl, response_message, 4096, 0);
      thread_close(cl);
      /* Checks to see if the key matches any known aliases*/
    } else if (key_search(&entry) == true) {
      pthread_mutex_lock(&lock);
      // printf("Writing new entry to a key\n");
      alias_kvs_write_entry(alias_fd, entry);
      response_message = "HTTP/1.1 201 CREATED\r\n\r\n";
      send(cl, response_message, 4096, 0);
      pthread_mutex_unlock(&lock);
      thread_close(cl);
    }
    /*Last case where we can possibly link alias to httpname*/
    if (kvs_info(value, -1) == -2) {
      // printf("No matching key that exists!\n");
      response_message = "HTTP/1.1 404 File not found\r\n\r\n";
      fprintf(stderr, "Key does not exist: %s\n", strerror(errno));
      send(cl, response_message, 4096, 0);
      thread_close(cl);
    } else {
      pthread_mutex_lock(&lock);
      // printf("Writing new entry to http\n");
      alias_kvs_write_entry(alias_fd, entry);
      response_message = "HTTP/1.1 200 OK\r\n\r\n";
      send(cl, response_message, 4096, 0);
      pthread_mutex_unlock(&lock);
      thread_close(cl);
    }
  }
  /*if neither a GET or PUT, internal server error*/
  else {
    char const *response_message;
    response_message = "HTTP/1.1 500 Bad Internal Server Error\r\n\r\n";
    send(cl, response_message, strlen(response_message), 0);
    thread_close(cl);
  }
  return;
}

void *dispatch(void *arg) {
  pthread_mutex_lock(&lock);
  if (arg == NULL) {
    while (init_cond == 0) {
      // printf("Initially waiting...\n");
      pthread_cond_wait(&init, &lock);
    }
  }
  pthread_mutex_unlock(&lock);
  /*only here to silence a warning*/
  int cl = connection.front();
  connection.pop();
  init_cond = 0;
  handle_response(cl);
  return NULL;
}

int main(int argc, char **argv) {
  /*parsing the command line for socket set up*/
  int32_t size = 40;
  int8_t threads = 4;
  int8_t opt;
  int8_t address_loc = 1;
  while ((opt = getopt(argc, argv, "N:c:f:m:")) != -1) {
    switch (opt) {
    case 'N':
      if (optarg == NULL) {
        address_loc++;
      } else {
        threads = atoi(optarg);
        address_loc = address_loc + 2;
      }
      break;
    case 'c':
      if (optarg == NULL) {
        size = 40;
        address_loc++;
      } else {
        size = atoi(optarg);
        address_loc = address_loc + 2;
      }
      break;
    case 'f': {
      object_name = optarg;
      address_loc = address_loc + 2;
    } break;
    case 'm': {
      alias_name = optarg;
      address_loc = address_loc + 2;
    } break;
    default:
      break;
    }
  }

  if (object_name == nullptr) {
    perror("object file not defined");
    return 0;
  }

  if (alias_name == nullptr) {
    perror("alias file not defined");
    return 0;
  }

  printf("Looking at alias name file!\n");
  alias_fd = open(alias_name, O_RDWR);
  /*if object name doesn't exist, make one*/
  if (errno == ENOENT) {
    // perror("did not find a file server\n");
    mode_t mode = S_IRUSR | S_IWUSR;
    alias_fd = open(alias_name, O_CREAT | O_RDWR, mode);
    /*initialzing the index portion of the file with dummy indexes*/
    init_alias_function();
    struct map_entry entry = {{'\0'}, {'\0'}, -1};
    int32_t entries_written = 0;
    while (entries_written < MAPSIZE) {
      if (write(alias_fd, &entry, sizeof(map_entry)) < 0) {
        perror("Initialize file with entries error");
        break;
      } else {
        entries_written++;
      }
    }
  } else {
    init_alias_function();
    struct map_entry find; //= (kvs_entry *)malloc(sizeof(kvs_entry));
    int32_t bytes_seen = 0;
    while (ssize_t read_bytes =
               pread(alias_fd, &find, sizeof(map_entry), bytes_seen)) {
      /*If found a default entry index. exit this loop*/
      if (find.offset_entry == -1) {
        perror("no more entries\n");
        break;
      } else {
        alias_insert(&find);
        alias_seen++;
        if (alias_seen > MAPSIZE) {
          break;
        }
        bytes_seen += read_bytes;
      }
    }
    // print_alias();
  }

  printf("Now looking at object file!\n");
  fd = open(object_name, O_RDWR);
  /*if object name doesn't exist, make one*/
  if (fd == -1) {
    // perror("did not find a file server\n");
    mode_t mode = S_IRUSR | S_IWUSR;
    fd = open(object_name, O_CREAT | O_RDWR, mode);
    /*initialzing the index portion of the file with dummy indexes*/
    init_hash_function();
    struct kvs_entry entry = {
        -1, 0, 0, 0, -1, "0123456789012345678901234567890123456789"};
    int32_t entries_written = 0;
    while (entries_written < SIZE) {
      if (write(fd, &entry, sizeof(kvs_entry)) < 0) {
        perror("Initialize file with entries error");
        break;
      } else {
        entries_written++;
      }
    }
  } else {
    init_hash_function();
    struct kvs_entry find; //= (kvs_entry *)malloc(sizeof(kvs_entry));
    int32_t bytes_seen = 0;
    while (ssize_t read_bytes =
               pread(fd, &find, sizeof(kvs_entry), bytes_seen)) {
      /*If found a default entry index. exit this loop*/
      if (find.length < 0) {
        perror("no more entries\n");
        break;
      } else {
        insert(&find);
        entries_seen++;
        if (entries_seen > SIZE) {
          break;
        }
        bytes_seen += read_bytes;
      }
    }
    // print_hash();
  }

  if (threads < 4 || size < 40) {
    perror("too small inputs");
    return 0;
  }

  /*preallocating the list*/
  init_list(size);
  char *saveptr1;
  char *address = strtok_r(argv[address_loc], ":", &saveptr1);
  char *hostname = address;
  address = strtok_r(NULL, ":", &saveptr1);
  uint16_t portnumber = 0;
  if (address == NULL) {
    portnumber = 80;
  } else {
    portnumber = atoi(address);
  }

  struct hostent *hent = gethostbyname(hostname);
  struct sockaddr_in addr;
  memcpy(&addr.sin_addr.s_addr, hent->h_addr, hent->h_length);
  addr.sin_port = htons(portnumber);
  addr.sin_family = AF_INET;

  int sock;
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("error");
    exit(1);
  }

  int enable = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable))) {
    perror("set sock error");
    exit(1);
  }

  /*initializing the worker threads*/
  pthread_t *worker;
  worker = new pthread_t[threads];
  for (int i = 0; i < threads; i++) {
    pthread_create(&worker[i], NULL, dispatch, NULL);
  }

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind error");
    exit(1);
  }

  if (listen(sock, 0) < 0) {
    perror("listen error");
    exit(1);
  }

  int cl;
  while ((cl = accept(sock, NULL, NULL)) != -1) {
    /*if more threads are running than available, wait*/
    pthread_mutex_lock(&lock);
    while (threads_running >= threads) {
      pthread_cond_wait(&cv, &lock);
    }

    pthread_mutex_unlock(&lock);
    connection.push(cl);
    init_cond = 1;
    threads_running++;
    pthread_cond_signal(&init);
  }

  delete (worker);
  return 0;
}
