#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "helpers.h"
#include <netinet/tcp.h>
#include <iostream>
#include <string>
#include <queue>
#include <vector>
#include <unordered_map>

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
    int type;           // 1 for exit command 2 for actual message
} Message;

// Structure to keep information about a client
typedef struct {
    string user_id;             // theid of the user
    int fd;                     // the file descriptor of the user (-1 is not online)
    bool online;                // variable to keep track of the status of the user
    deque <Message> messages;   // the messages recieved while the client was offline
} client;

// Structure to keep information about a topic
typedef struct {
    string topic_name;                          // the name of the topic
    unordered_map<string, int> userid_to_sf;    // map with userid as key and sf status as value
} Topic;

// Function to show how the exectuable should get the arguments
void usage(char *file) {
	fprintf(stderr, "Usage: %s server_port\n", file);
	exit(0);
}

int main(int argc, char **argv) {
    int sockfd_tcp, sockfd_udp, newsockfd, portno;
	char buffer[BUFLEN], buffer_aux[BUFLEN];
    char topic[52];
    char message[1502];
    char buffer_metadata[30];
    uint8_t type;

	struct sockaddr_in serv_addr_tcp, serv_addr_udp, cli_addr;
	int n, ret;
    socklen_t len_udp = sizeof(serv_addr_udp);
	socklen_t clilen;

    // Deactivating print buffering
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    // maps used to store the information
    // map of connected users with socket file descriptor as key and userid as value
    unordered_map<int, string> sock_to_userid_map;
    // map storing all clients with userid as key and client data structure as value
    unordered_map<string, client> userid_to_client_map;
    // topic map with topic name as key and Topic data strcture as value
    unordered_map<string, Topic> topicname_to_topic_map;

    // Setting the file descriptor sets which will be used in I/O multiplexing
    fd_set read_fds;	// multimea de citire folosita in select()
	fd_set tmp_fds;		// multime folosita temporar
	int fdmax;			// valoare maxima fd din multimea read_fds

    // Initialising them with empty sets
	FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);

    // If the wrong number of arguments is provided, show how the program should be used
    if (argc < 2) {
        usage(argv[0]);
    }
    
    // Parsing the given port
    portno = atoi(argv[1]);
    DIE(portno == 0, "port");

    // Setting up a udp socket (the udp socket must be first or the first udp message won't
    // be recieved by the server)
    sockfd_udp = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(sockfd_udp < 0, "socket_udp");

    // Setting the udp socket address
    memset((char*) &serv_addr_udp, 0, sizeof(serv_addr_udp));
    serv_addr_udp.sin_family = AF_INET;
    serv_addr_udp.sin_port = htons(portno);
    serv_addr_udp.sin_addr.s_addr = INADDR_ANY;

    // Setting up a tcp socket
    sockfd_tcp = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd_tcp < 0, "socket_tcp");

    // Setting the tcp socket address
    memset((char*) &serv_addr_tcp, 0, sizeof(serv_addr_tcp));
    serv_addr_tcp.sin_family = AF_INET;
    serv_addr_tcp.sin_port = htons(portno);
    serv_addr_tcp.sin_addr.s_addr = INADDR_ANY;

    // Freeing the sockets in case they are already in use (so that when the checker
    // runs using the 12345 port, there won't be any issues)
    int enable_tcp = 1;
    int enable_udp = 1;
    if (setsockopt(sockfd_tcp, SOL_SOCKET, SO_REUSEADDR, &enable_tcp, sizeof(int)) == -1 ||
        setsockopt(sockfd_udp, SOL_SOCKET, SO_REUSEADDR, &enable_udp, sizeof(int)) == -1) {
        perror("setsocketopt");
        exit(1);
    }

    // Binding the tcp and udp sockets and setting up the tcp socket to listed (to
    // be able to accept further tcp clients)
    ret = bind(sockfd_udp, (struct sockaddr *) &serv_addr_udp, sizeof(struct sockaddr));
    DIE(ret < 0, "bind");

    ret = bind(sockfd_tcp, (struct sockaddr *) &serv_addr_tcp, sizeof(struct sockaddr));
    DIE(ret < 0, "bind");

    ret = listen(sockfd_tcp, MAX_CLIENTS);
    DIE(ret < 0, "listen");

    // Adding STDIN and the 2 sockets, the udp and tcp sockets to the reading file
    // descriptors set
    FD_SET(STDIN_FILENO, &read_fds);
    FD_SET(sockfd_tcp, &read_fds);
    FD_SET(sockfd_udp, &read_fds);
    // Setting the tcp socket as the greatest one (since the tcp socket is the last
    // one initialized)
    fdmax = sockfd_tcp;

    // The event loop
    while(true) {
        // Taking the temporary file descriptors
        tmp_fds = read_fds;

        // Selecting the file descriptors available to read
        ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
        DIE(ret < 0, "select\n");

        // Iterating through all filedescriptors
        for (int i = 0; i <= fdmax; i++) {
            // If we found a file descriptor in our set
            if (FD_ISSET(i, &tmp_fds)) {
                // Reading from STDIN
                if (i == 0) {
                    // 0-ing the buffer and reading line by line after cutting the
                    // \n out from the buffer
                    memset(buffer, 0, BUFLEN);
                    fgets(buffer, BUFLEN, stdin);
                    buffer[strlen(buffer) - 1] = '\0';

                    // If the "exit" command is issued
                    if (strcmp(buffer, "exit") == 0) {
                        // Send an "exit" command to each connected client and close
                        // it's socket
                        for (auto& x : sock_to_userid_map) {
                            Message current;
                            strcpy(current.message, "exit");
                            current.type = 1;
                            
                            int remaining_bytes = sizeof(Message);
                            int sent_bytes = 0;
                            // In case the number of sent bytes isn't the one we expect
                            while (sent_bytes != sizeof(Message)) {
                                n = send(x.first, &current + sent_bytes, remaining_bytes, 0);
                                remaining_bytes -= n;
                                sent_bytes += n;
                                DIE(n < 0, "send");
                            }
                            close(x.first);
                        }
                        // Close the tcp and udp sockets
                        close(sockfd_tcp);
                        close(sockfd_udp);
                        return 0;
                    }

                    // "Bonus" helper functionality that prints all users in the server
                    // connected or not
                    if (strcmp(buffer, "print") == 0) {
                        for (auto& x : userid_to_client_map) {
                            cout << x.first << " " << x.second.fd << " " << x.second.online << " " << x.second.messages.size() << endl;
                        }
                    }

                    // "Bonus" helper function that prints all topics with all users
                    // subscribed to each topic with the sf type of subscription
                    if (strcmp(buffer, "topics") == 0) {
                        for (auto& x : topicname_to_topic_map) {
                            cout << "Topic " << x.first << " with the following subscribers:" << endl;
                            for (auto& y : x.second.userid_to_sf) {
                                cout << y.first << " " << y.second << endl;
                            }
                            cout << endl;
                        }
                    }
                }
                // UDP message
                else if (i == sockfd_udp) {
                    // Initialising the buffer
                    memset(buffer, 0, BUFLEN);
                    // Reading a message from the UDP port
                    n = recvfrom(sockfd_udp, buffer, BUFLEN, 0, (struct sockaddr *)&serv_addr_udp, &len_udp);
                    DIE(n < 0, "udp recieve\n");

                    // Setting the topic variable to the topic of the message
                    memset(topic, 0, 52);
                    memcpy(topic, buffer, 50);

                    // Setting the metadata of the message, required in the client
                    sprintf(buffer_metadata, "%s:%d", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

                    // Iterating through the entries in the userif_to_sf map, which contains
                    // information about the users subscribed to the given topic, map
                    // contained in the Topic found by topic key in the topic name to Topic map
                    for (auto& x : topicname_to_topic_map[topic].userid_to_sf) {
                        // Extracting as pointer since we may need to modify the entry (the client)
                        client *current = &userid_to_client_map[x.first];

                        // If the client is online, populate the metadata and the actual content of
                        // the message that is now being sent
                        if (current->online) {
                            Message current_message;
                            memcpy(current_message.message, buffer, 1600);
                            memcpy(current_message.metadata, buffer_metadata, 30);
                            current_message.type = 2;
                            int remaining_bytes = sizeof(Message);
                            int sent_bytes = 0;
                            // In case the number of sent bytes isn't the one we expect
                            while (sent_bytes != sizeof(Message)) {
                                n = send(current->fd, &current_message + sent_bytes, remaining_bytes, 0);
                                remaining_bytes -= n;
                                sent_bytes += n;
                                DIE(n < 0, "send");
                            }
                        }
                        // Otherwise, if the client isn't online but is subscribed as store and
                        // forward to this topic simply add the message to the back of the
                        // message storing deque of the client
                        else {
                            if (x.second == 1) {
                                Message current_message;
                                memcpy(current_message.message, buffer, 1600);
                                memcpy(current_message.metadata, buffer_metadata, 30);
                                current_message.type == 2;
                                current->messages.push_back(current_message);
                            }
                        }
                    }
                }
                // TCP connection
                else if (i == sockfd_tcp) {
                    // Setting the length of the incomming transmition
                    clilen = sizeof(cli_addr);
                    // Accepting a new connection from the TCP socket
					newsockfd = accept(sockfd_tcp, (struct sockaddr *) &cli_addr, &clilen);
					DIE(newsockfd < 0, "tcp accept\n");

                    // Adding the new file descriptor to the set and if it is bigger than the last
                    // biggest file descriptor, update it
                    FD_SET(newsockfd, &read_fds);
					if (newsockfd > fdmax) { 
						fdmax = newsockfd;
					}

                    // Deactivating Neagle's Algorithm
                    int enable = 1;
                    setsockopt(newsockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&enable, sizeof(int));

                    // Receiving the userid of the newly connected client (in order to bypass message
                    // concatenation issues, the server reads the exactly needed number of bytes, which
                    // if the size of Message structure)
                    n = recv(newsockfd, buffer, sizeof(Message), 0);
                    DIE(n < 0, "tcp accept name\n");
                    
                    // Folding the buffer into a Message so that we can access the information we need,
                    // the metadata and the actual content
                    Message *current_message = (Message *) buffer;

                    // If we have previously had a client with the given userid we have 2 cases,
                    // there either is one with the id already connected, which results in error
                    // for the client attempting to connect, or the client isn't already connected,
                    // which means that the client attempting to connect is actually reconnecting
                    if (userid_to_client_map.find(current_message->message) != userid_to_client_map.end()) {
                        // If there is an online client with the given userid
                        if (userid_to_client_map[current_message->message].online) {
                            // Print an error message
                            printf("Client %s already connected.\n", current_message->message);

                            // Send an exit message to the client
                            Message current;
                            strcpy(current.message, "exit");
                            current.type = 1;
                            int remaining_bytes = sizeof(Message);
                            int sent_bytes = 0;
                            // In case the number of sent bytes isn't the one we expect
                            while (sent_bytes != sizeof(Message)) {
                                n = send(newsockfd, &current + sent_bytes, remaining_bytes, 0);
                                remaining_bytes -= n;
                                sent_bytes += n;
                                DIE(n < 0, "send");
                            }

                            // Close the connection and remove the socket from the file descriptor set
                            close(newsockfd);
                            FD_CLR(newsockfd, &read_fds);
                        }
                        // The case of reconecction
                        else {
                            // Link the userid to the socket and extract the client as pointer since
                            // modifications are required
                            sock_to_userid_map[newsockfd] = current_message->message;
                            client *current = &userid_to_client_map[current_message->message];

                            // Set the client's status to online and it's file descriptor to the one
                            // just requested
                            current->online = true;
                            current->fd = newsockfd;
                            
                            // Print the connection message
                            cout << "New client " << current->user_id << " connected from " << inet_ntoa(cli_addr.sin_addr) << ":" << ntohs(cli_addr.sin_port) << "." << endl;

                            // Send the stored messages to the client (iterate through the deque, at
                            // each step pop a message from the front, the most recent ones and send it)
                            int size = current->messages.size();
                            for (int i = 0; i < size; i++) {
                                Message current_message = current->messages.front();
                                current->messages.pop_front();
                                int remaining_bytes = sizeof(Message);
                                int sent_bytes = 0;
                                // In case the number of sent bytes isn't the one we expect
                                while (sent_bytes != sizeof(Message)) {
                                    n = send(current->fd, &current_message + sent_bytes, remaining_bytes, 0);
                                    remaining_bytes -= n;
                                    sent_bytes += n;
                                    DIE(n < 0, "send");
                                }
                            }
                        }
                    }
                    // If we have a new client, populate it's fields and include it in the maps, the new
                    // socket as key and the userid as value, and the userid as key and the client as value
                    else {
                        // Populate
                        client current;
                        current.fd = newsockfd;
                        current.user_id = buffer;
                        current.online = true;

                        // Add to maps
                        sock_to_userid_map[newsockfd] = current.user_id;
                        userid_to_client_map[current.user_id] = current;

                        // Print the connection message
                        cout << "New client " << current.user_id << " connected from " << inet_ntoa(cli_addr.sin_addr) << ":" << ntohs(cli_addr.sin_port) << "." << endl;
                    }
                }
                // Messages recieved on the TCP socket
                else {
                    // Inititalising the reading buffer
                    memset(buffer, 0, BUFLEN);
                    // Read exactly the size of one message from the TCP flux of data
                    int remaining_bytes = sizeof(subscribe_command);
                    int recieved_bytes = 0;
                    // In case the number of recieved bytes isn't the one we expect
                    while (recieved_bytes != sizeof(subscribe_command)) {
                        n = recv(i, buffer + recieved_bytes, remaining_bytes, 0);
                        recieved_bytes += n;
                        remaining_bytes -= n;
                        // If the number of bytes is 0, disconnect
                        if (n == 0) {
                            // Printing the disconnect message
                            cout << "Client " << sock_to_userid_map[i] << " disconnected." << endl;
                            // Set the client's online status and file descriptor to "offline"
                            userid_to_client_map[sock_to_userid_map[i]].online = false;
                            userid_to_client_map[sock_to_userid_map[i]].fd = -1;
                            
                            // Close the socket and remove it from both the socket file descriptor
                            // to userid map and the read file descriptors set
                            close(i);
                            sock_to_userid_map.erase(i);
                            FD_CLR(i, &read_fds);
                            break;
                        }
                        DIE(n < 0, "tcp recieve");
                    }

                    // If we have recieved something
                    if (recieved_bytes != 0) {
                        // We cast the message to a subscribe command to easily access the
                        // recieved information
                        subscribe_command *current = (subscribe_command *) buffer;
                        // If it's a subscribe command
                        if (current->type == 1) {
                            // If the topic already exists, add the client to the clientmap inside the topic
                            // with the transmitted store and forward variable
                            if (topicname_to_topic_map.find(current->topic) != topicname_to_topic_map.end()) {
                                topicname_to_topic_map[current->topic].userid_to_sf[sock_to_userid_map[i]] = current->sf;
                            }
                            // If the topic doesn't already exist, create a new one and add the client to
                            // the topic's users map with the given sf
                            else {
                                Topic new_topic;
                                new_topic.topic_name = current->topic;
                                new_topic.userid_to_sf[sock_to_userid_map[i]] = current->sf;
                                topicname_to_topic_map[current->topic] = new_topic;
                            }
                        }
                        // If it's an unsubscribe command
                        else if (current->type == 2) {
                            // If the topic already exists, erase the client's entry from the subscriptions
                            // map inside the topic
                            if (topicname_to_topic_map.find(current->topic) != topicname_to_topic_map.end()) {
                                topicname_to_topic_map[current->topic].userid_to_sf.erase(sock_to_userid_map[i]);
                            }
                            // Otherwise, print an error
                            else {
                                printf("Unsubscribe from an inexistent topic.\n");
                                continue;
                            }
                        }
                        // In case there is another kind of message (some error of sorts) print it to STDOUT
                        else {
                            cout << "S-a primit de la clientul " << sock_to_userid_map[i] << " mesajul " << buffer << endl;
                        }
                    }
                }
            }
        }
    }

    return 0;
}