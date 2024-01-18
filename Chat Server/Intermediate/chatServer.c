/* server process */

/* include the necessary header files */
#include<ctype.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<stdlib.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include<sys/select.h>

#include "protocol.h"
#include "libParseMessage.h"
#include "libMessageQueue.h"

/**
 * send a single message to client 
 * sockfd: the socket to read from
 * toClient: a buffer containing a null terminated string with length at most 
 * 	     MAX_MESSAGE_LEN-1 characters. We send the message with \n replacing \0
 * 	     for a mximmum message sent of length MAX_MESSAGE_LEN (including \n).
 * return 1, if we have successfully sent the message
 * return 2, if we could not write the message
 */
int sendMessage(int sfd, char *toClient){
	int len = strlen(toClient);
        if (toClient[len] == '\0') toClient[len] = '\n'; //change '\0' to '\n'
        len++;
        int numSend = send(sfd, toClient, len, 0);
        if (numSend != len) return (2);
        return (1);
}

/**
 * read a single message from the client. 
 * sockfd: the socket to read from
 * fromClient: a buffer of MAX_MESSAGE_LEN characters to place the resulting message
 *             the message is converted from newline to null terminated, 
 *             that is the trailing \n is replaced with \0
 * return 1, if we have received a newline terminated string
 * return 2, if the socket closed (read returned 0 characters)
 * return 3, if we have read more bytes than allowed for a message by the protocol
 */
int recvMessage(int sfd, char *fromClient){
	int numRecv = recv(sfd, fromClient, MAX_MESSAGE_LEN, 0);
        if (numRecv == MAX_MESSAGE_LEN) return (3);
        if (numRecv == 0) return (2);
        if (fromClient[numRecv - 1] == '\n') fromClient[numRecv - 1] = '\0';
        return (1);
}

