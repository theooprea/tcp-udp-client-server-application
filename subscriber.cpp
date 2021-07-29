#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "helpers.h"
#include <netinet/tcp.h>
#include <iostream>
#include <string>

using namespace std;

// Structure to keep the information about a subscribe / unsubscribe command
// that will be sent to the server
typedef struct {
    int type;           // 1 for subscribe 2 for unsubscribe
    char topic[52];     // the topic for which the modification will be made
    int sf;             // variable to determine the type of subscription made
} subscribe_command;

// Structure to keep the information recieved from the server (single message)
typedef struct {
    char message[1600];
    char metadata[30];
    int type;
} Message;

// Function to show how the exectuable should get the arguments
void usage(char *file) {
	fprintf(stderr, "Usage: %s server_address server_port\n", file);
	exit(0);
}

int main(int argc, char **argv) {
    int sockfd, n, ret;
	struct sockaddr_in serv_addr;
	char buffer_stdin[BUFLEN], buffer_server[BUFLEN], buffer_server_metadata[30], buffer_aux[BUFLEN];
    char topic[52];
    char message[1502];
    uint8_t type;

    // Deactivating print buffering
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    // Setting the file descriptor sets which will be used in I/O multiplexing
    fd_set read_fds;
    fd_set tmp_read_fds;

    // Initialising them with empty sets
    FD_ZERO(&read_fds);
	FD_ZERO(&tmp_read_fds);

    // If the wrong number of arguments is provided, show how the program should be used
    if (argc < 4) {
		usage(argv[0]);
	}

    // Open a TCP (Stream) socket for communicating with the server
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "socket");

    // Prepare the connection on the given port and ip address
    serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(atoi(argv[3]));
	ret = inet_aton(argv[2], &serv_addr.sin_addr);
	DIE(ret == 0, "inet_aton");

    // Deactivating Neagle's Algorithm
    int enable = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&enable, sizeof(int));

    // Connecting to the created socket
    ret = connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr));
	DIE(ret < 0, "connect");

    // Sending a message containing the User ID of the client to the server
    Message mes;
    strcpy(mes.message, argv[1]);
    ret = send(sockfd, &mes, sizeof(Message), 0);

    // Inserting STDIN and the tcp socket in the set of reading file descriptors
    FD_SET(STDIN_FILENO, &read_fds);
	FD_SET(sockfd, &read_fds);

    // Getting the biggest file descriptor
	int max_fd = STDIN_FILENO > sockfd ? STDIN_FILENO + 1 : sockfd + 1;

    // The client's event loop
    while (true) {
        // Using a temporary file descriptor set
        tmp_read_fds = read_fds;

        // Selecting the ready to read / write file descriptors
        int rc = select(max_fd, &tmp_read_fds, NULL, NULL, NULL);
        DIE(rc < 0, "select");

        // If STDIN is available for reading
        if (FD_ISSET(STDIN_FILENO, &tmp_read_fds)) {
            // Reading a line from stdin
            memset(buffer_stdin, 0, BUFLEN);
			fgets(buffer_stdin, BUFLEN, stdin);
            buffer_stdin[strlen(buffer_stdin) - 1] = '\0';

            // If the exit command is issued, close the client
            if (strcmp(buffer_stdin, "exit") == 0) {
                break;
            }

            // Use an auxiliary buffer to be able to parse it and extract the
            // command, the topic and skip and forward variable
            strcpy(buffer_aux, buffer_stdin);
            // If the command is a subscription one
            if (strncmp(buffer_aux, "subscribe", 9) == 0) {
                char *command = strtok(buffer_aux, " ");
                char *topic = strtok(NULL, " ");
                char *sf = strtok(NULL, " ");
                
                // If the command is misused
                if (topic == NULL || sf == NULL) {
                    printf("Command not allowed\n");
                    continue;
                }

                // Create and populate a subscribe_command message
                subscribe_command current;
                current.type = 1;
                strcpy(current.topic, topic);
                current.sf = atoi(sf);

                // Send only one subscribe_command message and exit if it fails
                int remaining_bytes = sizeof(subscribe_command);
                int sent_bytes = 0;
                // In case the number of sent bytes isn't the one we expect
                while (sent_bytes != sizeof(subscribe_command)) {
                    n = send(sockfd, &current + sent_bytes, remaining_bytes, 0);
                    remaining_bytes -= n;
                    sent_bytes += n;
                    DIE(n < 0, "send");
                }
                
                // Print the fact that the client is subscribed
                printf("Subscribed to topic.\n");
            }
            // If it is an unsubscription command
            else if (strncmp(buffer_aux, "unsubscribe", 11) == 0) {
                char *command = strtok(buffer_aux, " ");
                char *topic = strtok(NULL, " ");

                // If the command is misused
                if (topic == NULL) {
                    printf("Command not allowed\n");
                    continue;
                }

                // Create and populate a subscribe_command message
                subscribe_command current;
                current.type = 2;
                strcpy(current.topic, topic);
                
                // Send only one subscribe_command message and exit if it fails
                int remaining_bytes = sizeof(subscribe_command);
                int sent_bytes = 0;
                // In case the number of sent bytes isn't the one we expect
                while (sent_bytes != sizeof(subscribe_command)) {
                    n = send(sockfd, &current + sent_bytes, remaining_bytes, 0);
                    remaining_bytes -= n;
                    sent_bytes += n;
                    DIE(n < 0, "send");
                }

                // Print the fact that the client is subscribed
                printf("Unsubscribed from topic.\n");
            }
        }
        // If the server TCP socket is available to read
        if (FD_ISSET(sockfd, &tmp_read_fds)) {
            // Initialize the buffer
            memset(buffer_server, 0, BUFLEN);

            // Take from the TCP stream only sizeof(Message) bytes
            int remaining_bytes = sizeof(Message);
            int recieved_bytes = 0;
            // In case the number of recieved bytes isn't the one we expect
            while (recieved_bytes != sizeof(Message)) {
                n = recv(sockfd, buffer_server + recieved_bytes, remaining_bytes, 0);
                remaining_bytes -= n;
                recieved_bytes += n;
                if (n == 0) {
                    break;
                }
                DIE(n < 0, "send");
            }

            // If there are no bytes recieved, the server is closed and the client
            // can be also closed
            if (recieved_bytes == 0) {
                break;
            }

            // "Fold" the buffer into a Message * variable for further usage
            Message *current = (Message *) buffer_server;

            // If the message recieved is the exit command, exit the program
            if (current->type == 1) {
                break;
            }
            // Otherwise
            else {
                // Initialising the soon to be processed buffers
                memset(topic, 0, 52);
                memset(message, 0, 1502);

                // Extract the topic, the type of the message and the actual message from
                // the corresponding field in the Message structure
                memcpy(topic, current->message, 50);
                memcpy(&type, current->message + 50, 1);
                memcpy(message, current->message + 51, 1500);

                // If we have a INT message recieved
                if (type == 0) {
                    // Variables needed to process the data
                    uint8_t sign;
                    uint32_t value;

                    // Copying the bytes in the variables
                    memcpy(&sign, message, 1);
                    memcpy(&value, message + 1, 4);

                    // Depending on the sign variable, set the final value recieved
                    // considering it's endianess
                    if (sign == 1) {
                        value = - ntohl(value);
                    }
                    else {
                        value = ntohl(value);
                    }

                    // Printing the recieved data in an appropiate format
                    printf("%s - %s - INT - %d\n", current->metadata, current->message, value);
                }
                else if (type == 1) {
                    // Variables needed to process the data
                    uint16_t value;
                    float actual_value;

                    // Copying the bytes in the variables and adjusing the floating
                    // point by dividing to 100
                    memcpy(&value, message, sizeof(uint16_t));
                    actual_value = (float)ntohs(value) / 100;

                    // Printing the recieved data in an appropiate format
                    printf("%s - %s - SHORT_REAL - %.2f\n", current->metadata, current->message, actual_value);
                }
                else if (type == 2) {
                    uint8_t sign;
                    uint32_t value;
                    uint8_t power_negative_ten;
                    float actual_value;

                    memcpy(&sign, message, 1);
                    memcpy(&value, message + 1, 4);
                    memcpy(&power_negative_ten, message + 5, 1);

                    actual_value = ntohl(value);

                    if (sign == 1) {
                        actual_value = - actual_value;
                    }

                    for (int j = 0; j < power_negative_ten; j++) {
                        actual_value /= 10.00;
                    }

                    // Printing the recieved data in an appropiate format
                    printf("%s - %s - FLOAT - %.*f\n", current->metadata, current->message, power_negative_ten, actual_value);
                }
                else if (type == 3) {
                    // Printing the recieved data in an appropiate format
                    printf("%s - %s - STRING - %s\n", current->metadata, current->message, message);
                }
                else {
                    // In case the server transmitted some other kind of message, print
                    // it and the metadata of the message
                    printf("%s - %s\n", current->metadata, current->message);
                }
            }
        }
    }
    // Closing the socket and exiting the program
    close(sockfd);
    return 0;
}