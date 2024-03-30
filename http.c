#include <stddef.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <pthread.h>
#include <regex.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUFFER_SIZE 104857600 // size of 1MB
#define PORT 8080

struct client_info
{
    int client_fd;
    struct sockaddr_in client_addr;
};

char* url_decode(const char* encoded)
{
	size_t encoded_len = strlen(encoded);
	char* decoded = malloc(encoded_len + 1);
	size_t decoded_len = 0;

	// decode to hex
	for (size_t i = 0; i < encoded_len; i++)
	{
		if (encoded[i] == '%' && i + 2 < encoded_len)
		{
			int hex_val;

			sscanf(encoded + i + 1, "%2x", &hex_val);
			decoded[decoded_len++] = hex_val;
			i += 2;
		}
		else
		{
			decoded[decoded_len++] = encoded[i];
		}
	}
	decoded[decoded_len] = '\0';

	return decoded;
}

const char* get_file_ext(const char* filename)
{
	const char* dot = strrchr(filename, '.');

	if (!dot || dot == filename)
		return "";

	return dot + 1;
}

const char* get_mime_type(const char* file_ext)
{
	if (strcasecmp(file_ext, "html") == 0 || strcasecmp(file_ext, "htm") == 0)
        return "text/html";
    else if (strcasecmp(file_ext, "css") == 0)
        return "text/css";
	else if (strcasecmp(file_ext, "txt") == 0)
        return "text/plain";
	else if (strcasecmp(file_ext, "jpg") == 0 || strcasecmp(file_ext, "jpeg") == 0)
        return "image/jpeg";
	else if (strcasecmp(file_ext, "png") == 0)
        return "image/png";
    else if (strcasecmp(file_ext, "pdf") == 0)
        return "application/pdf";
	else
        return "application/octet-stream"; // can't determine MIME type
}

void http_response(const char* filename, const char* file_ext, char* response, size_t* response_len)
{
	// HTTP header
	const char* mime_type = get_mime_type(file_ext);
	char* header = (char*) malloc(BUFFER_SIZE * sizeof(char));
	
	snprintf(header,
			 BUFFER_SIZE, 
			 "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "\r\n",
			 mime_type);

	// 404 response
	int file_fd = open(filename, O_RDONLY);

	if (file_fd == -1)
	{
		snprintf(response,
				 BUFFER_SIZE,
				 "HTTP/1.1 404 Not Found\r\n"
                 "Content-Type: text/html\r\n"
                 "\r\n"
                 "<!DOCTYPE html>\n"
                 "<html>\n"
                 "<head>\n"
                 "<title>404 Not Found</title>\n"
                 "<style>\n"
                 ".container {height: 100vh; font-family: 'Montserrat', 'sans-serif'; font-weight: bolder; display: flex; justify-content: center; align-items: center; flex-direction: column;\n"
                 "</style>\n"
                 "</head>\n"
                 "<body>\n"
                 "<div class='container'>\n"
                 "<h1>An error as occured.</h1>\n"
                 "<h2>404 Not Found</h2>\n"
                 "</div>\n"
                 "</body>\n"
                 "</html>\n");

		*response_len = strlen(response);

		return;
	}

	// get file size for Content-Length
	struct stat file_stat;
    fstat(file_fd, &file_stat);
    off_t file_size = file_stat.st_size;

	// copy header to response
    *response_len = 0;
    memcpy(response, header, strlen(header));
    *response_len += strlen(header);

	// copy file to response
	ssize_t bytes_read;

	while ((bytes_read = read(file_fd, response + *response_len, BUFFER_SIZE - *response_len)) > 0)
	{
        *response_len += bytes_read;
    }

    free(header);
    close(file_fd);
}

void* handle_client(void* args)
{   
    struct client_info* ci = (struct client_info*)args;
	int client_fd = ci->client_fd;
    struct sockaddr_in client_addr = ci->client_addr;
	char* buffer = (char*)malloc(BUFFER_SIZE * sizeof(char));

    free(args);

	ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0);

	if (bytes_received > 0)
	{
		// check if request is GET
		regex_t regex;
		regcomp(&regex, "^GET /([^ ]*) HTTP/1", REG_EXTENDED); // capture HTTP GET request
		regmatch_t matches[2];

		if (regexec(&regex, buffer, 2, matches, 0) == 0)
		{
			// extract filename from request and decode URL
			buffer[matches[1].rm_eo] = '\0';
			
			const char* encoded_filename_url = buffer + matches[1].rm_so;
			char* filename = url_decode(encoded_filename_url);

            printf("[PENDING] Client %s:%d request: %s\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), filename);

			// get file extension
			char file_ext[32];
			strcpy(file_ext, get_file_ext(filename));

			// HTTP response
			char* response = (char*) malloc(BUFFER_SIZE * 2 * sizeof(char));
			size_t response_len;
			http_response(filename, file_ext, response, &response_len);

            if (strncmp(response, "HTTP/1.1 200 OK", 15) == 0)
                printf("[SUCCESS] HTTP status code: 200 OK\n\n");
            else
                printf("[ERROR] HTTP status code: 400 Not Found\n\n");

			send(client_fd, response, response_len, 0);

			free(response);
			free(filename);
		}
		
		regfree(&regex);
	}

	close(client_fd);
	// free(args);
	free(buffer);

	return NULL;
}

int main(int argc, char* argv[])
{
	int server_fd;
	struct sockaddr_in server_address;

	// create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
        perror("[ERROR] Socket creation failed!");
        exit(EXIT_FAILURE);
    }

	// socket configuration
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(PORT);

	// bind socket to port
    if (bind(server_fd, (struct sockaddr*)&server_address, sizeof(server_address)) < 0)
	{
        perror("[ERROR] Socket port binding failed!");
        exit(EXIT_FAILURE);
    }

	// listen for connections (10 connections limit)
    if (listen(server_fd, 10) < 0)
	{
        perror("[ERROR] Listening for connections failed!");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port 8080...\n");
	while (1)
	{
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		int* client_fd = malloc(sizeof(int));

		if ((*client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len)) < 0)
		{
			perror("[ERROR] Accept client failed!");
			continue;
		}

        printf("[SUCCESS] Connection accepted: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        struct client_info* ci = malloc(sizeof(struct client_info));

        ci->client_fd = *client_fd;
        ci->client_addr = client_addr;

		pthread_t thread_id;
		pthread_create(&thread_id, NULL, handle_client, (void*)ci);
		pthread_detach(thread_id);
	}

	close(server_fd);
	
	return 0;
}
