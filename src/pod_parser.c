#include <stdio.h>
#include <stdlib.h>
#include <zmq.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <zip.h>
#include "../include/pod_parser.h"

int pod_counter = 0;
pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;

int file_exists_in_dir(const char *dir, const char *filename) {
    char fullpath[256];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, filename);
    return access(fullpath, F_OK) == 0;
}

void cleanup_tmp_dir(const char *dir) {
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);
}

int parse_pod_file(const char *filepath, PodSpec *spec) {
    FILE *file = fopen(filepath, "r");
    if (!file) {
        perror("Failed to open .pod file");
        return -1;
    }

    char line[MAX_LINE_LENGTH];
    int in_sidecars = 0;

    memset(spec, 0, sizeof(PodSpec));

    while (fgets(line, sizeof(line), file)) {
        // printf("line: %s\n", line);
        if (strlen(line) == 0 || line[0] == '#') continue;

        if (strncmp(line, "sidecars:", 9) == 0) {
            in_sidecars = 1;
            continue;
        }

        if (in_sidecars && strncmp(line, "  - logging:", 12) == 0) {
            // printf("inside\n");
            sscanf(line + 12, "%127s", spec->logging_script);
            continue;
        }

        if (strncmp(line, "name:", 5) == 0) {
            sscanf(line + 5, "%127s", spec->name);
        } else if (strncmp(line, "cpu:", 4) == 0) {
            sscanf(line + 4, "%d", &spec->cpu);
        } else if (strncmp(line, "memory:", 7) == 0) {
            char mem_str[32];
            sscanf(line + 7, "%31s", mem_str);
            if (strstr(mem_str, "MB")) {
                spec->memory = atoi(mem_str);
            } else if (strstr(mem_str, "GB")) {
                spec->memory = atoi(mem_str) * 1024;
            }
        } else if (strncmp(line, "main:", 5) == 0) {
            sscanf(line + 5, "%127s", spec->main_script);
        }
    }

    fclose(file);
    return 0;
}

int validate_zip_contents(const char *zip_path, const PodSpec *spec) {
    struct zip *zip;
    int err;

    // Open ZIP file
    zip = zip_open(zip_path, ZIP_RDONLY, &err);
    if (!zip) {
        fprintf(stderr, "Failed to open ZIP: %s (error code: %d)\n", zip_path, err);
        return -1;
    }

    // Create temp directory
    if (mkdir(TMP_DIR, 0755) && errno != EEXIST) {
        fprintf(stderr, "Failed to create temp directory %s: %s\n", TMP_DIR, strerror(errno));
        zip_close(zip);
        return -1;
    }

    // Extract files
    zip_int64_t num_entries = zip_get_num_entries(zip, 0);
    printf("Found %lld entries in ZIP\n", (long long)num_entries);
    for (zip_uint64_t i = 0; i < num_entries; ++i) {
        struct zip_stat st;
        zip_stat_init(&st);
        if (zip_stat_index(zip, i, 0, &st) != 0) {
            fprintf(stderr, "Failed to stat ZIP entry at index %llu\n", (unsigned long long)i);
            continue;
        }

        const char *name = st.name;
        printf("Processing entry: %s (size: %lld)\n", name, (long long)st.size);

        // Skip directories
        if (name[strlen(name) - 1] == '/') {
            printf("Skipping directory: %s\n", name);
            continue;
        }

        // Open ZIP entry
        struct zip_file *zf = zip_fopen_index(zip, i, 0);
        if (!zf) {
            fprintf(stderr, "Failed to open ZIP entry: %s\n", name);
            continue;
        }

        // Construct output path
        char outpath[300];
        snprintf(outpath, sizeof(outpath), "%s/%s", TMP_DIR, name);

        // Create parent directories if needed
        char *dir = strdup(outpath);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            if (mkdir(dir, 0755) && errno != EEXIST) {
                fprintf(stderr, "Failed to create directory %s: %s\n", dir, strerror(errno));
            }
        }
        free(dir);

        // Write file
        FILE *fout = fopen(outpath, "wb");
        if (!fout) {
            fprintf(stderr, "Failed to open output file %s: %s\n", outpath, strerror(errno));
            zip_fclose(zf);
            continue;
        }

        char buffer[4096];
        zip_int64_t n;
        while ((n = zip_fread(zf, buffer, sizeof(buffer))) > 0) {
            if (fwrite(buffer, 1, n, fout) != (size_t)n) {
                fprintf(stderr, "Failed to write to %s: %s\n", outpath, strerror(errno));
                break;
            }
        }
        if (n < 0) {
            fprintf(stderr, "Error reading ZIP entry %s\n", name);
        }

        fclose(fout);
        zip_fclose(zf);
        printf("Extracted: %s\n", outpath);
    }

    zip_close(zip);

    // Check for required files in testing/ subdirectory
    char main_path[256], log_path[256];
    snprintf(main_path, sizeof(main_path), "%s/testing/%s", TMP_DIR, spec->main_script);
    snprintf(log_path, sizeof(log_path), "%s/testing/%s", TMP_DIR, spec->logging_script);

    int main_found = access(main_path, F_OK) == 0;
    int log_found = access(log_path, F_OK) == 0;

    printf("Checking for main script: %s\n", main_path);
    printf("Checking for logging script: %s\n", log_path);

    if (main_found && log_found) {
        printf("✅ Found both %s and %s in ZIP.\n", spec->main_script, spec->logging_script);
    } else {
        if (!main_found) printf("❌ Missing main script: %s\n", main_path);
        if (!log_found) printf("❌ Missing logging script: %s\n", log_path);
    }

    cleanup_tmp_dir(TMP_DIR); 
    return (main_found && log_found) ? 0 : -1;
}

