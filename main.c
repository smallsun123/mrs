
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <sys/stat.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <dirent.h>
#include <time.h>
#include <stdarg.h>

#include <sys/ipc.h>
#include <sys/msg.h>


#include "sourcelist.h"

enum log_level{
	log_debug,
	log_info,
	log_warning,
	log_error
};

enum ipc_msg{
	msg_add_room_task = 1,
	msg_delete_room_task,	//2
	msg_update_room_seq,	//3
	msg_delete_room_seq,	//4
	msg_unknown				//5
};

#define BUFFER_SIZE 256

typedef struct msg_st_s{
    long type;
    char text[BUFFER_SIZE];
}msg_st;

#define CONCURRENT_MAX 10
#define BACKLOG	10

#define SERVER_IP "10.27.96.11"
#define SERVER_PORT 7070

pid_t g_spid;

char input_msg[BUFFER_SIZE];
char recv_msg[BUFFER_SIZE];
char dst_path[1024] = {0};

list_t *g_list = NULL;
seq_list_t *g_seq_list = NULL;

int daemo_start = 0;

pid_t start_recod_video(char* room, char *input, int seq);
void stop_recod_video(char *room);
pid_t pause_recod_video(char *room);
int detecte_file_exit(char *path);
int detecte_pid(char *pid);
void delete_room_task(char *room);
void logmsg(enum log_level level, char* log, ...);
int update_room_seq(char *room);
void delete_room_seq(char *room);
void add_room_task(pid_t pid, char *room);
int worker_fun();
void send_ipc_msg(int id, enum ipc_msg type, char *msg);
int detecte_room_task(char *room);
void worker_signal_handle(int nsignal);
int remove_dir(const char *dir);
void signal_handle(int nsignal);

int parse_msg(char *msg, int *type, char *url, char *roomid){
	char *p, *end = NULL;

	if(!msg || !url || !roomid)
		goto failed;
	
	p = strstr(msg, "type");
	if(!p)
		goto failed;
	
	p = strchr(msg, ':');
	p++;
	end = strchr(p, ';');
	
	if(memcmp(p, "start", end - p) == 0)
		*type = 1;
	if(memcmp(p, "pause", end - p) == 0)
		*type = 2;
	if(memcmp(p, "stop", end - p) == 0)
		*type = 3;

	p = strstr(msg, "url");
	if(!p)
		goto failed;
	p = strchr(p, ':');
	p++;
	
	end = strchr(p, ';');
	memcpy(url, p, end - p);

	p = strstr(msg, "roomid");
	if(!p)
		goto failed;
	p = strchr(p, ':');
	p++;
	end = strchr(p, ';');
	memcpy(roomid, p, end - p);
	
	return 0;
failed:
	return -1;
}

int parse_ipc_msg(char *msg, int *pid, char *room){
	char *p, *end = NULL;
	char strpid[10] = {0};
	if(!msg || !pid || !room)
		goto failed;

	p = msg;
	end = strchr(p, '/');
	memcpy(strpid, p, end - p);

	*pid = atoi(strpid);

	sprintf(room, "%s", ++end);

	return 0;
failed:
	return -1;
}

