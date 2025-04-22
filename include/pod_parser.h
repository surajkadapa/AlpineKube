#ifndef POD_PARSER_H
#define POD_PARSER_H

#define MAX_LINE_LENGTH 256
#define MAX_PATH_LENGTH 128
#define TMP_DIR "./tmp_extract"

typedef struct {
    char name[MAX_PATH_LENGTH];
    int cpu;
    int memory; // in MB
    char main_script[MAX_PATH_LENGTH];
    char logging_script[MAX_PATH_LENGTH];
} PodSpec;

// Parses .pod file and fills the PodSpec struct
int parse_pod_file(const char *filepath, PodSpec *spec);
int validate_zip_contents(const char *zip_path, const PodSpec *spec);
void *pod_listener_thread(void *arg);

#endif // POD_PARSER_H
