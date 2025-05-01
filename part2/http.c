#include "http.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUFSIZE 512

const char *get_mime_type(const char *file_extension) {
    if (strcmp(".txt", file_extension) == 0) {
        return "text/plain";
    } else if (strcmp(".html", file_extension) == 0) {
        return "text/html";
    } else if (strcmp(".jpg", file_extension) == 0) {
        return "image/jpeg";
    } else if (strcmp(".png", file_extension) == 0) {
        return "image/png";
    } else if (strcmp(".pdf", file_extension) == 0) {
        return "application/pdf";
    } else if (strcmp(".mp3", file_extension) == 0) {
        return "audio/mpeg";
    }

    return NULL;
}

int read_http_request(int fd, char *resource_name) {
    int fd_copy = dup(fd);
    if (fd_copy == -1) {
        perror("dup");
        return -1;
    }

    FILE *stream = fdopen(fd_copy, "r");
    if (!stream) {
        perror("fdopen");
        close(fd_copy);
        return -1;
    }

    // Disable buffering
    if (setvbuf(stream, NULL, _IONBF, 0) != 0) {
        perror("setvbuf");
        fclose(stream);
        return -1;
    }

    char line[BUFSIZE];
    if (fgets(line, sizeof(line), stream) == NULL) {
        perror("fgets (request line)");
        fclose(stream);
        return -1;
    }

    // Parse the request line: e.g., GET /quote.txt HTTP/1.1
    char method[8], path[BUFSIZE], version[16];
    if (sscanf(line, "%s %s %s", method, path, version) != 3) {
        fprintf(stderr, "Malformed request line: %s\n", line);
        fclose(stream);
        return -1;
    }

    if (strcmp(method, "GET") != 0) {
        fprintf(stderr, "Unsupported method: %s\n", method);
        fclose(stream);
        return -1;
    }

    // Copy the path to resource_name
    strncpy(resource_name, path, BUFSIZE - 1);
    resource_name[BUFSIZE - 1] = '\0';

    // Read and discard remaining headers until blank line
    while (fgets(line, sizeof(line), stream)) {
        if (strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0) {
            break;
        }
    }

    fclose(stream);
    return 0;
}

int write_http_response(int fd, const char *resource_path) {

    struct stat st;
    if (stat(resource_path, &st) == -1) {
        // File not found

        const char *response =
            "HTTP/1.0 404 Not Found\r\n"
            "Content-Length: 0\r\n"
            "\r\n";
        write(fd, response, strlen(response));
        return 0;
    }

    // Open the file
    int file_fd = open(resource_path, O_RDONLY);
    if (file_fd == -1) {
        perror("open");
        return -1;
    }

    // Get file size
    off_t file_size = st.st_size;

    // Determine MIME type
    const char *ext = strrchr(resource_path, '.');
    const char *ext_to_use = ext;
    if (!ext) {
        ext_to_use = "";
    }
    const char *mime = get_mime_type(ext_to_use);
    if (mime == NULL) {
        mime = "application/octet-stream";
    }

    // Write HTTP headers
    dprintf(fd,
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "\r\n",
            mime, file_size);

    // Send file content in chunks
    char buffer[BUFSIZE];
    ssize_t n;
    while ((n = read(file_fd, buffer, BUFSIZE)) > 0) {
        write(fd, buffer, n);
    }

    close(file_fd);
    return 0;
}

