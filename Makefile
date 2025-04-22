CC = gcc
CFLAGS = -Wall -g -Iinclude 
LDFLAGS = -lzmq -ljson-c -lzip

SRC_DIR = src
INCLUDE_DIR = include
OBJ_DIR = obj

$(shell mkdir -p $(OBJ_DIR))

all: master

master: $(OBJ_DIR)/master_broadcast.o \
        $(OBJ_DIR)/node_registry.o \
        $(OBJ_DIR)/node_creation.o \
        $(OBJ_DIR)/pod_parser.o \
        $(OBJ_DIR)/main.o
	$(CC) $(CFLAGS) -o master $^ $(LDFLAGS) 

$(OBJ_DIR)/master_broadcast.o: $(SRC_DIR)/master_broadcast.c $(INCLUDE_DIR)/master_broadcast.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/node_registry.o: $(SRC_DIR)/node_registry.c $(INCLUDE_DIR)/node_registry.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/node_creation.o: $(SRC_DIR)/node_creation.c $(INCLUDE_DIR)/node_creation.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/pod_parser.o: $(SRC_DIR)/pod_parser.c $(INCLUDE_DIR)/pod_parser.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/main.o: main.c \
    $(INCLUDE_DIR)/master_broadcast.h \
    $(INCLUDE_DIR)/node_registry.h \
    $(INCLUDE_DIR)/pod_parser.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) master
