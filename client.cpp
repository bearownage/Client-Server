#include <cstdint>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define BUFFER_SIZE 4096
// Turns the send to a PUT request
// And turns the receive to a GET request
void client_command(int curr, char **argv, int sock) {
  char *command = strtok(argv[curr], ":");
  char *instruction = command;
  if (strcmp(instruction, "s") == 0) {
    char *filename = strtok(NULL, ":");
    char *httpname = strtok(NULL, ":");
    /*Commented out this check since we don't need to handle this anymore*/
    /*if (strlen(httpname) != 40) {
      fprintf(stderr, "httpname is not 40 characters\n");
      return;
    }*/

    /*check if the file exists or is a directory before any send/receive*/
    ssize_t fd;
    if ((fd = open(filename, O_RDWR)) == -1) {
      fprintf(stderr, "fd open error: %s\n", strerror(errno));
      close(fd);
      return;
    }

    struct stat file;
    ssize_t filesize = 0;
    /*updating the file size as we go on*/
    if ((stat(filename, &file)) == 0) {
      filesize = file.st_size;
    }

    char put[4096];
    snprintf(put, 4096, "PUT %s HTTP/1.1\r\nContent-Length: %zd\r\n", httpname,
             filesize);
    /*First send httpname to server so it can open path*/
    if (send(sock, put, strlen(put), 0) < 0) {
      perror("send put error");
    }

    /*Write to server*/
    char buffer[BUFFER_SIZE];
    while (ssize_t read_byte = read(fd, buffer, sizeof(buffer))) {
      // printf("%zd\n", read_byte);
      if (send(sock, buffer, read_byte, 0) < 0) {
        perror("send error PUT");
      }
    }

    // printf("test\n");
    char header[4096];
    if (recv(sock, header, sizeof(header), 0) < 0) {
      perror("receive error");
      return;
    }
    char *response = strtok(header, "\0");
    response = strtok(response, " ");
    char *code = strtok(NULL, " ");
    /*if any bad status codes, don't do any writing*/
    if (strcmp(code, "400") == 0 || strcmp(code, "403") == 0 ||
        strcmp(code, "500") == 0 || strcmp(code, "404") == 0) {
      fprintf(stderr, "Error token: %s %s\n", code, strerror(errno));
      close(fd);
      return;
    }

    close(fd);
    return;
  }
  if (strcmp(instruction, "r") == 0) {
    char *httpname = strtok(NULL, ":");
    char *filename = strtok(NULL, ":");
    /*if (strlen(httpname) != 40) {
      perror("httpname is not 40 characterss\n");
      return;
    }*/

    /*create the GET header*/
    char get[4096];
    snprintf(get, 4096, "GET %s HTTP/1.1\r\n", httpname);
    if (send(sock, get, strlen(get), 0) < 0) {
      perror("sending get error");
    }

    ssize_t read_bytes;
    /*Get the header response*/
    char header[4096];
    if ((read_bytes = recv(sock, header, sizeof(header), 0)) < 0) {
      perror("header GET recv");
    }
    /*Check the token code we got, if not 200, ignore and don't write*/
    // printf("%s", header);
    std::string str(header);
    // add a terminating character
    snprintf(header, 4097, "%s", str.c_str());
    std::string first_response = header;
    /*Getting the code*/
    std::string code = first_response.substr(9, 3);
    if (strcmp(code.c_str(), "400") == 0 || strcmp(code.c_str(), "403") == 0 ||
        strcmp(code.c_str(), "404") == 0 || strcmp(code.c_str(), "500") == 0) {
      fprintf(stderr, "Error token: %s %s\n", code.c_str(), strerror(errno));
      return;
    }

    ssize_t fd;
    /*Create a file if it doesn't exist*/
    if (access(filename, F_OK) == -1) {
      mode_t mode = S_IRUSR | S_IWUSR;
      fd = open(filename, O_CREAT | O_RDWR, mode);
    } else {
      /*allow the file to be overwritten*/
      fd = open(filename, O_RDWR | O_TRUNC);
    }

    /*Seperate header from first bytes of data to write to file*/
    size_t post = first_response.find("\r\n\r\n");
    std::string data = first_response.substr(post + 4);
    char const *response = data.c_str();
    if (write(fd, response, strlen(response)) < 0) {
      perror("write initial response error");
    }
    /*Writing the rest of data received from the server to the file*/
    char buffer[BUFFER_SIZE];
    // printf("Enter the reading rainbow\n");
    while ((read_bytes = recv(sock, buffer, sizeof(buffer), 0))) {
      if (write(fd, buffer, read_bytes) < 0) {
        perror("write");
        break;
      }
    }
    close(fd);
  } else if (strcmp(instruction, "a") == 0) {
    char *alias = strtok(NULL, ":");
    char *httpname = strtok(NULL, ":");
    char patch[4096];
    snprintf(patch, 4096, "PATCH %s HTTP/1.1\r\n\r\nALIAS %s %s\r\n", httpname,
             httpname, alias);
    /*First send httpname to server so it can open path*/
    if (send(sock, patch, strlen(patch), 0) < 0) {
      perror("send put error");
    }

    char header[4096];
    if (recv(sock, header, sizeof(header), 0) < 0) {
      perror("receive error");
      return;
    }
    char *response = strtok(header, "\0");
    response = strtok(response, " ");
    char *code = strtok(NULL, " ");
    /*if any bad status codes, don't do any writing*/
    if (strcmp(code, "404") == 0 || strcmp(code, "403") == 0 ||
        strcmp(code, "500") == 0 || strcmp(code, "400") == 0) {
      fprintf(stderr, "Error token: %s %s\n", code, strerror(errno));
      return;
    }
    return;
  }
}
int main(int argc, char **argv) {
  if (argc < 2) {
    perror("not enough files");
    return 0;
  }

  /*parsing the command line for socket set up*/
  char *address = strtok(argv[1], ":");
  char *hostname = address;
  address = strtok(NULL, ":");
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

  for (int i = 2; i < argc; i++) {
    int sock;
    /*creates the socket*/
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      perror("sock error client");
      return 0;
    }

    /*connects the socket regerred to by the descript to address*/
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      perror("connection failed");
      return 0;
    }
    client_command(i, argv, sock);
    close(sock);
  }
  return 0;
}
