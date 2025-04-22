#ifndef MASTER_BROADCAST_H
#define MASTER_BROADCAST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

// Define broadcast parameters
#define PORT "5555"  

// Function prototypes
void get_ip_address(char *ip_buffer);
void broadcast();

#endif // MASTER_BROADCAST_H