int post(char *ip, int port, char *page, char *msg){  

	int fd, ret, i;
	struct sockaddr_in svr_addr;
	int on;

	memset(&svr_addr, 0, sizeof(svr_addr));
    svr_addr.sin_family = AF_INET;
    svr_addr.sin_addr.s_addr = inet_addr(ip);
	svr_addr.sin_port = htons(port);
	
	if((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		//fprintf(stdout, "socket error, errno=%s\n", strerror(errno));
		logmsg(log_error, "post socket error, errno=%s\n", strerror(errno));
		ret = -1;
        goto failed;
    }
	on = 1;
	if ((ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int))) < 0){
		//fprintf(stdout, "setsockopt(SO_REUSEADDR) failed=%d, errno=%s\n", ret, strerror(errno));
		logmsg(log_error, "post setsockopt(SO_REUSEADDR) failed=%d, errno=%s\n", ret, strerror(errno));
		goto failed;
	}
	on = 1;
	if ((ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(int))) < 0){
        //fprintf(stdout, "setsockopt(SO_REUSEPORT) failed=%d, errno=%s\n", ret, strerror(errno));
		logmsg(log_error, "post setsockopt(SO_REUSEPORT) failed=%d, errno=%s\n", ret, strerror(errno));
		goto failed;
    }
	on = 1;
    if ((ret = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(int))) < 0){
       	//fprintf(stdout, "setsockopt(TCP_NODELAY) failed=%d, errno=%s\n", ret, strerror(errno));
		logmsg(log_error, "post setsockopt(TCP_NODELAY) failed=%d, errno=%s\n", ret, strerror(errno));
        goto failed;
    }
	if((ret = connect(fd, (struct sockaddr*)&svr_addr, sizeof(svr_addr))) < 0){
		//fprintf(stdout, "connect error=%d, errno=%s\n", ret, strerror(errno));
		logmsg(log_error, "post connect error=%d, errno=%s\n", ret, strerror(errno));
		goto failed;
	}

	if((ret = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)) < 0){
		//fprintf(stdout, "fcntl(O_NONBLOCK) failed=%d, errno=%s\n", ret, strerror(errno));
		logmsg(log_error, "post fcntl(O_NONBLOCK) failed=%d, errno=%s\n", ret, strerror(errno));
        goto failed;
	}

	char content[4096] = {0};
    char content_page[50];
    sprintf(content_page,"POST /%s HTTP/1.1\r\n",page); 
	char ursagent[50] = {0};
	sprintf(ursagent,"User-Agent: */*\r\n");
	char content_host[50];
    sprintf(content_host,"Host: %s:%d\r\n",ip, port);
	char straccept[100] = {0};
    sprintf(straccept,"Accept: */*\r\n");
    char content_len[50];
    sprintf(content_len,"Content-Length: %lu\r\n", sizeof(char)*strlen(msg));
	char content_type[] = "Content-Type: application/x-www-form-urlencoded\r\n\r\n";
    sprintf(content,"%s%s%s%s%s%s",content_page,content_host,straccept,content_len,content_type,msg);
	
	if((ret = send(fd, content, sizeof(char)*strlen(content), 0)) < 0){
		logmsg(log_error, "post send error=%d, errno=%s\n", ret, strerror(errno));
		goto failed;
	}

	//fprintf(stdout, "\n=========sendlen=%d=========\n%s\n============\n", ret, content);

	logmsg(log_debug, "len=%d,send:\n%s\n", ret, content);

	memset(content, 0, 4096);
	for(i = 0; i <= 60*2*2; ++i){
		if((ret = recv(fd, content, 4096, 0)) < 0){
			if(errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN){
				usleep(500*1000);
				continue;
			}
			logmsg(log_error, "post recv error=%d, errno=%s\n", ret, strerror(errno));
			goto failed;
		}
		logmsg(log_debug, "len=%d,recv:\n%s\n", ret, content);
		break;
	}

	//fprintf(stdout, "\n---------recv-------\n%s\n-------\n", content);

 failed:
 	close(fd);
 	return ret;
}

int sendurl(char *room, char *path){
	char msg[1024] = {0};
	char page[] = "zego/mixStreamSave";
	sprintf(msg, "roomid=%s&path=%s", room, path);
    
	return post(SERVER_IP, SERVER_PORT, page, msg); 
}

