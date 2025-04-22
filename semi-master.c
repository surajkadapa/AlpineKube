#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <signal.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <zmq.h>
#include <time.h>
#include <pthread.h>

pthread_t heartbeat_thread;
pthread_t shell_thread;

#define HEARTBEAT_PORT 5557
#define HEARTBEAT_INTERVAL 3
#define LOCAL_IP_BUFFER_SIZE INET_ADDRSTRLEN
#define BROADCAST_PORT 5000
#define MASTER_PORT 5555  // Master listens for node registration
char *master_ip;
int node_id = -1;  // Global variable to store node ID


void run_shell_script() {
    system("/root/testing/new_ip.sh");

    printf("[MSG] Shell script executed successfully\n");
}

char* get_own_ip() {
    static char ip_buffer[LOCAL_IP_BUFFER_SIZE] = "UNKNOWN";
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1) {
        perror("[ERR] getifaddrs failed");
        return ip_buffer;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) { // Only IPv4
            struct sockaddr_in *addr = (struct sockaddr_in*)ifa->ifa_addr;
            char *ip = inet_ntoa(addr->sin_addr);

            // Ignore loopback address (127.0.0.1)
            if (strcmp(ip, "127.0.0.1") != 0) {
                strncpy(ip_buffer, ip, LOCAL_IP_BUFFER_SIZE);
                break;  // Take the first valid IP
            }
        }
    }

    freeifaddrs(ifaddr);
    printf("[IPADDR] The ip address is: %s\n", ip_buffer);
    return ip_buffer;
}


// Function to discover the master's IP via broadcast
char* discover_master() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    struct sockaddr_in recvAddr;
    memset(&recvAddr, 0, sizeof(recvAddr));
    recvAddr.sin_family = AF_INET;
    recvAddr.sin_port = htons(BROADCAST_PORT);
    recvAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr*)&recvAddr, sizeof(recvAddr)) < 0) {
        perror("Bind failed");
        close(sock);
        exit(1);
    }

    char buffer[1024];
    struct sockaddr_in senderAddr;
    socklen_t senderLen = sizeof(senderAddr);

    printf("Listening for master broadcast...\n");

    while (1) {
        int received = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&senderAddr, &senderLen);
        if (received > 0) {
            buffer[received] = '\0';  
            printf("Received: %s\n", buffer);
            char *start = strstr(buffer, "\"ip\":\"");
            if (start) {
                start += 6;
                char *end = strchr(start, '"');
                if (end) {
                    size_t len = end - start;
                    char *ip = (char*)malloc(len + 1);
                    if (!ip) {
                        perror("Memory allocation failed");
                        close(sock);
                        return NULL;
                    }
                    strncpy(ip, start, len);
                    ip[len] = '\0';
                    close(sock);
                    return ip;  
                }
            }
        }
    }
    close(sock);
    return NULL;
}

// Get CPU count using system command
int get_cpu_count() {
    FILE *fp = popen("nproc", "r");  
    if (!fp) return -1;

    char buffer[10];
    fgets(buffer, sizeof(buffer), fp);
    pclose(fp);
    return atoi(buffer);
}

// Get memory size from /proc/meminfo
int get_memory_size() {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return -1;

    char line[256];
    int mem_total = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "MemTotal: %d kB", &mem_total) == 1) {
            mem_total /= 1024; 
            break;
        }
    }
    fclose(fp);
    return mem_total;
}

