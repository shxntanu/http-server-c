#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ctype.h>
#include <getopt.h>

char *parse_args(int argc, char *argv[])
{
    /**
     * getopt_long is used to parse the command-line options.
     * The long_options array specifies the long options that the program accepts.
     *
     * Each element of the array is a struct option that specifies the name of the long option,
     * whether it requires an argument, the address of a variable that getopt_long should set to
     * indicate that this option was seen (we pass NULL because we don't need this), and the value
     * to return when this option is seen (we use 'd' to match the case in the switch statement).
     */

    struct option long_options[] = {
        {"directory", required_argument, NULL, 'd'},
        {NULL, 0, NULL, 0}};

    char *directory = NULL;
    int opt;

    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1)
    {
        switch (opt)
        {
        case 'd':
            directory = optarg;
            break;
        default:
            fprintf(stderr, "Usage: %s --directory <directory>\n", argv[0]);
        }
    }

    if (directory == NULL)
    {
        fprintf(stderr, "You must specify a directory with the --directory option.\n");
        return NULL;
    }
    return directory;
}

int main(int argc, char *argv[])
{
    // Disable output buffering
    setbuf(stdout, NULL);
    printf("Logs from your program will appear here!\n");

    char *directory = parse_args(argc, argv);

    int server_fd, client_fd;
    socklen_t client_addr_len;
    struct sockaddr_in client_addr;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1)
    {
        printf("Socket creation failed: %s...\n", strerror(errno));
        return 1;
    }

    // Since the tester restarts your program quite often, setting REUSE_PORT
    // ensures that we don't run into 'Address already in use' errors
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0)
    {
        printf("SO_REUSEPORT failed: %s \n", strerror(errno));
        return 1;
    }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(4221),
        .sin_addr = {htonl(INADDR_ANY)},
    };

    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0)
    {
        printf("Bind failed: %s \n", strerror(errno));
        return 1;
    }
    int connection_backlog = 5;

    if (listen(server_fd, connection_backlog) != 0)
    {
        printf("Listen failed: %s \n", strerror(errno));
        return 1;
    }

    printf("Waiting for a client to connect...\n");

    while (1)
    {
        client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0)
        {
            printf("Accept failed: %s \n", strerror(errno));
            continue;
        }

        int pid = fork();
        if (pid < 0)
        {
            printf("Fork failed: %s\n", strerror(errno));
            close(client_fd);
        }
        else if (pid == 0)
        {
            close(server_fd);
            printf("Client connected\n");

            char request[1024];
            char buff[1024];

            if (recv(client_fd, request, sizeof(request), 0) == -1)
            {
                printf("Receive failed: %s \n", strerror(errno));
            }
            strcpy(buff, request);
            printf("Received Message: %s", buff);

            // Stores request method (e.g. GET, PUT, POST)
            char *request_method = strtok(buff, " ");
            // Stores request path (e.g. "/", "/echo/abc")
            char *request_path = strtok(NULL, " ");

            char response[1024];

            printf("Request path: %s\n", request_path);

            if (strcmp(request_path, "/") == 0)
            {
                char *message = "Ok";
                sprintf(response,
                        "HTTP/1.1 200 Ok\r\nContent-Type: text/plain\r\nContent-Length: "
                        "%ld\r\n\r\n%s",
                        strlen(message), message);
            }
            else if (strncmp(request_path, "/echo/", strlen("/echo/")) == 0)
            {
                char *message = request_path + 6;
                sprintf(response,
                        "HTTP/1.1 200 Ok\r\nContent-Type: text/plain\r\nContent-Length: "
                        "%ld\r\n\r\n%s",
                        strlen(message), message);
            }
            else if (strncmp(request_path, "/files/", strlen("/files/")) == 0)
            {
                if (strcmp(request_method, "GET") == 0)
                {
                    char *filename = request_path + 7;
                    char filepath[1024];
                    sprintf(filepath, "%s/%s", directory, filename);
                    FILE *file = fopen(filepath, "r");
                    if (file == NULL)
                    {
                        char *message = "Not Found";
                        sprintf(response,
                                "HTTP/1.1 404 Not Found\r\nContent-Type: "
                                "text/plain\r\nContent-Length: %ld\r\n\r\n%s",
                                strlen(message), message);
                    }
                    else
                    {
                        char file_content[1024];
                        size_t n = fread(file_content, 1, sizeof(file_content) - 1, file);
                        file_content[n] = '\0';
                        fclose(file);

                        sprintf(response,
                                "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: "
                                "%ld\r\n\r\n%s",
                                strlen(file_content), file_content);
                    }
                }
                else if (strcmp(request_method, "POST") == 0)
                {
                    char *filename = request_path + 7;
                    char *body = strstr(request, "\r\n\r\n");
                    body += 4;
                    char filepath[1024];
                    sprintf(filepath, "%s/%s", directory, filename);
                    FILE *file = fopen(filepath, "w");
                    if (file == NULL)
                    {
                        char *message = "Failed to open file for writing";
                        sprintf(response,
                                "HTTP/1.1 500 Internal Server Error\r\nContent-Type: "
                                "text/plain\r\nContent-Length: %ld\r\n\r\n%s",
                                strlen(message), message);
                    }
                    else
                    {
                        fprintf(file, "%s", body);
                        fclose(file);

                        char *message = "File created successfully";
                        sprintf(response,
                                "HTTP/1.1 201 Created\r\nContent-Type: text/plain\r\nContent-Length: "
                                "%ld\r\n\r\n%s",
                                strlen(message), message);
                    }
                }
            }
            else if (strncmp(request_path, "/user-agent", strlen("/user-agent")) == 0)
            {
                /**
                 * strtok uses a static buffer while parsing, which means if we use strtok in a nested manner
                 * to get the headers of the request, then it parses one header and then stops.
                 *
                 * A safer approach would be to first tokenize the entire request into lines,
                 * store those lines, and then parse each line separately for header name and value.
                 */

                char *lines[1024];
                int line_count = 0;
                char *line = strtok(request, "\r\n");
                while (line)
                {
                    lines[line_count++] = strdup(line);
                    line = strtok(NULL, "\r\n");
                }

                char *user_agent = NULL;
                for (int i = 0; i < line_count; i++)
                {
                    char *header_name = strtok(lines[i], ":");
                    char *header_value = strtok(NULL, "\r\n");
                    printf("Header Name: %s\n", header_name);
                    printf("Header Val: %s\n", header_value);

                    if (header_name && header_value)
                    {
                        // Remove leading and trailing whitespace from header_value
                        while (isspace((unsigned char)*header_value))
                            header_value++;
                        char *end = header_value + strlen(header_value) - 1;
                        while (end > header_value && isspace((unsigned char)*end))
                            end--;
                        *(end + 1) = 0;

                        if (strcmp(header_name, "User-Agent") == 0)
                        {
                            user_agent = strdup(header_value);
                            break;
                        }
                    }
                    free(lines[i]);
                }

                if (user_agent != NULL)
                {
                    sprintf(response,
                            "HTTP/1.1 200 Ok\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n%s",
                            strlen(user_agent),
                            user_agent);
                    free(user_agent);
                }
            }
            else
            {
                char *message = "Not Found";
                sprintf(response,
                        "HTTP/1.1 404 Not Found\r\nContent-Type: "
                        "text/plain\r\nContent-Length: %ld\r\n\r\n%s",
                        strlen(message), message);
            }
            ssize_t bytes = send(client_fd, response, strlen(response), 0);
            if (bytes == -1)
            {
                printf("Send response failed: %s \n", strerror(errno));
                return 1;
            }
            close(client_fd);
            exit(0);
        }
        else
        {
            close(client_fd);
        }
    }
    return 0;
}