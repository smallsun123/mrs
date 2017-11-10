
#ifndef _SOURCE_LIST_H_
#define _SOURCE_LIST_H_

#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include "list.h"


typedef struct pid_url_node_s pid_url_node_t;

struct pid_url_node_s{
	struct list_head 	node;
	
	pid_t 				pid;
	char 				room[100];
	char 				url[100];
};

typedef struct {
	struct list_head 	list;
}list_t;

typedef struct {
	struct list_head 	node;
	char 				room[100];
	int					seq;
}seq_node_t;

typedef struct {
	struct list_head 	list;
}seq_list_t;


pid_url_node_t* create_node();
list_t* create_list();


int free_node(pid_url_node_t *node);
int free_list(list_t *list);


seq_node_t* create_seq_node();
seq_list_t* create_seq_list();


int free_seq_node(seq_node_t *node);
int free_seq_list(seq_list_t *list);


#endif