void request_node_id() {
    if (!master_ip) {
        printf("[ERR] No master IP found, cannot request node ID!\n");
        return;
    }

    void *context = zmq_ctx_new();
    void *requester = zmq_socket(context, ZMQ_REQ);

    char master_address[256];
    snprintf(master_address, sizeof(master_address), "tcp://%s:%d", master_ip, MASTER_PORT);

    printf("[MSG] Connecting to master at %s\n", master_address);
    if (zmq_connect(requester, master_address) != 0) {
        perror("[ERR] zmq_connect failed");
        zmq_close(requester);
        zmq_ctx_destroy(context);
        return;
    }

    int cpus = get_cpu_count();
    int memory = get_memory_size();
    char request[256];
    char *own_ip;
    own_ip = get_own_ip();
    snprintf(request, sizeof(request), 
             "{\"node_ip\":\"%s\",\"cpus\":%d,\"memory\":%d,\"message_type\":\"registration\"}",
             own_ip, cpus, memory);

    printf("[MSG] Sending registration request to %s: %s\n", master_ip, request);
    zmq_send(requester, request, strlen(request), 0);

    char response[256];
    zmq_recv(requester, response, sizeof(response), 0);
    response[255] = '\0'; 

    sscanf(response, "{\"node_ip\":\"%*15[^\"]\",\"node_id\":%d}", &node_id);

    printf("[MSG] Registered with Node ID: %d\n", node_id);

    zmq_close(requester);
    zmq_ctx_destroy(context);
}

void *send_heartbeat(void *arg) {
    void *context = zmq_ctx_new();
    void *publisher = zmq_socket(context, ZMQ_PUB);

    char endpoint[50];
    snprintf(endpoint, sizeof(endpoint), "tcp://%s:%d", master_ip, HEARTBEAT_PORT);
    sleep(1);
    if (zmq_connect(publisher, endpoint) != 0) {
        perror("[ERR] Failed to bind ZeroMQ PUB socket");
        zmq_close(publisher);
        zmq_ctx_destroy(context);
        return NULL;
    }

    while (1) {
        sleep(HEARTBEAT_INTERVAL);

        time_t now = time(NULL);
        int cpu_available = get_cpu_count();
        int memory_available = get_memory_size();

        char heartbeat_msg[100];
        snprintf(heartbeat_msg, sizeof(heartbeat_msg), "%d %ld %d %d",
                 node_id, now, cpu_available, memory_available);

        zmq_send(publisher, heartbeat_msg, strlen(heartbeat_msg), 0);
        printf("[HB] Sent heartbeat: %s\n", heartbeat_msg);
    }

    zmq_close(publisher);
    zmq_ctx_destroy(context);
    return NULL;
}

pid_t shell_pid = -1;
void handle_sigint(int sig) {
    printf("\nSIGINT received. Stopping heartbeat thread and shell process...\n");
    pthread_cancel(heartbeat_thread);  

    if (shell_pid > 0) {
        kill(shell_pid, SIGTERM); 
        waitpid(shell_pid, NULL, 0);
    }
}

void *start_shell(void *arg) {
    pid_t pid = fork();

    if (pid == -1) {
        perror("[ERR] Fork failed");
        return NULL;
    }

    if (pid == 0) {  // Child process
        // Start the shell command
        execlp("ttyd", "ttyd", "-p", "7681", "--writable", "/bin/sh", (char *)NULL);
        perror("[ERR] execlp failed");
        exit(1);
    } else {  // Parent process
        // Save the shell PID to terminate it later in the signal handler
        shell_pid = pid;
        printf("[MSG] Started shell with PID: %d\n", shell_pid);
    }

    // Wait for the shell process to finish
    waitpid(pid, NULL, 0);
    return NULL;
}


int main() {
    run_shell_script();
    master_ip = discover_master();
    if (master_ip) {
        printf("[MSG] The master IP address is: %s\n", master_ip);
        printf("[REG] Requesting node_id\n");
        request_node_id();
    } else {
        printf("[ERR] No master found!!\n");
    }

    signal(SIGINT, handle_sigint);

    printf("[REG]Node ID: %d\n", node_id);
    printf("[MSG]Starting heartbeat thread.....\n");
    if (pthread_create(&heartbeat_thread, NULL, send_heartbeat, NULL) != 0) {
        perror("[ERR] Failed to create heartbeat thread");
        exit(1);
    }

    if(pthread_create(&shell_thread, NULL, start_shell, NULL) != 0) {
        perror("[ERR] Failed to start shell");
        exit(1);
    }

    pthread_join(heartbeat_thread, NULL);
    pthread_join(shell_thread, NULL);

    return 0;
}