void *pod_listener_thread(void *arg) {
    void *context = zmq_ctx_new();
    void *socket = zmq_socket(context, ZMQ_REP);
    int rc = zmq_bind(socket, "ipc:///tmp/pod_dispatcher.sock");
    if (rc != 0) {
        perror("Failed to bind ZeroMQ socket");
        return NULL;
    }
    printf("[DEBUG] Started pod listener thread\n");
    while (1) {
        zmq_msg_t meta_msg, pod_msg, zip_msg;

        // Receive JSON metadata
        zmq_msg_init(&meta_msg);
        zmq_msg_recv(&meta_msg, socket, 0);
        char *meta_json = strndup((char *)zmq_msg_data(&meta_msg), zmq_msg_size(&meta_msg));
        zmq_msg_close(&meta_msg);

        // Receive .pod content
        zmq_msg_init(&pod_msg);
        zmq_msg_recv(&pod_msg, socket, 0);
        void *pod_data = zmq_msg_data(&pod_msg);
        size_t pod_size = zmq_msg_size(&pod_msg);

        // Receive .zip content
        zmq_msg_init(&zip_msg);
        zmq_msg_recv(&zip_msg, socket, 0);
        void *zip_data = zmq_msg_data(&zip_msg);
        size_t zip_size = zmq_msg_size(&zip_msg);

        // Create folder: pods/pod_<number>/
        pthread_mutex_lock(&counter_lock);
        int pod_id = pod_counter++;
        pthread_mutex_unlock(&counter_lock);

        char folder_path[256];
        snprintf(folder_path, sizeof(folder_path), "pods/pod_%d", pod_id);
        mkdir("pods", 0777); // Safe: mkdir on existing dir doesn't error
        mkdir(folder_path, 0777);

        // Extract filenames from metadata
        char pod_filename[128] = "uploaded.pod";
        char zip_filename[128] = "uploaded.zip";

        sscanf(meta_json, "{\"pod_filename\":\"%127[^\"]\",\"zip_filename\":\"%127[^\"]\"}",
               pod_filename, zip_filename);
        free(meta_json);

        // Save pod file
        char pod_path[256];
        snprintf(pod_path, sizeof(pod_path), "%s/%s", folder_path, pod_filename);
        FILE *fp = fopen(pod_path, "wb");
        if (fp) {
            fwrite(pod_data, 1, pod_size, fp);
            fclose(fp);
        }

        // Save zip file
        char zip_path[256];
        snprintf(zip_path, sizeof(zip_path), "%s/%s", folder_path, zip_filename);
        FILE *fz = fopen(zip_path, "wb");
        if (fz) {
            fwrite(zip_data, 1, zip_size, fz);
            fclose(fz);
        }
        
        zmq_msg_close(&pod_msg);
        zmq_msg_close(&zip_msg);

        // Basic verification (you can expand this)
        PodSpec spec;
        int pod_res = parse_pod_file(pod_path, &spec);
        int zip_res = validate_zip_contents(zip_path, &spec);
        if (pod_res != 0 || zip_res != 0) {
            zmq_send(socket, "Upload failed: empty file(s)", 28, 0);
        } else {
            zmq_send(socket, "Upload & parsing successful", 27, 0);
        }
    }

    zmq_close(socket);
    zmq_ctx_term(context);
    return NULL;
}
