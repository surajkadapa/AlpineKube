#ifndef NODE_REGISTRY_H
#define NODE_REGISTRY_H

#include <pthread.h>

typedef struct {
    char node_ip[16];
    int cpus;
    int memory;
    int pods[100];
    int pod_count;
    int node_id;
    time_t last_heartbeat;
    int active;
} NodeInfo;

#define MAX_NODES 100

extern NodeInfo node_table[MAX_NODES];
extern int node_count;
extern pthread_mutex_t node_table_lock;

void *node_registration_thread(void *arg);
void send_node_data(int client_sock);
void *socket_server_thread(void *arg);
void *heartbeat_receiver_thread(void *arg);
void *node_monitoring_thread(void *arg);

#endif // NODE_REGISTRY_H
