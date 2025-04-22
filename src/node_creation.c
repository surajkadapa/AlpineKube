#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include "../include/node_creation.h"

#define SOCKET_PATH "/tmp/node_creation.sock"
#define BUFFER_SIZE 1024


void parse_node_request(const char *request, int *cpus, int *memory) {
    sscanf(request, "{\"cpus\": \"%d\", \"memory\": \"%d\"}", cpus, memory);
}

int cpus, memory;

void *handle_client(void *arg) {
    int client_sock = *(int *)arg;
    free(arg);  // Free the dynamically allocated socket descriptor

    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    // Receive data
    int bytes_received = recv(client_sock, buffer, BUFFER_SIZE, 0);
    if (bytes_received > 0) {
        printf("[LISTENER] Received node creation request: %s\n", buffer);

        parse_node_request(buffer, &cpus, &memory);
        printf("[DEBUG] CPUs requested: %d\n", cpus);
        printf("[DEBUG] Memory Requested: %dMB\n", memory);
        
        char overlay_create[512];  
        snprintf(overlay_create, sizeof(overlay_create), 
                 "sudo qemu-img create -f qcow2 -b /var/lib/libvirt/images/linux2022.qcow2 -F qcow2 /var/lib/libvirt/images/overlay_%d.qcow2", 
                 node_count);
        system(overlay_create);
        char vm_startup[512];  
        snprintf(vm_startup, sizeof(vm_startup),
                 "sudo virt-install --name my-vm%d --memory %d --vcpus %d --disk path=/var/lib/libvirt/images/overlay_%d.qcow2,format=qcow2 --os-variant ubuntu20.04 --network network=default,model=virtio --graphics none --noautoconsole --import &",
                 node_count, memory, cpus, node_count);
        system(vm_startup);

        const char *response = "Node creation request received. Booting soon...";
        send(client_sock, response, strlen(response), 0);
    }

    close(client_sock);
    return NULL;
}

void start_node_creation_listener() {
    int server_sock, client_sock;
    struct sockaddr_un server_addr;

    // Create UNIX domain socket
    server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Remove existing socket file if it exists
    unlink(SOCKET_PATH);

    // Configure socket address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    // Bind socket
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_sock, 5) == -1) {
        perror("Listen failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    printf("[LISTENER] Node creation listener started.\n");

    while (1) {
        // Accept client connection
        client_sock = accept(server_sock, NULL, NULL);
        if (client_sock == -1) {
            perror("Accept failed");
            continue;
        }

        // Create a thread to handle the request
        pthread_t thread;
        int *new_sock = malloc(sizeof(int));  // Allocate memory for client socket descriptor
        *new_sock = client_sock;
        pthread_create(&thread, NULL, handle_client, new_sock);
        pthread_detach(thread);  // Detach thread to avoid memory leaks
    }

    close(server_sock);
}