int loop(){

	int ipcid = worker_fun();
	
	fd_set fdset;
    int max_fd = -1, svr_fd = -1, clt_fd = -1;
    struct timeval tv;
	int ret = -1, j = -1;
    struct sockaddr_in svr_addr;
	struct sockaddr_in clt_addr;
    socklen_t addr_len = -1;
	int clt_fds[CONCURRENT_MAX] = {0};
	int on;
	
    if((svr_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		//fprintf(stdout, "socket error, errno=%s\n", strerror(errno));
		logmsg(log_error, "loop socket error, errno=%s\n", strerror(errno));
		ret = -1;
        goto failed;
    }
	
	on = 1;
	if ((ret = setsockopt(svr_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int))) < 0){
		//fprintf(stdout, "setsockopt(SO_REUSEADDR) failed=%d, errno=%s\n", ret, strerror(errno));
		logmsg(log_error, "loop setsockopt(SO_REUSEADDR) failed=%d, errno=%s\n", ret, strerror(errno));
		goto failed;
	}

	on = 1;
	if ((ret = setsockopt(svr_fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(int))) < 0){
		//fprintf(stdout, "setsockopt(SO_REUSEPORT) failed=%d, errno=%s\n", ret, strerror(errno));
		logmsg(log_error, "loop setsockopt(SO_REUSEPORT) failed=%d, errno=%s\n", ret, strerror(errno));
		goto failed;
	}
	
	memset(&svr_addr,0,sizeof(svr_addr));
    svr_addr.sin_family = AF_INET;
    svr_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	svr_addr.sin_port = htons(1000);
	
	if((ret = bind(svr_fd, (struct sockaddr *)&svr_addr, sizeof(svr_addr))) < 0){
		//fprintf(stdout, "loop bind failed=%d, errno=%s\n", ret, strerror(errno));
		logmsg(log_error, "loop bind failed=%d, errno=%s\n", ret, strerror(errno));
        goto failed;
    }
	
	on = 1;
	if ((ret = setsockopt(svr_fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(int))) < 0){
		//fprintf(stdout, "setsockopt(TCP_NODELAY) failed=%d, errno=%s\n", ret, strerror(errno));
		logmsg(log_error, "loop setsockopt(TCP_NODELAY) failed=%d, errno=%s\n", ret, strerror(errno));
		goto failed;
	}
	
	if((ret = fcntl(svr_fd, F_SETFL, fcntl(svr_fd, F_GETFL) | O_NONBLOCK)) < 0){
		//fprintf(stdout, "fcntl(O_NONBLOCK) failed=%d, errno=%s\n", ret, strerror(errno));
		logmsg(log_error, "loop fcntl(O_NONBLOCK) failed=%d, errno=%s\n", ret, strerror(errno));
        goto failed;
	}

	if((ret = fcntl(svr_fd, F_SETFD, fcntl(svr_fd, F_GETFL) | FD_CLOEXEC)) < 0) {
		//fprintf(stdout, "fcntl(FD_CLOEXEC) failed=%d, errno=%s\n", ret, strerror(errno));
		logmsg(log_error, "loop fcntl(FD_CLOEXEC) failed=%d, errno=%s\n", ret, strerror(errno));
        goto failed;
	}

    if((ret = listen(svr_fd, BACKLOG)) < 0){
		//fprintf(stdout, "listen failed=%d, errno=%s\n", ret, strerror(errno));
		logmsg(log_error, "loop listen failed=%d, errno=%s\n", ret, strerror(errno));
        goto failed;
    }

    for(;;){

		max_fd = 0;
        tv.tv_sec = 20;
        tv.tv_usec = 0;
		
        FD_ZERO(&fdset);
        FD_SET(svr_fd, &fdset);
        if(max_fd < svr_fd){  
            max_fd = svr_fd;
        }

        for(j =0; j < CONCURRENT_MAX; j++){
            if(clt_fds[j] != 0){
                FD_SET(clt_fds[j], &fdset);
                if(max_fd < clt_fds[j]){
                    max_fd = clt_fds[j];
                }
            }
        }
		
        ret = select(max_fd + 1, &fdset, NULL, NULL, &tv);
        if(ret < 0){  
			//fprintf(stdout, "select failed=%d, errno=%s\n", ret, strerror(errno));
			if(errno != 4){
				logmsg(log_error, "loop select failed=%d, errno=%d, errno=%s\n", ret, errno, strerror(errno));
			}
            continue;
        }
        else if(ret == 0){  
            //printf("-----select time out \n");  
            continue;  
        }else{
        	int cindex = -1, i;
            if(FD_ISSET(svr_fd, &fdset)){
            	//new client
            	memset(&clt_addr,0,sizeof(clt_addr));
				addr_len = sizeof(clt_addr);
                clt_fd = accept(svr_fd, (struct sockaddr *)&clt_addr, &addr_len);  
                //fprintf(stdout, "new connection client_sock_fd = %d\n", clt_fd);
				logmsg(log_debug, "loop new connection client fd = %d\n", clt_fd);
                if(clt_fd > 0){
                    for(i = 0; i < CONCURRENT_MAX; i++){
                        if(clt_fds[i] == 0){  
                            cindex = i;  
                            clt_fds[i] = clt_fd;  
                            break;  
                        }  
                    }

                    if(cindex >= 0){
                        //fprintf(stdout, "new connection client[%d] %s:%d\n",
							//cindex, inet_ntoa(clt_addr.sin_addr), ntohs(clt_addr.sin_port));
						logmsg(log_debug, "loop new connection client[%d] %s:%d\n",
							cindex, inet_ntoa(clt_addr.sin_addr), ntohs(clt_addr.sin_port));
                    }else{
                        //memset(input_msg, 0, BUFFER_SIZE);  
                        //strcpy(input_msg, "can not add\n");  
                        //send(clt_fd, input_msg, BUFFER_SIZE, 0);  
                        //fprintf(stdout, "client add failed %s:%d\n", 
							//inet_ntoa(clt_addr.sin_addr), ntohs(clt_addr.sin_port));  
						logmsg(log_error, "loop client add failed %s:%d\n", 
							inet_ntoa(clt_addr.sin_addr), ntohs(clt_addr.sin_port));
                    }
                }  
            }  
            for(i =0; i < CONCURRENT_MAX; i++){
                if(clt_fds[i] !=0){
                    if(FD_ISSET(clt_fds[i], &fdset)){
                        //handle clt msg
                        memset(recv_msg, 0, BUFFER_SIZE);  
						long byte_num = 0;
                        byte_num = recv(clt_fds[i], recv_msg, BUFFER_SIZE, 0);  
                        if (byte_num > 0){
                            if(byte_num > BUFFER_SIZE){
                                byte_num = BUFFER_SIZE;  
                            }
                            recv_msg[byte_num] = '\0';
							char url[100] = {0};
							char roomid[100] = {0};
                            //fprintf(stdout, "\n[client] (%d):%s, len=%ld\n\n", i, recv_msg, byte_num);
							logmsg(log_info, "loop [client] (%d):%s, len=%ld\n", i, recv_msg, byte_num);
							int type = -1;
							
							if(parse_msg(recv_msg, &type, url, roomid) < 0){
								//fprintf(stdout, "parse url failed\n");
								logmsg(log_warning, "loop parse url failed\n");
								continue;
							}
							//fprintf(stdout, "<type=%d>, <url=%s>, <roomid=%s>\n", type, url, roomid);
							logmsg(log_info, "loop <type=%d>, <url=%s>, <roomid=%s>\n", type, url, roomid);

							if(type == -1 || !strlen(url) || !strlen(roomid) || strncmp(url, "rtmp", strlen("rtmp")*sizeof(char)) != 0){
								//fprintf(stdout, "recevie message invalid! continue ...\n");
								logmsg(log_warning, "loop recevie message invalid! continue ...\n");
								continue;
							}

							switch(type){
								case 1: {
										char buf[255] = {0};

										if(detecte_room_task(roomid)){
											logmsg(log_warning, "loop start roomid=%s task is already run!\n", roomid);
											break;
										}
										
										int seq = update_room_seq(roomid);
										pid_t pid = start_recod_video(roomid, url, seq); 
										add_room_task(pid, roomid);
										
										sprintf(buf, "%s", roomid);
										send_ipc_msg(ipcid, msg_update_room_seq, buf);
										memset(buf, 0, sizeof(buf));
										sprintf(buf, "%u/%s", pid, roomid);
										send_ipc_msg(ipcid, msg_add_room_task, buf);
									} break;
								case 2: {
										char buf[255] = {0};

										if(!detecte_room_task(roomid)){
											logmsg(log_warning, "loop pause roomid=%s task is not found!\n", roomid);
											break;
										}
										
										pause_recod_video(roomid);
										delete_room_task(roomid);

										sprintf(buf, "%s", roomid);
										send_ipc_msg(ipcid, msg_delete_room_task, buf);
									} break;
								case 3: {
										char buf[255] = {0};
										delete_room_seq(roomid);
										stop_recod_video(roomid);
										delete_room_task(roomid);

										sprintf(buf, "%s", roomid);
										send_ipc_msg(ipcid, msg_delete_room_task, buf);

										send_ipc_msg(ipcid, msg_delete_room_seq, buf);
									} break;
								default: break;
							}
							
                        }else if(byte_num < 0){
                            //fprintf(stdout, "[client] (%d) recieve msg failed\n", i);
							logmsg(log_error, "loop [client] (%d) recieve msg failed\n", i);
                        } else {
                            FD_CLR(clt_fds[i], &fdset);
                            clt_fds[i] = 0;
                            //fprintf(stdout, "[client](%d) logout \n", i);  
							logmsg(log_debug, "loop [client] (%d) logout \n", i);
                        }
                    }  
                }  
            }  
        }  
    }  
    return 0;
	
failed:
	return ret;
}

