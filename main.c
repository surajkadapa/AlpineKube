#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include "include/master_broadcast.h"
#include "include/node_registry.h"
#include "include/node_creation.h"
#include "include/pod_parser.h"
#define MAX_NODES 100

pthread_t broadcast_thread;
pthread_t registry_thread;
pthread_t server_thread;
pthread_t hb_receiver_thread;
pthread_t node_monitor_thread;
pthread_t node_creation_thread;
pthread_t pod_creation_thread;

NodeInfo node_table[MAX_NODES];
int node_count = 0;
pthread_mutex_t node_table_lock = PTHREAD_MUTEX_INITIALIZER;

void handle_sigint(int sig) {
    printf("\nSIGINT received. Stopping broadcast thread...\n");
    pthread_cancel(broadcast_thread);
    pthread_cancel(registry_thread);
    pthread_cancel(server_thread);
    pthread_cancel(hb_receiver_thread);
    pthread_cancel(node_monitor_thread);
    pthread_cancel(node_creation_thread);
    pthread_cancel(pod_creation_thread);
}

int main() {
    printf("Starting Master broadcast...\n");

    signal(SIGINT, handle_sigint);

    if (pthread_create(&hb_receiver_thread, NULL, (void*)heartbeat_receiver_thread, NULL) != 0){
        perror("Failed to start HB receiver thread");
        return EXIT_FAILURE;
    }

    if (pthread_create(&node_creation_thread, NULL, (void*)start_node_creation_listener, NULL) != 0){
        perror("Failed to start node creation thread");
        return EXIT_FAILURE;
    }

    if (pthread_create(&broadcast_thread, NULL, (void*)broadcast, NULL) != 0){
        perror("Failed to create broadcast thread");
        return EXIT_FAILURE;
    }

    if (pthread_create(&registry_thread, NULL, (void*)node_registration_thread, NULL) != 0){
        perror("Failed to create registration thread");
        return EXIT_FAILURE;
    }

    if (pthread_create(&server_thread, NULL, (void*)socket_server_thread, NULL) != 0){
        perror("Failed to start server thread");
        return EXIT_FAILURE;
    }


    if (pthread_create(&node_monitor_thread, NULL, (void*)node_monitoring_thread, NULL) != 0){
        perror("Failed to start node monitoring thread");
        return EXIT_FAILURE;
    }

    if (pthread_create(&pod_creation_thread, NULL, (void*)pod_listener_thread, NULL) != 0){
        perror("Failed to start pod listener thread");
        return EXIT_FAILURE;
    }

    PodSpec spec;
    if(parse_pod_file("testing.pod", &spec) == 0){
        printf("Pod Name: %s\n", spec.name);
        printf("CPU: %d\n", spec.cpu);
        printf("Memory: %d MB\n", spec.memory);
        printf("Main: %s\n", spec.main_script);
        printf("Logging: %s\n", spec.logging_script);
        validate_zip_contents("testing.zip", &spec);
    }
    pthread_join(broadcast_thread, NULL);
    pthread_join(registry_thread, NULL);
    pthread_join(server_thread, NULL);
    pthread_join(hb_receiver_thread, NULL);
    pthread_join(node_monitor_thread, NULL);
    pthread_join(node_creation_thread, NULL);
    pthread_join(pod_creation_thread, NULL);
    printf("All threads stopped.\n");
    
    return 0;
}
