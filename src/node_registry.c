#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <zmq.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <json-c/json.h>
#include "../include/node_registry.h"

#define MASTER_PORT 5555
#define SOCKET_PATH "/tmp/node_registry.sock"

void print_node_info(NodeInfo *node) {
    printf("\n[DEBUG] Node Struct Dump:\n");
    printf("  Node ID: %d\n", node->node_id);
    printf("  Node IP: %s\n", node->node_ip);
    printf("  CPUs: %d\n", node->cpus);
    printf("  Memory: %d MB\n", node->memory);
    printf("  Pod Count: %d\n", node->pod_count);
    printf("----------------------------\n");
}

void *node_registration_thread(void *arg) {
    void *context = zmq_ctx_new();
    void *responder = zmq_socket(context, ZMQ_REP);

    char bind_address[256];
    snprintf(bind_address, sizeof(bind_address), "tcp://*:%d", MASTER_PORT);

    if (zmq_bind(responder, bind_address) != 0) {
        perror("[ERR] zmq_bind failed");
        zmq_close(responder);
        zmq_ctx_destroy(context);
        return NULL;
    }

    printf("[MSG] Master is listening for node registrations on port %d...\n", MASTER_PORT);

    while (1) {
        char buffer[256];
        zmq_recv(responder, buffer, sizeof(buffer) - 1, 0);
        buffer[255] = '\0';  

        char node_ip[16];
        int cpus, memory;
        sscanf(buffer, "{\"node_ip\":\"%15[^\"]\",\"cpus\":%d,\"memory\":%d,\"message_type\":\"registration\"}",
               node_ip, &cpus, &memory);

        pthread_mutex_lock(&node_table_lock);

        int node_id = node_count;
        node_count++;

        strcpy(node_table[node_id].node_ip, node_ip);
        node_table[node_id].cpus = cpus;
        node_table[node_id].memory = memory;
        node_table[node_id].pod_count = 0;
        node_table[node_id].node_id = node_id; 
        node_table[node_id].last_heartbeat = time(NULL); 
        node_table[node_id].active = 1;  
        print_node_info(&node_table[node_id]); 

        pthread_mutex_unlock(&node_table_lock);

        printf("[MSG] Assigned Node ID %d to %s (CPUs: %d, Memory: %dMB) | Heartbeat Initialized\n",
               node_id, node_ip, cpus, memory);

        char response[128];
        snprintf(response, sizeof(response), "{\"node_ip\":\"%s\",\"node_id\":%d}", node_ip, node_id);
        zmq_send(responder, response, strlen(response), 0);
    }

    zmq_close(responder);
    zmq_ctx_destroy(context);
    return NULL;
}


void send_node_data(int client_sock) {
    struct json_object *node_array = json_object_new_array();

    pthread_mutex_lock(&node_table_lock);
    for (int i = 0; i < node_count; i++) {
        struct json_object *node_obj = json_object_new_object();
        json_object_object_add(node_obj, "node_id", json_object_new_int(node_table[i].node_id));
        json_object_object_add(node_obj, "node_ip", json_object_new_string(node_table[i].node_ip));
        json_object_object_add(node_obj, "cpus", json_object_new_int(node_table[i].cpus));
        json_object_object_add(node_obj, "memory", json_object_new_int(node_table[i].memory));
        json_object_object_add(node_obj, "pod_count", json_object_new_int(node_table[i].pod_count));
        json_object_object_add(node_obj, "active", json_object_new_boolean(node_table[i].active));

        json_object_array_add(node_array, node_obj);
    }
    pthread_mutex_unlock(&node_table_lock);

    const char *json_str = json_object_to_json_string(node_array);
    write(client_sock, json_str, strlen(json_str));
}



void *heartbeat_receiver_thread(void *arg) {
    void *context = zmq_ctx_new();
    void *subscriber = zmq_socket(context, ZMQ_SUB);

    zmq_bind(subscriber, "tcp://*:5557");
    zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);

    printf("[MSG] Master is listening for heartbeats on port 5557...\n");

    while (1) {
        char buffer[64];
        zmq_recv(subscriber, buffer, sizeof(buffer) - 1, 0);
        buffer[63] = '\0';

        int node_id;
        time_t timestamp;
        sscanf(buffer, "%d %ld", &node_id, &timestamp);

        pthread_mutex_lock(&node_table_lock);

        if (node_id >= 0 && node_id < node_count) {
            node_table[node_id].last_heartbeat = timestamp;
            printf("[HB] Heartbeat received: Node %d at %ld\n", node_id, timestamp);
        }

        pthread_mutex_unlock(&node_table_lock);
    }

    zmq_close(subscriber);
    zmq_ctx_destroy(context);
    return NULL;
}

void *node_monitoring_thread(void *arg) {
    while (1) {
        sleep(5); 

        time_t now = time(NULL);
        pthread_mutex_lock(&node_table_lock);

        for (int i = 0; i < node_count; i++) {
            printf("time is %ld\n", now);
            if (difftime(now, node_table[i].last_heartbeat) > 8) {
                node_table[i].active = 0; 
                printf("[MONITOR] Node %d is INACTIVE!\n", i);
            } else {
                node_table[i].active = 1; 
                printf("[MONITOR] Node %d is ACTIVE.\n", i);
            }
        }

        pthread_mutex_unlock(&node_table_lock);
    }
    return NULL;
}


void *socket_server_thread(void *arg) {
    int server_sock, client_sock;
    struct sockaddr_un server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    unlink(SOCKET_PATH);
    server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("[ERR] Socket creation failed");
        return NULL;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERR] Bind failed");
        close(server_sock);
        return NULL;
    }

    if (listen(server_sock, 5) < 0) {
        perror("[ERR] Listen failed");
        close(server_sock);
        return NULL;
    }

    printf("[MSG] Unix socket server running at %s...\n", SOCKET_PATH);

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_sock < 0) {
            perror("[ERR] Accept failed");
            continue;
        }

        char buffer[256] = {0};
        read(client_sock, buffer, sizeof(buffer) - 1);

        if (strcmp(buffer, "GET_NODES") == 0) {
            send_node_data(client_sock);
        }

        close(client_sock);
    }

    close(server_sock);
    unlink(SOCKET_PATH);
    return NULL;
}