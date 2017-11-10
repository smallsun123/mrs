
#include "sourcelist.h"

pid_url_node_t* create_node()
{
	pid_url_node_t *node = NULL;
	node = (pid_url_node_t*)malloc((size_t)sizeof(*node));
	if(node == NULL)
		return NULL;

	INIT_LIST_HEAD(&node->node);
	
	node->pid = -1;
	memset((void*)node->url, 0, sizeof(node->url));
	memset((void*)node->room, 0, sizeof(node->room));
	
	return node;
}

list_t* create_list()
{
	list_t *li = NULL;
	li = (list_t*)malloc(sizeof(*li));
	if(li == NULL)
		return NULL;

	INIT_LIST_HEAD(&li->list);

	return li;
}


int free_node(pid_url_node_t *node)
{
	if(node){
		free((void*)node);
		node = NULL;
	}
	return 0;
}

int free_list(list_t *list)
{
	struct list_head *pos, *npos;
	pid_url_node_t *nd;

	if(list){
		list_for_each_safe(pos, npos, &list->list){
			nd = list_entry(pos, struct pid_url_node_s, node);
			list_del(pos);
			free_node(nd);
		}

		free((void*)list);
		list = NULL;
	}

	return 0;
}


seq_node_t* create_seq_node()
{
	seq_node_t *node = NULL;
	node = (seq_node_t*)malloc(sizeof(*node));
	if(node == NULL)
		return NULL;

	INIT_LIST_HEAD(&node->node);
	
	node->seq = -1;
	memset((void*)node->room, 0, sizeof(node->room));
	
	return node;
}

seq_list_t* create_seq_list()
{
	seq_list_t *li = NULL;
	li = (seq_list_t*)malloc(sizeof(*li));
	if(li == NULL)
		return NULL;

	INIT_LIST_HEAD(&li->list);

	return li;
}


int free_seq_node(seq_node_t *node)
{
	if(node){
		free((void*)node);
		node = NULL;
	}
	return 0;
}

int free_seq_list(seq_list_t *list)
{
	struct list_head *pos, *npos;
	seq_node_t *nd;

	if(list){
		list_for_each_safe(pos, npos, &list->list){
			nd = list_entry(pos, seq_node_t, node);
			list_del(pos);
			free_seq_node(nd);
		}

		free((void*)list);
		list = NULL;
	}

	return 0;
}


