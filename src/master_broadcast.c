#include "../include/master_broadcast.h"

#define BROADCAST_INTERVAL 5
#define BROADCAST_PORT 5000  

void get_local_ips(char ips[10][INET_ADDRSTRLEN], char broadcasts[10][INET_ADDRSTRLEN], int *count) {
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    struct sockaddr_in *mask;
    *count = 0;

    if (getifaddrs(&ifap) == -1) {
        perror("getifaddrs");
        exit(1);
    }

    for (ifa = ifap; ifa && *count < 10; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET && ifa->ifa_netmask) {
            sa = (struct sockaddr_in *) ifa->ifa_addr;
            mask = (struct sockaddr_in *) ifa->ifa_netmask;
            
            //skipping lo
            if ((sa->sin_addr.s_addr & 0xFF) == 127)
                continue;
                
            inet_ntop(AF_INET, &(sa->sin_addr), ips[*count], INET_ADDRSTRLEN);
            
            in_addr_t host_ip = sa->sin_addr.s_addr;
            in_addr_t netmask = mask->sin_addr.s_addr;
            in_addr_t broadcast = host_ip | ~netmask;
            
            struct in_addr broadcast_addr;
            broadcast_addr.s_addr = broadcast;
            inet_ntop(AF_INET, &broadcast_addr, broadcasts[*count], INET_ADDRSTRLEN);
            
            printf("Interface: %s, IP: %s, Broadcast: %s\n", 
                  ifa->ifa_name, ips[*count], broadcasts[*count]);
            
            (*count)++;
        }
    }

    freeifaddrs(ifap);
}

void broadcast() {
    char local_ips[10][INET_ADDRSTRLEN];
    char broadcast_ips[10][INET_ADDRSTRLEN];
    int ip_count = 0;
    
    get_local_ips(local_ips, broadcast_ips, &ip_count);
    
    if (ip_count == 0) {
        fprintf(stderr, "No suitable network interfaces found\n");
        return;
    }
    
    char *master_ip = local_ips[0];
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    int broadcastEnable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        perror("Failed to set broadcast option");
        close(sock);
        exit(1);
    }
    
    struct sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddr.sin_port = htons(0);
    
    if (bind(sock, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
        perror("Failed to bind socket");
        close(sock);
        exit(1);
    }
    
    printf("Starting broadcast service using IP: %s\n", master_ip);
    
    char message[100];
    snprintf(message, sizeof(message), "{\"type\":\"master\", \"ip\":\"%s\"}", master_ip);
    
    while (1) {
        for (int i = 0; i < ip_count; i++) {
            struct sockaddr_in broadcastAddr;
            memset(&broadcastAddr, 0, sizeof(broadcastAddr));
            broadcastAddr.sin_family = AF_INET;
            broadcastAddr.sin_port = htons(BROADCAST_PORT);
            inet_pton(AF_INET, broadcast_ips[i], &broadcastAddr.sin_addr);  
            int bytes_sent = sendto(sock, message, strlen(message), 0, 
                            (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
                            
            if (bytes_sent < 0) {
                perror("Failed to send broadcast to subnet");
            } else {
                printf("Sent %d bytes to %s\n", bytes_sent, broadcast_ips[i]);
            }
        }
        
        sleep(BROADCAST_INTERVAL);
    }
    
    close(sock);
}