void add_room_task(pid_t pid, char *room){
	pid_url_node_t *node = create_node();
	node->pid = pid;
	sprintf(node->room, "%s", room);
	list_add_tail(&(node->node), &(g_list->list));
}

int detecte_room_task(char *room){
	int ret = 0;

	struct list_head *pos, *npos;
	pid_url_node_t *node;
	list_for_each_safe(pos, npos, &g_list->list){
		node = list_entry(pos, pid_url_node_t, node);

		if(!strcmp(node->room, room)){
			ret = 1;
			break;
		}
	}
	
	return ret;
}

void delete_room_task(char *room){

	struct list_head *pos, *npos;
	pid_url_node_t *node;
	list_for_each_safe(pos, npos, &g_list->list){
		node = list_entry(pos, pid_url_node_t, node);

		if(!strcmp(node->room, room)){
			list_del(pos);
			free_node(node);
			break;
		}
	}
}

pid_t init_daemon(void){

	pid_t pid = fork();  
	if(pid > 0){	   //father
		return pid;
	}else if(pid < 0 ){  //error
		//fprintf(stdout, "init_daemon() fork() error\n");
		logmsg(log_error, "init_daemon() fork() error\n");
		exit((int)-1);
	}else if(pid == 0){  //son

		if(signal(SIGINT, SIG_DFL) == SIG_ERR){
			logmsg(log_error, "init_daemon signal(SIGINT, SIG_DFL) error=%d, errno=%s\n", errno, strerror(errno));
		}
		
		struct sigaction act, oact;
		act.sa_handler = worker_signal_handle;
		sigemptyset(&act.sa_mask);
		act.sa_flags = 0;
		
		if(sigaction(SIGCHLD, &act, &oact) < 0){
			logmsg(log_error, "init_daemon sigaction(SIGCHLD, worker_signal_handle) error=%d, errno=%s\n", errno, strerror(errno));
		}

		//fprintf(stdout, "===========child work=========\n");
		logmsg(log_debug, "child work\n");
		loop();
		exit((int)0);
	}
    return pid;
}  

void cms_daemon(){
	
	pid_t pid;
    pid = fork();  
    if(pid > 0){		//father
    	//fprintf(stdout, "===========cms_daemon father return=========\n");
		logmsg(log_debug, "daemon father return\n");
		g_spid = pid;
    	return;
    }else if(pid < 0 ){	//error
    	//fprintf(stdout, "cms_daemon() fork() error\n");
		logmsg(log_error, "daemon() fork() error\n");
    	exit((int)-1);
    }else if(pid == 0){	//son

		if(signal(SIGINT, SIG_DFL) == SIG_ERR){
			logmsg(log_error, "daemon signal(SIGINT, SIG_DFL) error=%d, errno=%s\n", errno, strerror(errno));
		}
		
		struct sigaction act, oact;
		act.sa_handler = worker_signal_handle;
		sigemptyset(&act.sa_mask);
		act.sa_flags = 0;
		
		if(sigaction(SIGCHLD, &act, &oact) < 0){
			logmsg(log_error, "daemon sigaction(SIGCHLD, worker_signal_handle) error=%d, errno=%s\n", errno, strerror(errno));
		}

		//fprintf(stdout, "===========cms_daemon child work=========\n");
		logmsg(log_debug, "daemon child work\n");

		loop();
		exit((int)0);
    }
}