int main (int argc, char ** argv) {
    	int sockfd;

    	if(argc!=2){
	    	fprintf(stderr, "Usage: %s portNumber\n", argv[0]);
	    	exit(1);
    	}
    	int port = atoi(argv[1]);

    	if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
        	perror ("socket call failed");
        	exit (1);
    	}

    	struct sockaddr_in server;
    	server.sin_family=AF_INET;          // IPv4 address
    	server.sin_addr.s_addr=INADDR_ANY;  // Allow use of any interface 
    	server.sin_port = htons(port);      // specify port

    	if (bind (sockfd, (struct sockaddr *) &server, sizeof(server)) == -1) {
        	perror ("bind call failed");
        	exit (1);
    	}

    	if (listen (sockfd, 5) == -1) {
        	perror ("listen call failed");
        	exit (1);
    	}
	// manages 32 fds (clients).
	char users[32][MAX_USER_LEN+1];
	MessageQueue queues[32];
        for (int i = 0; i < 32; i++) {
        	users[i][0] = '\0';
                initQueue(&queues[i]);
                // or queues[i] = initQueue(&queues[i]);
        }
	char toClients[32][MAX_MESSAGE_LEN];
	int fdlist[32];
	int fdcount = 0;
	fdlist[fdcount++]=sockfd;
	for (;;) {
		// START: prepare for the select call
		fd_set readfds, writefds;
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		// setup readfds, identifying the FDs we are interested in waking up and reading from
		int fdmax = 0;
		for (int i = 0; i < fdcount; i++) {
			if (fdlist[i] > 0) {
				FD_SET(fdlist[i], &readfds); // poke the fdlist[i] bit of readfds
				if (fdlist[i] > fdmax) {
					fdmax = fdlist[i];
				}
			}
		}
		struct timeval tv; // how long we should sleep in case nothing happens
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		// END: Prepare for select call
		int numfds1;
		int numfds2;
		if ((numfds1=select(fdmax+1, &readfds, NULL, NULL, &tv)) > 0) {
			// block!!
			for (int i = 0; i<fdcount;i++) {
				if (FD_ISSET(fdlist[i], &readfds) /*&& FD_ISSET(fdlist[i], &writefds)*/) {
					if (fdlist[i] == sockfd) {
						// accept a connection
						int newsockfd;
						if ((newsockfd = accept(sockfd, NULL, NULL)) == -1) {
							perror("accept call failed");
							continue;
						}
						if (fdcount == 32) {
							close(newsockfd);
						} else {
							fdlist[fdcount] = newsockfd;
							fdcount++;
						}
					} else { //read from existing client: guaranteed 1 non-blocking read
						// code for reading
						char fromClient[MAX_MESSAGE_LEN];
                                		int retVal=recvMessage(fdlist[i], fromClient);
                                		if(retVal==1){
							FD_SET(fdlist[i], &writefds);
                                        		// we have a null terminated string from the client
                                        		char *part[4];
                                        		int numParts=parseMessage(fromClient, part);
                                        		if(numParts==0){
                                                		strcpy(toClients[i],"ERROR");
                                        		} else if(strcmp(part[0], "list") == 0) {
								strcpy(toClients[i], "");
								int v = 1;
								strcat(toClients[i], "users: ");
								strcat(toClients[i], users[v]);
								v++;
								while (v < fdcount && v < 11) {
									strcat(toClients[i], ", ");
									strcat(toClients[i], users[v]);
									v++;
								}
                                        		} else if(strcmp(part[0], "message")==0){
                                               			char *fromUser=part[1];
                                               			char *toUser=part[2];
                                               			char *message=part[3];
								if (users[i][0] == '\0') { // cant send if not registered yet :)
									strcpy(toClients[i], "ERROR");
									continue;
								}
                                               			if(strcmp(fromUser, users[i])!=0){
                                                       			sprintf(toClients[i], "invalidFromUser:%s",fromUser);
                                               			} else {
									int sent = 0;
									for (int k = 1; k <fdcount; k++) {
										if (strcmp(users[k], toUser) == 0) {
											sprintf(toClients[i], "%s:%s:%s:%s","message", fromUser, toUser, message);
                                                                               		if(enqueue(&queues[k], toClients[i])){
                                                                                       		strcpy(toClients[i], "messageQueued");
                                                                               		}else{
                                                                                       		strcpy(toClients[i], "messageNotQueued");
                                                                               		}
											sent = 1;
										}
									}
									if (sent == 0) {
										sprintf(toClients[i], "invalidToUser:%s",toUser);
									}
                                                		}
                                        		} else if(strcmp(part[0], "quit")==0){
                                                		strcpy(toClients[i], "closing");
                                       			} else if(strcmp(part[0], "getMessage")==0){
                                               			if(dequeue(&queues[i], toClients[i])){
									continue;
                                               			} else {
                                                       			strcpy(toClients[i], "noMessage");
                                               			}
                                       			} else if(strcmp(part[0], "register")==0){
								if (users[i][0] != '\0') {
                                                			strcpy(toClients[i],"ERROR");
                                                			continue;
                                        			}
								int userNotExist = 1;
                                                                for (int k = 0; k < fdcount; k++) {
                                                                        if (strncmp(part[1], users[k], MAX_USER_LEN) == 0) {
                                                                                userNotExist = 0;
                                                                                break;
                                                                        }
                                                                }
                                                                if (userNotExist) {
                                                                        strcpy(users[i], part[1]);
                                                                        strcpy(toClients[i], "registered");
                                                                } else {
                                                                        strcpy(toClients[i], "userAlreadyRegistered");
                                                                }
                                       			}
                               			} else {
                                       			close(fdlist[i]);
                               			}
					}
				}
			}
		}
		if ((numfds2 = select(fdmax+1, NULL, &writefds, NULL, &tv)) > 0) {
			for (int i=0; i<fdcount; i++) {
				if(FD_ISSET(fdlist[i], &writefds)) {
					if (fdlist[i] == sockfd) {
						int newsockfd;
						if ((newsockfd = accept(sockfd, NULL, NULL)) == 0) {
							perror("accept call failed");
							continue;
						}
						if (fdcount == 32) {
							close(newsockfd);
						} else {
							fdlist[fdcount] = newsockfd;
							fdcount++;
						}
					} else {
						if (strcmp(toClients[i], "closing") == 0) {
							sendMessage(fdlist[i], toClients[i]);
							close(fdlist[i]);
							fdlist[i] = -1;
							users[i][0] = '\0';
						} else {
							sendMessage(fdlist[i], toClients[i]);
						}
					}
					FD_CLR(fdlist[i], &writefds);
				}
			}
		}
	}
	exit(0);
}
