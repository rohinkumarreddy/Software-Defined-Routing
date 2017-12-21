#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/queue.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <arpa/inet.h>
#include <string.h>


using namespace std;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define CNTRL_HEADER_SIZE 8
#define CNTRL_RESP_HEADER_SIZE 8
#define CNTRL_CONTROL_CODE_OFFSET 0x04
#define CNTRL_PAYLOAD_LEN_OFFSET 0x06
#define CNTRL_RESP_CONTROL_CODE_OFFSET 0x04
#define CNTRL_RESP_RESPONSE_CODE_OFFSET 0x05
#define CNTRL_RESP_PAYLOAD_LEN_OFFSET 0x06
#define AUTHOR_STATEMENT "I, rohinkum, have read and understood the course academic integrity policy."
#define INF 65535

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Linked List for active control connections
static struct ControlConn
{
    int sockfd;
    LIST_ENTRY(ControlConn) next;
}*connection, *conn_temp;
LIST_HEAD(ControlConnsHead, ControlConn) control_conn_list;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ssize_t recvALL(int sock_index, char *buffer, ssize_t nbytes)
{
    ssize_t bytes = 0;
    bytes = recv(sock_index, buffer, nbytes, 0);

    if(bytes == 0) return -1;
    while(bytes != nbytes)
        bytes += recv(sock_index, buffer+bytes, nbytes-bytes, 0);

    return bytes;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ssize_t sendALL(int sock_index, char *buffer, ssize_t nbytes)
{
    ssize_t bytes = 0;
    bytes = send(sock_index, buffer, nbytes, 0);

    if(bytes == 0) return -1;
    while(bytes != nbytes)
        bytes += send(sock_index, buffer+bytes, nbytes-bytes, 0);

    return bytes;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Global Variables
static uint16_t num_routers=0, upd_per_interval=0,my_ID_g=0,my_ID_row=0,from_ID_g=0,from_ID_row=0;
static uint16_t CONTROL_PORT=0,ROUTER_PORT=0,DATA_PORT=0, cost_matrix[5][5], from_cost=0;
static int control_socket=0,head_fd=0,router_socket=0,data_socket=0,timeout_val=2;
static fd_set master_list, watch_list;
static char my_IP[INET_ADDRSTRLEN];
static bool init_done=false;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct router_topology
{
	uint16_t router_ID;
        uint16_t router_port1;
        uint16_t router_port2;
        uint16_t router_cost;
        char router_IP[INET_ADDRSTRLEN];
};
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct routing_tables
{
	uint16_t router_ID;
	uint16_t padding;
        uint16_t next_hop_ID;
        uint16_t cost;
	bool neighbor;
};
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct router_field
{
	char router_IP[INET_ADDRSTRLEN];
	uint16_t router_port;
	uint16_t padding;
	uint16_t router_ID;
	uint16_t router_cost;
};
struct routing_upd_packet
{
	uint16_t num_upd;
	uint16_t src_port;
	char src_IP[INET_ADDRSTRLEN];
	struct router_field router_data[6];
};
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//global struct variables
static struct router_topology table[6];
static struct routing_tables r_table[6];
static struct timeval timeout;
static struct routing_upd_packet r_pack;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Creates and adds a control socket to list
int create_control_sock()
{
    int sock;
    struct sockaddr_in control_addr;
    socklen_t addrlen = sizeof(control_addr);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0){cerr<<"ERROR:Unable to create socket"<<endl;}

    /* Make socket re-usable */
    int num=1;
    	if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &num, sizeof(int))<0)
	{
		cerr<<"setsockopt() failed"<<endl;
	}
    bzero(&control_addr, sizeof(control_addr));

    control_addr.sin_family = AF_INET;
    control_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    control_addr.sin_port = htons(CONTROL_PORT);

    if(bind(sock, (struct sockaddr *)&control_addr, sizeof(control_addr)) < 0)
        {cerr<<"bind() failed"<<endl;}

    if(listen(sock, 5) < 0)
        {cerr<<"listen() failed"<<endl;}

    LIST_INIT(&control_conn_list);

    return sock;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void remove_control_conn(int sock_index)
{
    LIST_FOREACH(connection, &control_conn_list, next) {
        if(connection->sockfd == sock_index) LIST_REMOVE(connection, next); // this may be unsafe?
        free(connection);
    }

    close(sock_index);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int new_control_conn(int sock_index)
{
    int fdaccept;socklen_t caddr_len;
    struct sockaddr_in remote_controller_addr;

    caddr_len = sizeof(remote_controller_addr);
    fdaccept = accept(sock_index, (struct sockaddr *)&remote_controller_addr, &caddr_len);
    if(fdaccept < 0)
    {cerr<<"accept() failed"<<endl;}

    /* Insert into list of active control connections */
    connection = (ControlConn*) malloc(sizeof(struct ControlConn));
    connection->sockfd = fdaccept;
    LIST_INSERT_HEAD(&control_conn_list, connection, next);

    return fdaccept;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool isControl(int sock_index)
{
    LIST_FOREACH(connection, &control_conn_list, next)
        if(connection->sockfd == sock_index) {return true;}

    return false;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
char* create_response_header(int sock_index, uint8_t control_code, uint8_t response_code, uint16_t payload_len)
{
    char *buffer;
    char *cntrl_resp_header;

    struct sockaddr_in addr;
    socklen_t addr_size;

    buffer = (char *) malloc(sizeof(char)*CNTRL_RESP_HEADER_SIZE);
    cntrl_resp_header = buffer;

    addr_size = sizeof(struct sockaddr_in);
    getpeername(sock_index, (struct sockaddr *)&addr, &addr_size);

    // Controller IP Address
    memcpy(cntrl_resp_header, &(addr.sin_addr), sizeof(struct in_addr));
    // Control Code
    memcpy(cntrl_resp_header+CNTRL_RESP_CONTROL_CODE_OFFSET, &control_code, sizeof(control_code));
    // Response Code
    memcpy(cntrl_resp_header+CNTRL_RESP_RESPONSE_CODE_OFFSET, &response_code, sizeof(response_code));
    // Payload Length
    payload_len = htons(payload_len);
    memcpy(cntrl_resp_header+CNTRL_RESP_PAYLOAD_LEN_OFFSET, &payload_len, sizeof(payload_len));

    return buffer;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void print_cost_matrix()
{
	cout<<"-----------------Cost Matrix---------------"<<endl;
	for(int i=0;i<num_routers;i++)
	{
		for(int j=0;j<num_routers;j++)
		{
			cout<<cost_matrix[i][j]<<" ";
		}
		cout<<endl;
	}
	cout<<"-------------------------------------------"<<endl;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void find_hop_cost()
{
	for(int i=0;i<num_routers;i++)
	{
		if(r_pack.router_data[i].router_cost==0)
		{
			from_cost=r_table[i].cost;
		}
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void update_cost_matrix()
{
	for(int i=0;i<num_routers;i++)
	{
		if(my_ID_row==i){continue;}
		else if(cost_matrix[my_ID_row][i]>(from_cost+cost_matrix[from_ID_row][i]))
		{
			cost_matrix[my_ID_row][i]>(from_cost+cost_matrix[from_ID_row][i]);
		}
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void self_fill_cost_matrix()
{
	for(int i=0;i<num_routers;i++)
	{
		cost_matrix[my_ID_row][i]=r_table[i].cost;
	}
	print_cost_matrix();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void fill_cost_matrix()
{
	for(int i=0;i<num_routers;i++)
	{
		cost_matrix[from_ID_row][i]=r_pack.router_data[i].router_cost;
	}
	print_cost_matrix();
}	
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void initialize_cost_matrix()
{
	for(int i=0;i<num_routers;i++)
	{
		for(int j=0;j<num_routers;j++)
		{
			if(i==j)
			{
				cost_matrix[i][j]=0;
			}
			else
			{
				cost_matrix[i][j]=INF;
			}
		}
	}
	print_cost_matrix();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void author_response(int sock_index)
{
	uint16_t payload_len, response_len;
	char *cntrl_response_header, *cntrl_response_payload, *cntrl_response;

	payload_len = sizeof(AUTHOR_STATEMENT)-1; // Discount the NULL chararcter
	cntrl_response_payload = (char *) malloc(payload_len);
	memcpy(cntrl_response_payload, AUTHOR_STATEMENT, payload_len);

	cntrl_response_header = create_response_header(sock_index, 0, 0, payload_len);

	response_len = CNTRL_RESP_HEADER_SIZE+payload_len;
	cntrl_response = (char *) malloc(response_len);
	// Copy Header
	memcpy(cntrl_response, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
	free(cntrl_response_header);
	//Copy Payload
	memcpy(cntrl_response+CNTRL_RESP_HEADER_SIZE, cntrl_response_payload, payload_len);
	free(cntrl_response_payload);

	sendALL(sock_index, cntrl_response, response_len);

	free(cntrl_response);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void find_my_ID_global()
{
	for(int i=0;i<num_routers;i++)
	{
		if(r_table[i].cost==0){my_ID_g=r_table[i].router_ID;my_ID_row=i;break;}
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void find_from_ID_global()
{
	for(int i=0;i<num_routers;i++)
	{
		if(r_pack.router_data[i].router_cost==0){from_ID_g=r_pack.router_data[i].router_ID;from_ID_row=i;break;}
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void init_response(int sock_index, char *cntrl_payload)
{
	uint16_t payload_len=0, response_len=0;
	char *cntrl_response_header, *cntrl_response_payload, *cntrl_response;

	uint16_t num_rout=0,num_routs=0,up_per_int=0,up_per_inter=0,temp=0;int offset=0,num=0;char ip_buffer[INET_ADDRSTRLEN];
	uint32_t temp32=0;

	cntrl_response_header = create_response_header(sock_index, 1, 0, payload_len);//sock,control_code,response_code,payload_len
	response_len = CNTRL_RESP_HEADER_SIZE+payload_len;
	cntrl_response = (char *) malloc(response_len);
	bzero(cntrl_response,response_len);
	// Copy Header
	memcpy(cntrl_response, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
	sendALL(sock_index, cntrl_response, response_len);

	memcpy(&num_rout,cntrl_payload+offset,sizeof(num_routs));
	num_routs=ntohs(num_rout);//copy number of routers
	num_routers=num_routs;
	////cout<<"num of routers is "<<num_routs<<endl;
	offset+=sizeof(num_routs);
	memcpy(&up_per_int,cntrl_payload+offset,sizeof(up_per_inter));//copy update interval
	up_per_inter=ntohs(up_per_int);
	upd_per_interval=up_per_inter;
	timeout_val=upd_per_interval;
	//cout<<"timeout_val set to "<<timeout_val<<endl;
	////cout<<"periodic interval is "<<up_per_inter<<endl;
	offset+=sizeof(up_per_inter);
	while(num_routs>0)
	{
		memcpy(&temp,cntrl_payload+offset,sizeof(temp));
		table[num].router_ID=ntohs(temp);
		r_table[num].router_ID=table[num].router_ID;//copy router ID
		r_table[num].padding=0;
		////cout<<"Router ID is "<<table[num].router_ID<<endl;
		offset+=sizeof(temp);
		memcpy(&temp,cntrl_payload+offset,sizeof(temp));
		table[num].router_port1=ntohs(temp);//copy port1
		////cout<<"Router port1 is "<<table[num].router_port1<<endl;
		offset+=sizeof(temp);
		memcpy(&temp,cntrl_payload+offset,sizeof(temp));
		table[num].router_port2=ntohs(temp);//copy port2
		////cout<<"Router port2 is "<<table[num].router_port2<<endl;
		offset+=sizeof(temp);
		memcpy(&temp,cntrl_payload+offset,sizeof(temp));
		table[num].router_cost=ntohs(temp);//copy cost
		r_table[num].cost=table[num].router_cost;
		////cout<<"Router cost is "<<table[num].router_cost<<endl;

		offset+=sizeof(temp);
		memcpy(&temp32,cntrl_payload+offset,sizeof(temp32));
		//table[0].router_IP=inet_ntop(temp);
	
		struct sockaddr_in ip_addr;
	       	bzero(&ip_addr,sizeof(ip_addr));
	       	ip_addr.sin_family=AF_INET;
		ip_addr.sin_addr.s_addr=temp32;
		////cout<<"Router IP is "<<ip_buffer<<endl;
		inet_ntop(AF_INET,&(ip_addr.sin_addr),ip_buffer,INET_ADDRSTRLEN);
		strcpy(table[num].router_IP,ip_buffer);//Copy IP
		////cout<<"Router IP table[0] is "<<table[num].router_IP<<endl;
		offset+=sizeof(temp32);
		
		if(table[num].router_cost==INF)
		{
			r_table[num].next_hop_ID=INF;
			r_table[num].neighbor = false;
			//cout<<r_table[num].router_ID<<" is not a neighbor"<<endl;
		}
		else if(table[num].router_cost==0)
		{
			r_table[num].next_hop_ID = r_table[num].router_ID;
			r_table[num].neighbor = false;
			ROUTER_PORT = table[num].router_port1;
			strcpy(my_IP,table[num].router_IP);
			//cout<<r_table[num].router_ID<<" is my ID\n"<<table[num].router_port1<<" is my router_port\n"<<my_IP<<" is my IP"<<endl;	
		}
		else 
		{
			r_table[num].next_hop_ID = r_table[num].router_ID;
			r_table[num].neighbor = true;
			//cout<<r_table[num].router_ID<<" is a neighbor"<<endl;
		}
		num_routs-=1;num+=1;
			
	}//end of while

	struct sockaddr_in router_addr;
	socklen_t addrlen1 = sizeof(router_addr);

	router_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if(router_socket < 0){cerr<<"ERROR:Unable to create socket"<<endl;}

	//Make socket re-usable
	int num1=1;
    	if(setsockopt(router_socket, SOL_SOCKET, SO_REUSEADDR, &num1, sizeof(int))<0)
	{
		cerr<<"setsockopt() failed"<<endl;
	}
	bzero(&router_addr, sizeof(router_addr));
    	router_addr.sin_family = AF_INET;
	router_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    	router_addr.sin_port = htons(ROUTER_PORT);

    	if(bind(router_socket, (struct sockaddr *)&router_addr, sizeof(router_addr)) < 0)
        {cerr<<"UDP bind() failed"<<endl;}//else{cout<<"Created UDP Socket"<<endl;}
	FD_SET(router_socket, &master_list);
        if(router_socket > head_fd) {head_fd = router_socket;}
	init_done = true;
	
	initialize_cost_matrix();
	find_my_ID_global();
	self_fill_cost_matrix();

}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*void print_r_table()
{
	//cout<<"printing r_table"<<endl;
	for(int e=0;e<num_routers;e++)
	{
		//cout<<"Router ID is "<<r_table[e].router_ID<<endl;
		//cout<<"Padding is "<<r_table[e].padding<<endl;
		//cout<<"Next Hop ID is "<<r_table[e].next_hop_ID<<endl;
		//cout<<"Cost is "<<r_table[e].cost<<endl;
	}
}*/
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void bellman_ford()
{
	//cout<<"BellmanFord is running"<<endl;
	char from_IP[INET_ADDRSTRLEN];
	bzero(&from_IP,INET_ADDRSTRLEN);
	uint16_t from_port=0;
	uint16_t from_num_upd=0;
	uint16_t from_ID=0;
	uint16_t my_ID=0;
	uint16_t to_cost=0;
	uint16_t numd = num_routers,numf=0;
	uint16_t temp_dist=0;
	
	from_num_upd=r_pack.num_upd;
	from_port=r_pack.src_port;
	strcpy(from_IP,r_pack.src_IP);

	for(int r=0;r<num_routers;r++)
	{
		if(r_pack.router_data[r].router_cost==0)
		{
			from_ID=r_pack.router_data[r].router_ID;//collecting from_ID
			cout<<"from ID "<<from_ID<<endl;
		}
	}
	
	for(int i=0;i<num_routers;i++)
	{
		if(r_table[i].router_ID==from_ID)//selecting the router from which update came
		{
			to_cost=r_table[i].cost;//copying cost to reach router from which update came
			//cout<<"to_cost is "<<to_cost<<endl;
		}
		else if(r_table[i].cost==0)
		{
			my_ID=r_table[i].router_ID;
			//cout<<"myID is "<<my_ID<<endl;
		}//collecting my_ID
	}
	
	for(int j=0;j<num_routers;j++)
	{
		if(r_table[j].cost==0){continue;}
		else if(r_table[j].router_ID==from_ID){continue;}
		/*else if(r_table[j].cost==INF)
		{
			
			r_table[j].next_hop_ID=from_ID;//changing next_hop_ID
			for(int q=0;q<num_routers;q++)
			{
				if(r_table[j].router_ID==r_pack.router_data[q].router_ID)//searching for the same ID entry
				{
					temp_dist=r_pack.router_data[q].router_cost;//retrieving cost for this router from src router
					if(r_table[j].cost>(temp_dist+to_cost))
					{
						r_table[j].cost=temp_dist+to_cost;//updating cost in local r_table
						//cout<<"updated cost "<<r_table[j].cost<<endl;
						//cout<<"updated next_hop_ID "<<r_table[j].next_hop_ID<<endl;
					}
				}
			}
		}*/
		else 
		{
			for(int p=0;p<num_routers;p++)
			{
				if(r_table[j].router_ID==r_pack.router_data[p].router_ID)//searching for the same ID entry
				{
					temp_dist=r_pack.router_data[p].router_cost;//retrieving cost for this router from src router
					if(r_table[j].cost>(temp_dist+to_cost))
					{
						r_table[j].cost=temp_dist+to_cost;//updating cost in local r_table
						r_table[j].next_hop_ID=from_ID;//changing next_hop_ID
						//cout<<"updated cost "<<r_table[j].cost<<endl;
						//cout<<"updated next_hop_ID "<<r_table[j].next_hop_ID<<endl;
					}
				}
			}
			
		}
	}
	//print_r_table();
	
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void recv_routing_update()
{
	uint16_t total_len, temp_offset=0,temp_num=0,num_r=0,num=0,recv=0;
	char* recv_update_pkt, ip_buffer3[INET_ADDRSTRLEN];
	uint32_t temp23=0;
	total_len = (4+(6*num_routers))*sizeof(uint16_t);
	recv_update_pkt = (char *) malloc(total_len);
	bzero(recv_update_pkt,total_len);
	struct sockaddr_in ip_addr2;
	bzero(&ip_addr2,sizeof(ip_addr2));
	socklen_t var=sizeof(ip_addr2);
	ip_addr2.sin_family=AF_INET;
	ip_addr2.sin_addr.s_addr=htonl(INADDR_ANY);
	ip_addr2.sin_port=htons(ROUTER_PORT);
				
	recv=recvfrom(router_socket, recv_update_pkt, total_len, 0, (struct sockaddr *)&ip_addr2, &var);
	if(recv<0){cerr<<"Failed to Recieve Update!"<<endl;}
	else
	{	
		memcpy(&temp_num,recv_update_pkt+temp_offset,sizeof(uint16_t));
		temp_offset+=sizeof(uint16_t);
		//cout<<"entered recv updates"<<endl;
		num_r=ntohs(temp_num);
		r_pack.num_upd=num_r;

		//cout<<"recv'd num of fields is "<<r_pack.num_upd<<endl;

		memcpy(&temp_num,recv_update_pkt+temp_offset,sizeof(uint16_t));
		temp_offset+=sizeof(uint16_t);
		r_pack.src_port=ntohs(temp_num);
		
		//cout<<"recv'd src port is "<<r_pack.src_port<<endl;

		bzero(&ip_addr2,sizeof(ip_addr2));
		memcpy(&temp23,recv_update_pkt+temp_offset,sizeof(uint32_t));
		ip_addr2.sin_family=AF_INET;
		ip_addr2.sin_addr.s_addr=temp23;
		inet_ntop(AF_INET,&(ip_addr2.sin_addr),ip_buffer3,INET_ADDRSTRLEN);
		strcpy(r_pack.src_IP,ip_buffer3);//Copy IP
		temp_offset+=sizeof(uint32_t);
	
		//cout<<"recv'd router IP is "<<r_pack.src_IP<<endl;
		num_r=num_routers;
		while(num_r>0)
		{
			temp23=0;
	
			bzero(&ip_addr2,sizeof(ip_addr2));
			memcpy(&temp23,recv_update_pkt+temp_offset,sizeof(uint32_t));
			ip_addr2.sin_family=AF_INET;
			ip_addr2.sin_addr.s_addr=temp23;
			inet_ntop(AF_INET,&(ip_addr2.sin_addr),ip_buffer3,INET_ADDRSTRLEN);
			strcpy(r_pack.router_data[num].router_IP,ip_buffer3);//Copy IP
			temp_offset+=sizeof(uint32_t);

			//cout<<"recv'd router["<<num<<"] IP is "<<r_pack.router_data[num].router_IP<<endl;
		
			temp_num=0;
			memcpy(&temp_num,recv_update_pkt+temp_offset,sizeof(uint16_t));
			r_pack.router_data[num].router_port=ntohs(temp_num);
			temp_offset+=sizeof(uint16_t);

			//cout<<"recv'd router["<<num<<"] router_port is "<<ntohs(temp_num)<<endl;
			
			temp_num=0;
			memcpy(&temp_num,recv_update_pkt+temp_offset,sizeof(uint16_t));
			r_pack.router_data[num].padding=ntohs(temp_num);
			temp_offset+=sizeof(uint16_t);

			//cout<<"recv'd router["<<num<<"] padding is "<<ntohs(temp_num)<<endl;

			temp_num=0;
			memcpy(&temp_num,recv_update_pkt+temp_offset,sizeof(uint16_t));
			r_pack.router_data[num].router_ID=ntohs(temp_num);
			temp_offset+=sizeof(uint16_t);
			
			//cout<<"recv'd router["<<num<<"] ID is "<<ntohs(temp_num)<<endl;

			temp_num=0;
			memcpy(&temp_num,recv_update_pkt+temp_offset,sizeof(uint16_t));
			r_pack.router_data[num].router_cost=ntohs(temp_num);
			temp_offset+=sizeof(uint16_t);

			//cout<<"recv'd router["<<num<<"] cost is "<<ntohs(temp_num)<<endl;
			
			num_r-=1;num+=1;
		}
	}
	find_from_ID_global();
	find_hop_cost();
	//bellman_ford();
	fill_cost_matrix();
	update_cost_matrix();	
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void send_routing_update()
{
	uint16_t total_len, temp_offset=0,temp_num=0,num_r=num_routers,num=0,pad=0;
	int sent=0;
	uint32_t temp23=0;
	struct sockaddr_in ip_addr2;
	total_len = (4+(6*num_routers))*sizeof(uint16_t);
	char * router_update_pkt,ip_buffer3[INET_ADDRSTRLEN];
	router_update_pkt = (char *) malloc(total_len);
	bzero(router_update_pkt,total_len);
	if(init_done==true)
	{
		//cout<<"entered routing updates"<<endl;
		temp_num=htons(num_routers);
		//cout<<"Sent number of routers is "<<num_routers<<endl;
		memcpy(router_update_pkt+temp_offset,&temp_num,sizeof(uint16_t));
		temp_offset+=sizeof(uint16_t);

		temp_num=htons(ROUTER_PORT);
		memcpy(router_update_pkt+temp_offset,&temp_num,sizeof(uint16_t));
		temp_offset+=sizeof(uint16_t);

		//cout<<"Sent router port is "<<ROUTER_PORT<<endl;

		struct sockaddr_in ip_addr1;
	       	bzero(&ip_addr1,sizeof(ip_addr1));
		inet_pton(AF_INET,my_IP,&(ip_addr1.sin_addr));
		memcpy(router_update_pkt+temp_offset,&(ip_addr1.sin_addr.s_addr),sizeof(uint32_t));
		temp_offset+=sizeof(uint32_t);

		//cout<<"Sent router IP is "<<my_IP<<endl;
		
		while(num_r>0)
		{
			bzero(&ip_addr1,sizeof(ip_addr1));
			inet_pton(AF_INET,table[num].router_IP,&(ip_addr1.sin_addr));
			memcpy(router_update_pkt+temp_offset,&(ip_addr1.sin_addr.s_addr),sizeof(uint32_t));
			temp_offset+=sizeof(uint32_t);

			//cout<<"Sent router["<<num<<"] IP is "<<table[num].router_IP<<endl;

			temp_num=htons(table[num].router_port1);
			memcpy(router_update_pkt+temp_offset,&temp_num,sizeof(uint16_t));
			temp_offset+=sizeof(uint16_t);

			//cout<<"Sent router["<<num<<"] router_port1 is "<<ntohs(temp_num)<<endl;

			temp_num=htons(pad);
			memcpy(router_update_pkt+temp_offset,&temp_num,sizeof(uint16_t));
			temp_offset+=sizeof(uint16_t);

			//cout<<"Sent router["<<num<<"] padding is "<<ntohs(temp_num)<<endl;

			temp_num=htons(r_table[num].router_ID);
			memcpy(router_update_pkt+temp_offset,&temp_num,sizeof(uint16_t));
			temp_offset+=sizeof(uint16_t);

			////cout<<"Sent router["<<num<<"] router_ID is "<<ntohs(temp_num)<<endl;
			
			temp_num=htons(r_table[num].cost);
			memcpy(router_update_pkt+temp_offset,&temp_num,sizeof(uint16_t));
			temp_offset+=sizeof(uint16_t);

			////cout<<"Sent router["<<num<<"] router_cost is "<<ntohs(temp_num)<<endl;
			

			num+=1;num_r-=1;
			
		}
		num_r=num_routers;num=0;
		while(num_r>0)
		{
			if((table[num].router_cost>0)&&(table[num].router_cost<INF))
			{
		       		bzero(&ip_addr1,sizeof(ip_addr1));
				ip_addr1.sin_family=AF_INET;
				ip_addr1.sin_addr.s_addr=inet_addr(table[num].router_IP);
				ip_addr1.sin_port=htons(table[num].router_port1);
				sent=sendto(router_socket, router_update_pkt, total_len, 0, (struct sockaddr *)&ip_addr1, sizeof(ip_addr1));
				if(sendto<0){cerr<<"ERROR: sendto Failed!"<<endl;}
			}
				
			num+=1;num_r-=1;	
		}
	}
}
		/*/////////////////////////////////
		temp_num=0;temp_offset=0;num_r=5;num=0;
		memcpy(&temp_num,router_update_pkt+temp_offset,sizeof(uint16_t));
		temp_offset+=sizeof(uint16_t);
		//cout<<"entered recv updates"<<endl;

		//cout<<"recv'd1 num of fields is "<<ntohs(temp_num)<<endl;

		memcpy(&temp_num,router_update_pkt+temp_offset,sizeof(uint16_t));
		temp_offset+=sizeof(uint16_t);
		r_pack.src_port=ntohs(temp_num);
		
		//cout<<"recv'd1 src port is "<<ntohs(temp_num)<<endl;

		bzero(&ip_addr2,sizeof(ip_addr2));
		memcpy(&temp23,router_update_pkt+temp_offset,sizeof(uint32_t));
		ip_addr2.sin_family=AF_INET;
		ip_addr2.sin_addr.s_addr=temp23;
		inet_ntop(AF_INET,&(ip_addr2.sin_addr),ip_buffer3,INET_ADDRSTRLEN);
		strcpy(r_pack.src_IP,ip_buffer3);//Copy IP
		temp_offset+=sizeof(uint32_t);
	
		//cout<<"recv'd1 router IP is "<<ip_buffer3<<endl;
	
		while(num_r>0)
		{
			temp23=0;
	
			bzero(&ip_addr2,sizeof(ip_addr2));
			memcpy(&temp23,router_update_pkt+temp_offset,sizeof(uint32_t));
			ip_addr2.sin_family=AF_INET;
			ip_addr2.sin_addr.s_addr=temp23;
			inet_ntop(AF_INET,&(ip_addr2.sin_addr),ip_buffer3,INET_ADDRSTRLEN);
			temp_offset+=sizeof(uint32_t);

			//cout<<"recv'd1 router["<<num<<"] IP is "<<ip_buffer3<<endl;
		
			temp_num=0;
			memcpy(&temp_num,router_update_pkt+temp_offset,sizeof(uint16_t));
			temp_offset+=sizeof(uint16_t);

			//cout<<"recv'd1 router["<<num<<"] router_port is "<<htons(temp_num)<<endl;
			
			temp_num=0;
			memcpy(&temp_num,router_update_pkt+temp_offset,sizeof(uint16_t));
			temp_offset+=sizeof(uint16_t);

			//cout<<"recv'd1 router["<<num<<"] padding is "<<htons(temp_num)<<endl;

			temp_num=0;
			memcpy(&temp_num,router_update_pkt+temp_offset,sizeof(uint16_t));
			temp_offset+=sizeof(uint16_t);
			
			//cout<<"recv'd1 router["<<num<<"] ID is "<<htons(temp_num)<<endl;

			temp_num=0;
			memcpy(&temp_num,router_update_pkt+temp_offset,sizeof(uint16_t));
			temp_offset+=sizeof(uint16_t);

			//cout<<"recv'd1 router["<<num<<"] cost is "<<htons(temp_num)<<endl;
			
			num_r-=1;num+=1;
		}
		//////////////////////////////////////////////////////////////////////*/

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void routing_table_response(int sock_index)
{
	//build packet with routing table response payload
	uint16_t payload_len, response_len, num=num_routers, temp1=0, offset=0, num1=0;
	char *cntrl_response_header, *cntrl_response_payload, *cntrl_response;
	payload_len = (sizeof(uint16_t))*4*5; // Discount the NULL chararcter
	cntrl_response_header = create_response_header(sock_index, 2, 0, payload_len);//sock,control_code,response_code,payload_len
	cntrl_response_payload = (char *) malloc(payload_len);
	bzero(cntrl_response_payload,payload_len);
	cout<<"-------------Routing Table--------------"<<endl;
	while(num>0)
	{	
		temp1=htons(r_table[num1].router_ID);
		memcpy(cntrl_response_payload+offset, &temp1, 2);
		offset+=2;
		cout<<"ID \t\t"<<ntohs(temp1)<<endl;
		temp1=htons(r_table[num1].padding);
		memcpy(cntrl_response_payload+offset, &temp1, 2);
		offset+=2;
		cout<<"padding \t"<<ntohs(temp1)<<endl;
		temp1=htons(r_table[num1].next_hop_ID);
		memcpy(cntrl_response_payload+offset, &temp1, 2);
		offset+=2;
		cout<<"next_hop_ID \t"<<ntohs(temp1)<<endl;
		temp1=htons(r_table[num1].cost);
		memcpy(cntrl_response_payload+offset, &temp1, 2);
		offset+=2;
		cout<<"cost \t\t"<<ntohs(temp1)<<endl;
		num-=1;num1+=1;	
		cout<<"----------------------------------------"<<endl;
	}
	response_len = CNTRL_RESP_HEADER_SIZE+payload_len;
	cntrl_response = (char *) malloc(response_len);
	bzero(cntrl_response,response_len);
	//Copy Header
	memcpy(cntrl_response, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
	//Copy Payload
	memcpy(cntrl_response+CNTRL_RESP_HEADER_SIZE, cntrl_response_payload, payload_len);
	sendALL(sock_index, cntrl_response, response_len);
	////cout<<"sent routing response to controller"<<endl;

}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void update_response(int sock_index, char* cntrl_payload)
{
	
	uint16_t payload_len=0, response_len=0;
	char *cntrl_response_header, *cntrl_response_payload, *cntrl_response;
	uint16_t temp_ID=0,temp_buf=0,temp_cost=0;

	cntrl_response_header = create_response_header(sock_index, 3, 0, payload_len);//sock,control_code,response_code,payload_len
	response_len = CNTRL_RESP_HEADER_SIZE+payload_len;
	cntrl_response = (char *) malloc(response_len);
	bzero(cntrl_response,response_len);
	// Copy Header
	memcpy(cntrl_response, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
	sendALL(sock_index, cntrl_response, response_len);

	memcpy(&temp_buf, cntrl_payload, sizeof(uint16_t));
	temp_ID=ntohs(temp_buf);
	//cout<<"temp ID is "<<temp_ID<<endl;
	memcpy(&temp_buf, cntrl_payload, sizeof(uint16_t));
	temp_cost=ntohs(temp_buf);
	//cout<<"temp cost is "<<temp_cost<<endl;
	
	for(int i=0;i<num_routers;i++)
	{
		if(r_table[i].router_ID==temp_ID)
		{
			r_table[i].cost=temp_cost;
		}
	}
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool control_recv_hook(int sock_index)
{
    char *cntrl_header, *cntrl_payload;
    uint8_t control_code;
    uint16_t payload_len;

    //Get control header 
    cntrl_header = (char *) malloc(sizeof(char)*CNTRL_HEADER_SIZE);
    bzero(cntrl_header, CNTRL_HEADER_SIZE);

    if(recvALL(sock_index, cntrl_header, CNTRL_HEADER_SIZE) < 0){
        remove_control_conn(sock_index);
        free(cntrl_header);
        return false;
    }
    memcpy(&control_code, cntrl_header+CNTRL_CONTROL_CODE_OFFSET, sizeof(control_code));
    memcpy(&payload_len, cntrl_header+CNTRL_PAYLOAD_LEN_OFFSET, sizeof(payload_len));
    payload_len = ntohs(payload_len);

    free(cntrl_header);

    // Get control payload
    if(payload_len != 0){
        cntrl_payload = (char *) malloc(sizeof(char)*payload_len);
        bzero(cntrl_payload, payload_len);

        if(recvALL(sock_index, cntrl_payload, payload_len) < 0){
            remove_control_conn(sock_index);
            free(cntrl_payload);
            return false;
        }
    }

    // Triage on control_code
    switch(control_code){
        case 0: author_response(sock_index);
                break;
        case 1: init_response(sock_index, cntrl_payload);
                break;
	case 2: routing_table_response(sock_index);
		break;
	case 3: update_response(sock_index, cntrl_payload);
		break;
           
    }

    if(payload_len != 0) {free(cntrl_payload);}
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void main_loop()
{
    int selRet, sock_index, fdaccept;

    while(true)
    {
        watch_list = master_list;
	timeout.tv_sec = timeout_val;
	timeout.tv_usec=0;
        selRet = select(head_fd+1, &watch_list, NULL, NULL, &timeout);

        if(selRet < 0){cerr<<"select failed."<<endl;}
	else if(selRet==0)
	{
		//cout<<"timeout"<<endl;
		send_routing_update();
	}
	else
	{
        	// Loop through file descriptors to check which ones are ready 
    		for(sock_index=0; sock_index<=head_fd; sock_index+=1)
		{

            		if(FD_ISSET(sock_index, &watch_list))
	    		{
                		// control_socket 
                		if(sock_index == control_socket)
				{
                    			fdaccept = new_control_conn(sock_index);
                    			// Add to watched socket list 
                    			FD_SET(fdaccept, &master_list);
                    			if(fdaccept > head_fd) {head_fd = fdaccept;}
                		}

                		// router_socket 
                		else if(sock_index == router_socket)
				{
		    			//cout<<"received update"<<endl;
					recv_routing_update();
                    			//call handler that will call recvfrom() .....
                		}

                		// data_socket 
                		//else if(sock_index == data_socket){
                    		//new_data_conn(sock_index);
                		//}

                		// Existing connection 
                		else
				{
                    			if(isControl(sock_index))
		    			{
                        			if(!control_recv_hook(sock_index)) {FD_CLR(sock_index, &master_list);}
                    			}
                    			//else if isData(sock_index);
                    			else {cerr<<"Unknown socket index"<<endl;}
                		}
            		}//end of FD_ISSET
        	}//end of for(sock_index)
	}
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void init()
{////cout<<"entered init"<<endl;
    control_socket = create_control_sock();
    //router_socket and data_socket will be initialized after INIT from controller

    FD_ZERO(&master_list);
    FD_ZERO(&watch_list);

    //Register the control socket
    FD_SET(control_socket, &master_list);
    head_fd = control_socket;

    main_loop();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{////cout<<"entered main"<<endl;
	CONTROL_PORT=(uint16_t)atoi(argv[1]);
	//cout<<"Started at "<<CONTROL_PORT<<endl;
	init(); // Initialize connection manager; This will block
	return 0;
}