int update_room_seq(char *room){
	int	find = 0, seq = 0;
	struct list_head *pos, *npos;
	seq_node_t *snode = NULL;
	list_for_each_safe(pos, npos, &g_seq_list->list){
		snode = list_entry(pos, seq_node_t, node);
		if(!strcmp(snode->room, room)){
			seq = ++snode->seq;
			find = 1;
			break;
		}
	}

	if(!find){
		snode = create_seq_node();
		seq = snode->seq = 0;
		sprintf(snode->room, "%s", room);
		list_add_tail(&(snode->node), &(g_seq_list->list));
	}

	return seq;
}

void add_room_seq(int seq, char *room){

	seq_node_t *node = create_seq_node();
	node->seq= seq;
	sprintf(node->room, "%s", room);
	list_add_tail(&(node->node), &(g_seq_list->list));
	
	return;
}


void delete_room_seq(char *room){
	struct list_head *pos, *npos;
	seq_node_t *node;
	list_for_each_safe(pos, npos, &g_seq_list->list){
		node = list_entry(pos, seq_node_t, node);
		
		if(!strcmp(node->room, room)){
			list_del(pos);
			free_seq_node(node);
			break;
		}
	}
	return;
}

pid_t start_recod_video(char *room, char *input, int seq){  
    pid_t pid = fork();
    if(pid > 0){//father
		return pid;
    }else if(pid < 0 ){	//error
    	//fprintf(stdout, "start_recod_video() fork() error\n");
		logmsg(log_error, "start_recod_video() fork() error\n");
    	exit((int)-1);
    }else if(pid == 0){	//son

		signal(SIGCHLD, SIG_DFL);
		signal(SIGINT, SIG_DFL);

		char roomdir[100] = {0};
		sprintf(roomdir, "./%s", room);

		if(!opendir(roomdir)){
			if(mkdir(roomdir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0){
				//fprintf(stdout, "mkdir %s failed\n", roomdir);
				logmsg(log_error, "mkdir %s failed\n", roomdir);
				exit((int)-1);
			}
		}
		
		char txt[100] = {0};
		char fname[100] = {0};
		char *p = strrchr(input, '/');		
		p++;

		sprintf(fname, "%s", p);

		FILE *fp = NULL;
		sprintf(txt, "%s/filelist.txt", roomdir);
		fp = fopen(txt, "a+");
		if(!fp){
			//fprintf(stdout, "open %s failed\n", txt);
			logmsg(log_error, "open %s failed\n", txt);
			return 0;
		}
		fprintf(fp, "file '%02d-%s.flv'\n", seq, fname);
		fclose(fp);
		fp = NULL;

		char filename[512] = {0};
		sprintf(filename, "%s/%02d-%s.flv", roomdir, seq, fname);

		//fprintf(stdout, "\nexecl:ffmpeg -i %s -c:v copy -c:a copy -f flv -y %s\n", input, filename);
		logmsg(log_info, "execl:ffmpeg -i %s -c:v copy -c:a copy -f flv -y %s\n", input, filename);
		
		execl("./ffmpeg", 
			"ffmpeg", 
			"-i", 
			input, 
			"-c:v", 
			"copy", 
			"-c:a", 
			"copy", 
			"-f", 
			"flv",
			"-y",
			filename,
			NULL);

		exit((int)0);
    }
	return pid;
}


void stop_recod_video(char *room){
    pid_t pid = fork();
    if(pid > 0){//father
		return;
    }else if(pid < 0 ){	//error
    	//fprintf(stdout, "stop_recod_video() fork() error\n");
		logmsg(log_error, "stop_recod_video() fork() error\n");
    	exit((int)-1);
    }else if(pid == 0){	//son

		signal(SIGCHLD, SIG_DFL);

		char strpath[512] = {0};
		time_t now;  
	    time(&now);  
	    struct tm *local;  
	    local = localtime(&now);

		sprintf(strpath,"%s-%04d%02d%02d%02d%02d%02d.mp4", room, local->tm_year+1900, local->tm_mon+1,  
	                local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);
		
		pid_t spid, rpid = 0;
		int	status;
	    spid = fork();
	    if(spid > 0){//son
			rpid = waitpid(spid, &status, 0);
			if(rpid < 0){
				logmsg(log_error, "stop son wait gson spid=%u, rpid=%u, status=%d, errno=%d, errno=%s\n", 
					spid, rpid, status, errno, strerror(errno));
			}
			logmsg(log_debug, "stop son wait gson spid=%u, rpid=%u, status=%d\n", spid, rpid, status);

			//sprintf(path, "./%s.flv", room);

			char exitpath[1024] = {0};
			sprintf(exitpath, "./%s", strpath);
			if(detecte_file_exit(exitpath) == 0){
				char buf[1024] = {0};
				sprintf(buf, "%s/%s", "/fayuan/video", strpath);

				strcat(dst_path, buf);

				pid_t mvpid = 0, rmvpid = 0;
				int	mvstatus = -1;
			    mvpid = fork();
			    if(mvpid > 0){
					rmvpid = waitpid(mvpid, &mvstatus, 0);
					if(rmvpid < 0){
						logmsg(log_error, "mv file wait son pid=%u, rpid=%u, status=%d, errno=%d, errno=%s\n", 
							mvpid, rmvpid, mvstatus, errno, strerror(errno));
					}

					logmsg(log_debug, "mv file wait son pid=%u, rpid=%u, status=%d\n", mvpid, rmvpid, mvstatus);

					if(mvstatus != -1 && WIFEXITED(mvstatus) && 0 == WEXITSTATUS(status)){
						sendurl(room, buf);
						remove_dir(room);
					}
					exit((int)0);
				}else if(mvpid < 0 ){
			    	//fprintf(stdout, "stop_recod_video() gson fork() error\n");
					logmsg(log_error, "mv file fork() error\n");
			    	exit((int)-1);
			    }else if(mvpid == 0){
			    	int ret = 0;
					logmsg(log_info, "mv %s %s\n", exitpath, dst_path);
					ret = execl("/bin/mv", "mv", exitpath, dst_path, NULL);
					exit(ret);
				}
			}

			exit((int)0);
	    }else if(spid < 0 ){	//error
	    	//fprintf(stdout, "stop_recod_video() gson fork() error\n");
			logmsg(log_error, "stop_recod_video() gson fork() error\n");
	    	exit((int)-1);
	    }else if(spid == 0){	//gradeson
			char roomdir[100] = {0};
			sprintf(roomdir, "./%s", room);

			char txt[100] = {0};
			sprintf(txt, "%s/filelist.txt", roomdir);

			if(detecte_file_exit(txt) < 0){
				//fprintf(stdout, "file:[%s] is not exit!\n", txt);
				logmsg(log_warning, "file:[%s] is not exit!\n", txt);
				exit((int)0);
			}

			//fprintf(stdout, "room=%s,   roomdir=%s,  txt=%s\n", room, roomdir, txt);

			pid_t gpid, grpid = 0;
			gpid = pause_recod_video(room);
			grpid = waitpid(gpid, &status, 0);
			if(grpid < 0){
				logmsg(log_error, "stop gson wait pause gpid=%u, rpid=%u, status=%d, errno=%d, errno=%s\n", 
					gpid, grpid, status, errno, strerror(errno));
			}
			logmsg(log_debug, "stop gson wait pause gpid=%u, rpid=%u, status=%d\n", gpid, grpid, status);

			//fprintf(stdout, "\nexecl:ffmpeg -f concat -i %s -c:v copy -c:a copy -f flv -y %s\n", txt, flv);
			logmsg(log_info, "execl:ffmpeg -f concat -i %s -c:v copy -c:a copy -y %s\n", txt, strpath);
			
			execl("./ffmpeg", 
				"ffmpeg", 
				"-f",
				"concat",
				"-i",
				txt,
				"-c:v", 
				"copy", 
				"-c:a", 
				"copy",
				"-y",
				strpath,
				NULL);

			exit((int)0);
    	}
    }
}

pid_t pause_recod_video(char *room){  
    pid_t pid;

    pid = fork();
    if(pid > 0){//father
		return pid;
    }else if(pid < 0 ){	//error
    	//fprintf(stdout, "pause_recod_video() fork() error\n");
		logmsg(log_error, "pause_recod_video() fork() error\n");
    	exit((int)-1);
    }else if(pid == 0){	//son

		signal(SIGCHLD, SIG_DFL);
		
		int find = 0, i;
		char strpid[50] = {0};
		int killpid = -1;
		struct list_head *pos, *npos;
		pid_url_node_t *node;
		list_for_each_safe(pos, npos, &g_list->list){
			node = list_entry(pos, pid_url_node_t, node);

			//fprintf(stdout, "\nlist -- kill-room=%s, node->room=%s,--pid=%u\n", room, node->room, node->pid);
			
			if(!strcmp(node->room, room)){
				list_del(pos);
				find = 1;
				sprintf(strpid, "%u", node->pid);
				killpid = node->pid;
				free_node(node);
				break;
			}
		}

		if(!find || detecte_pid(strpid) != 1){
			//fprintf(stdout, "\n =========find=%d, pid=%s is not found !========\n", find, strpid);
			logmsg(log_warning, "find=%d, pid=%s is not found !\n", find, strpid);
			exit((int)0);
		}

		//fprintf(stdout, "\n---find=%d,--room=%s,--pid=%s\n", find, room, strpid);

		usleep(5*1000*1000);	//delay time

		//fprintf(stdout, "\nexecl:kill -2 %s\n", strpid);
		logmsg(log_debug, "kill(%d, SIGTERM)\n", killpid);
		if (kill(killpid, SIGTERM) < 0) {
			logmsg(log_error, "kill(%d, SIGTERM) failed, errno=%s\n", killpid, strerror(errno));
			if(errno == 3)
				exit((int)0);
	    }

		usleep(1*1000*1000);	//delay time

		if(detecte_pid(strpid) == 1){
			logmsg(log_debug, "kill(%d, SIGKILL)\n", killpid);
			if (kill(killpid, SIGKILL) < 0) {
				logmsg(log_error, "kill(%d, SIGKILL) failed, errno=%s\n", killpid, strerror(errno));
		        exit((int)-1);
		    }
		}
		exit((int)0);
    }
	return 0;
}

int remove_dir(const char *dir)
{
    char dir_name[128];
    DIR *dirp;
    struct dirent *dp;
    struct stat dir_stat;

    if (access(dir, F_OK) != 0) {
        return 0;
    }

    if (stat(dir, &dir_stat) < 0) {
		logmsg(log_error, "get directory stat failed !\n");
        return -1;
    }

    if (S_ISREG(dir_stat.st_mode)) {
        remove(dir);
    } else if (S_ISDIR(dir_stat.st_mode)) {
        dirp = opendir(dir);
        while ((dp=readdir(dirp)) != NULL){
            if ((!strcmp(".", dp->d_name)) || (!strcmp("..", dp->d_name))){
                continue;
            }

            sprintf(dir_name, "%s/%s", dir, dp->d_name);
            remove_dir(dir_name);
        }
        closedir(dirp);

        rmdir(dir);
    } else {
		logmsg(log_error, "unknow file type !\n");
    }

    return 0;
}

void worker_signal_handle(int nsignal)
{
	int	status;
	pid_t pid = 0;
	pid = waitpid(-1, &status, 0);
	if(pid < 0){
		logmsg(log_error, "worker signal_handle wait failed errno=%d, errno=%s\n", errno, strerror(errno));
	}

	//fprintf(stdout, "=========== waitpid=%u , g_spid=%u =========\n", pid, g_spid);
	logmsg(log_debug, "worker waitpid=%u, status=%d\n", pid, status);
}

void signal_handle(int nsignal)
{
	int	status;
	pid_t pid;
	pid = waitpid(-1, &status, 0);
	if(pid < 0){
		logmsg(log_error, "master signal_handle wait failed errno=%d, errno=%s\n", errno, strerror(errno));
	}

	//fprintf(stdout, "=========== waitpid=%u , g_spid=%u =========\n", pid, g_spid);
	logmsg(log_debug, "master waitpid=%u, status=%d, workerpid=%u\n", pid, status, g_spid);

	if(pid == g_spid){
		daemo_start = 1;
	}
}

int detecte_file_exit(char *path){

	if(!path)
		return -1;
	
	if(access(path, F_OK) == 0){
		return 0;
	}

	return -1;
}

int detecte_pid(char *pid){
	char cmd[100] = {0};
	char buf[512] = {0};
	FILE *pstr = NULL;
	int ret = 0;
	sprintf(cmd, "ps -p %s",pid);
	pstr = popen(cmd, "r");
	if(pstr == NULL)
		return -1;

	if(fgets(buf,512,pstr) == NULL){
		ret = -1;
		goto failed;
	}

	if(!strncmp("ERROR", buf, strlen("ERROR"))){
		ret = -1;
		goto failed;
	}

	memset((void*)buf, 0, 512);

	if(fgets(buf,512,pstr) == NULL){
		ret = 0;
	}else{
		ret = 1;
	}

failed:
	pclose(pstr);
	return ret;
}

void logmsg(enum log_level level, char* log, ...){
	char strtm[100] = {0};
	char strlevel[50] = {0};
	char msg[1024] = {0};
	char buf[1024] = {0};
	char path[500] = {0};
	FILE* pfile = NULL;
	
	va_list args;
    va_start(args, log);
    vsprintf(msg , log, args);
    va_end(args);
	
	time_t now;  
    time(&now);  
    struct tm *local;  
    local = localtime(&now);

	sprintf(strtm,"%04d-%02d-%02d %02d:%02d:%02d", local->tm_year+1900, local->tm_mon+1,  
                local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);

	sprintf(path, "mrs-%04d-%02d-%02d.log", local->tm_year+1900, local->tm_mon+1, local->tm_mday);

	switch(level){
		case log_debug:   sprintf(strlevel, "%s", "DEBUG");   break;
		case log_info: 	  sprintf(strlevel, "%s", "INFO"); 	  break;
		case log_warning: sprintf(strlevel, "%s", "WARNING"); break;
		case log_error:   sprintf(strlevel, "%s", "ERROR");   break;
		default: break;
	}
	sprintf(buf, "[%s][pid:%u][%s] %s", strtm, getpid(), strlevel, msg);
	pfile = fopen(path, "a+");
    fwrite(buf, 1, strlen(buf), pfile);
    fclose(pfile);

	return;
}

void send_ipc_msg(int id, enum ipc_msg type, char *msg){
	msg_st sdmsg;
	memset(&sdmsg, 0, sizeof(msg));
	sdmsg.type =(long)type;
	sprintf(sdmsg.text, "%s", msg);
	if (msgsnd(id, (void *)&sdmsg, sizeof(sdmsg.text), 0) == -1) {
		logmsg(log_error, "msgsnd failed :type=%ld,text=%s\n", sdmsg.type, sdmsg.text);
		//printf("[pid:%u]msgsnd failed :[%ld] %s\n", getpid(), sdmsg.type, sdmsg.text);
	}
	return;
}

int worker_fun(){
	key_t skey;
	int sid=0;

	if((skey = ftok("./mrs", 1001)) == -1) {  
        //fprintf(stderr,"[pid:%u]Creat Key Error£º%s\n", getpid(), strerror(errno));  
		logmsg(log_error, "worker Creat Key Error£º%s\n", strerror(errno));
        goto failed; 
    }
    if ((sid = msgget(skey, 0666 | IPC_CREAT)) == -1) {
		//printf("[pid:%u]msgget failed with error: %d\n", getpid(), errno);
		logmsg(log_error, "worker msgget failed errno=%s\n", strerror(errno));
        goto failed;
    }
	return sid;
failed:
	//printf("\nworker fun error\n");
	logmsg(log_error, "worker fun error\n");
	return -1;
}

void master_fun(){

	key_t rkey;
	int rid;
	msg_st msg;

restart:

	if((rkey = ftok("./mrs", 1001)) == -1) {  
        //fprintf(stderr,"Creat rKey Error£º%s\n",strerror(errno));  
		logmsg(log_error, "master Creat rKey Error£º%s\n",strerror(errno));
        exit((int)-1);  
    }
    if ((rid = msgget(rkey, 0666 | IPC_CREAT)) == -1) {
		//printf("msgget rid failed with error: %d\n", errno);
		logmsg(log_error, "master msgget rid failed errno=%s\n", strerror(errno));
        exit((int)-1);
    }

	if(daemo_start){
		cms_daemon();
		daemo_start = 0;
	}

	for(;;){
		memset(&msg, 0, sizeof(msg));
        if (msgrcv(rid, (void*)&msg, sizeof(msg.text), 0, 0) == -1) {
			if(errno == 4){

				//printf("errno=%d, ---master recv :%ld, %s---\n", errno, msg.type, msg.text);
				if (msgctl(rid, IPC_RMID, 0) == -1) {
					//printf("master msgctl(IPC_RMID) failed\n");
					logmsg(log_error, "master msgctl(IPC_RMID) readid=%d failed\n", rid);
					exit(-1);
				}

				goto restart;
			}
			//printf("master msgrcv failed with error: %s\n", strerror(errno));
			logmsg(log_error, "master msgrcv failed errno=%s\n", strerror(errno));
			usleep(500*1000);
			continue;
        }

		//msg_stat(msgid);

		//printf("---master recv :%ld, %s---\n", msg.type, msg.text);

		switch(msg.type){
			case msg_add_room_task: {
				int pid;
				char room[100] = {0};
				parse_ipc_msg(msg.text, &pid, room);
				add_room_task((pid_t)pid, room);
			} break;
			case msg_delete_room_task: {
				delete_room_task(msg.text);
			} break;
			case msg_update_room_seq: {
				update_room_seq(msg.text);
			} break;
			case msg_delete_room_seq: {
				delete_room_seq(msg.text);
			} break;
			default: break;
		}
	}

	if (msgctl(rid, IPC_RMID, 0) == -1) {
		//printf("msgctl(IPC_RMID) failed\n");
		logmsg(log_error, "master msgctl(IPC_RMID) readid=%d failed\n", rid);
		exit(-1);
	}
}


int main(int argc, char **argv){

	sprintf(dst_path, "%s", *(argv+1));
	//signal(SIGCHLD, signal_handle);
	//signal(SIGTSTP, SIG_IGN); 
	//signal(SIGHUP, SIG_IGN);
	//signal(SIGALRM, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	//signal(SIGIO, SIG_IGN);

	struct sigaction act, oact;
	act.sa_handler = signal_handle;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	if(sigaction(SIGCHLD, &act, &oact) < 0){
		logmsg(log_error, "main sigaction(SIGCHLD, signal_handle) error=%d, errno=%s\n", errno, strerror(errno));
	}

	g_list = create_list();
	g_seq_list = create_seq_list();
	
	//fprintf(stdout, "===========start=========\n");
	logmsg(log_debug, "start\n");

   	g_spid = init_daemon();

	//fprintf(stdout, "===========master work=========\n");
	logmsg(log_debug, "master work\n");

	master_fun();

	//fprintf(stdout, "=========== end =========\n");
	logmsg(log_debug, "end\n");

	return 0;
}

