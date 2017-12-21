#include<iostream>
#include<stdio.h>
#include<stdlib.h>
#include<mainloop.h>
#include<unistd.h>
#include<cstring>
#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<strings.h>
#include<string.h>

#include<control_header_lib.h>

#define AUTHOR_STATEMENT "I, rohinkum, have read and understood the course academic integrity policy."

using namespace std;

struct router_topology
{
	uint16_t router_ID;
        uint16_t router_port1;
        uint16_t router_port2;
        uint16_t router_cost;
        uint32_t router_IP;
};

struct __attribute__((__packed__)) INIT_PAYLOAD
    {
        uint16_t router_ID;
        uint16_t router_port1;
        uint16_t router_port2;
        uint16_t router_cost;
        uint32_t router_IP;
    };

ssize_t recvALL(int sock_index, char *buffer, ssize_t nbytes)
{
    ssize_t bytes = 0;
    bytes = recv(sock_index, buffer, nbytes, 0);

    if(bytes == 0) return -1;
    while(bytes != nbytes)
        bytes += recv(sock_index, buffer+bytes, nbytes-bytes, 0);

    return bytes;
}

ssize_t sendALL(int sock_index, char *buffer, ssize_t nbytes)
{
    ssize_t bytes = 0;
    bytes = send(sock_index, buffer, nbytes, 0);

    if(bytes == 0) return -1;
    while(bytes != nbytes)
        bytes += send(sock_index, buffer+bytes, nbytes-bytes, 0);

    return bytes;
}

char* create_response_header(int sock_index, uint8_t control_code, uint8_t response_code, uint16_t payload_len)
{
	char *buffer;
	//BUILD_BUG_ON(sizeof(struct CONTROL_RESPONSE_HEADER) != CNTRL_RESP_HEADER_SIZE); 
        struct CONTROL_RESPONSE_HEADER *cntrl_resp_header;
    
	struct sockaddr_in addr;
	socklen_t addr_size;

	buffer = (char *) malloc(sizeof(char)*CNTRL_RESP_HEADER_SIZE);

	cntrl_resp_header = (struct CONTROL_RESPONSE_HEADER *) buffer;

	addr_size = sizeof(struct sockaddr_in);
	getpeername(sock_index, (struct sockaddr *)&addr, &addr_size);
	/* Controller IP Address */
	memcpy(&(cntrl_resp_header->controller_ip_addr), &(addr.sin_addr), sizeof(struct in_addr));
	/* Control Code */
	cntrl_resp_header->control_code = control_code;
	/* Response Code */
	cntrl_resp_header->response_code = response_code;
	/* Payload Length */
	cntrl_resp_header->payload_len = htons(payload_len);

	return buffer;
}

void author_response(int sock_index)
{
	uint16_t payload_len, response_len;
	char *cntrl_response_header, *cntrl_response_payload, *cntrl_response;

	payload_len = sizeof(AUTHOR_STATEMENT)-1; // Discount the NULL chararcter
	cntrl_response_payload = (char *) malloc(payload_len);
	memcpy(cntrl_response_payload, AUTHOR_STATEMENT, payload_len);

	cntrl_response_header = create_response_header(sock_index, 0, 0, payload_len);//sock,control_code,response_code,payload_len

	response_len = CNTRL_RESP_HEADER_SIZE+payload_len;
	cntrl_response = (char *) malloc(response_len);
	/* Copy Header */
	memcpy(cntrl_response, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
	free(cntrl_response_header);
	/* Copy Payload */
	memcpy(cntrl_response+CNTRL_RESP_HEADER_SIZE, cntrl_response_payload, payload_len);
	free(cntrl_response_payload);

	sendALL(sock_index, cntrl_response, response_len);

	free(cntrl_response);
}

void init_response(int sock_index, char *cntrl_payload)
{
	char num_router[2],upd_per_inter[2],*cntrl_response_header,*cntrl_response_payload, *cntrl_response;
	uint16_t num_routers=0,upd_per_interval=0,response_len;
	struct router_topology recv_table [5];
	uint16_t payload_len = 0;
	//parse incoming data
	//memcpy(num_routers,&cntrl_payload,sizeof(num_routers));
	
	/*update it's own routing table and send the response*/
	memcpy(&num_routers,cntrl_payload,sizeof(num_routers));	
	//num_routers=atoi(num_router);
	cout<<num_routers<<endl;
	memcpy(&upd_per_interval,cntrl_payload+16,sizeof(upd_per_inter));
	//upd_per_interval=atoi(upd_per_inter);
	struct INIT_PAYLOAD *init_payload = (struct INIT_PAYLOAD *) (cntrl_payload+32);
	recv_table[0].router_ID=init_payload->router_ID;
	recv_table[0].router_port1=init_payload->router_port1;
	recv_table[0].router_port2=init_payload->router_port2;
	recv_table[0].router_cost=init_payload->router_cost;
	recv_table[0].router_IP=init_payload->router_IP;
	struct INIT_PAYLOAD *init_payload1 = (struct INIT_PAYLOAD *) (cntrl_payload+32+32*3);
	recv_table[1].router_ID=init_payload1->router_ID;
	recv_table[1].router_port1=init_payload1->router_port1;
	recv_table[1].router_port2=init_payload1->router_port2;
	recv_table[1].router_cost=init_payload1->router_cost;
	recv_table[1].router_IP=init_payload1->router_IP;
	struct INIT_PAYLOAD *init_payload2 = (struct INIT_PAYLOAD *) (cntrl_payload+32+32*3*2);
	recv_table[2].router_ID=init_payload2->router_ID;
	recv_table[2].router_port1=init_payload2->router_port1;
	recv_table[2].router_port2=init_payload2->router_port2;
	recv_table[2].router_cost=init_payload2->router_cost;
	recv_table[2].router_IP=init_payload2->router_IP;
	struct INIT_PAYLOAD *init_payload3 = (struct INIT_PAYLOAD *) (cntrl_payload+32+32*3*3);
	recv_table[3].router_ID=init_payload3->router_ID;
	recv_table[3].router_port1=init_payload3->router_port1;
	recv_table[3].router_port2=init_payload3->router_port2;
	recv_table[3].router_cost=init_payload3->router_cost;
	recv_table[3].router_IP=init_payload3->router_IP;
	struct INIT_PAYLOAD *init_payload4 = (struct INIT_PAYLOAD *) (cntrl_payload+32+32*3*4);
	recv_table[4].router_ID=init_payload4->router_ID;
	recv_table[4].router_port1=init_payload4->router_port1;
	recv_table[4].router_port2=init_payload4->router_port2;
	recv_table[4].router_cost=init_payload4->router_cost;
	recv_table[4].router_IP=init_payload4->router_IP;
	cntrl_response_header = create_response_header(sock_index, 1, 1, payload_len);
	//sendALL(sock_index, cntrl_response, response_len);

	response_len = CNTRL_RESP_HEADER_SIZE+payload_len;
	cntrl_response = (char *) malloc(response_len);
	/* Copy Header */
	memcpy(cntrl_response, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
	free(cntrl_response_header);
	/* Copy Payload 
	memcpy(cntrl_response+CNTRL_RESP_HEADER_SIZE, cntrl_response_payload, payload_len);
	free(cntrl_response_payload);*/

	sendALL(sock_index, cntrl_response, response_len);

	free(cntrl_response);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int mainloop(int control_port)
{
	cout<<"\t==========\n\t| ROUTER |\n\t==========\nRunning........\nListening for controller at port "<<control_port<<endl;
	int control_sock=0,head_fd=0;
	fd_set master_list, watch_list;

	//router_socket and data_socket will be initialized after INIT from controller
	FD_ZERO(&master_list);
	FD_ZERO(&watch_list);

	struct sockaddr_in control_addr;
    	socklen_t addrlen = sizeof(control_addr);

	control_sock = socket(AF_INET, SOCK_STREAM, 0);//creating a socket for controller port using TCP

    	if(control_sock < 0)
	{
		cerr<<"socket() failed"<<endl;
	}

	//Make socket re-usable
	//if(setsockopt(control_sock, SOL_SOCKET, SO_REUSEADDR, (int[]){1}, sizeof(int)) < 0){cerr<<"setsockopt() failed"<<endl;}
	int num=1;
    	if(setsockopt(control_sock, SOL_SOCKET, SO_REUSEADDR, &num, sizeof(int))<0)
	{
		cerr<<"setsockopt() failed"<<endl;
	}

	bzero(&control_addr, sizeof(control_addr));
	control_addr.sin_family = AF_INET;
	control_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	control_addr.sin_port = htons(control_port);//assigning control port from command line arguments

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	if(bind(control_sock, (struct sockaddr *)&control_addr, sizeof(control_addr)) < 0)
	{
		cerr<<"bind() failed"<<endl;
	}
	//bind and listen
	if(listen(control_sock, 5) < 0)
	{
		cerr<<"listen() failed"<<endl;
	}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//Register the control socket
	FD_SET(control_sock, &master_list);
	if(head_fd<control_sock)
	{
		head_fd = control_sock;
	}
	int selret=0, sock_index=0, fdaccept=0;
	socklen_t caddr_len=0;

	struct sockaddr_in remote_controller_addr;
	caddr_len = sizeof(remote_controller_addr);

	char *cntrl_header, *cntrl_payload;
	uint8_t control_code;
	uint16_t payload_len;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    	for(;;)
	{
	        watch_list = master_list;
	        selret = select(head_fd+1, &watch_list, NULL, NULL, NULL);
	        if(selret < 0)
		{
			cerr<<"select failed."<<endl;
		}

        	//Loop through file descriptors to check which ones are ready
        	for(sock_index=0; sock_index<=head_fd; sock_index+=1)
		{
			if(FD_ISSET(sock_index, &watch_list))
			{
        	        	//control_socket
        	        	if(sock_index == control_sock)
				{
					fdaccept = accept(sock_index, (struct sockaddr *)&remote_controller_addr, &caddr_len);
					if(fdaccept < 0)
					{
						cerr<<"accept() failed"<<endl;
					}
					//Add to watched socket list
					FD_SET(fdaccept, &master_list);
        	            		if(fdaccept > head_fd)
					{
						head_fd = fdaccept;
					}
        	        	}

/*
        		        //router_socket
        		        else if(sock_index == router_socket)
				{
	      			        //call handler that will call recvfrom() .....
                		}

                		 //data_socket
                		else if(sock_index == data_socket)
				{
	                    		//new_data_conn(sock_index);
                		}

*/


                		//Existing connection
                		else
				{

    					/* Get control header */
					cntrl_header = (char *) malloc(sizeof(char)*CNTRL_HEADER_SIZE);
    					bzero(cntrl_header, CNTRL_HEADER_SIZE);

    					if(recvALL(sock_index, cntrl_header, CNTRL_HEADER_SIZE) < 0)
					{
	        				//remove_control_conn(sock_index);
	        				free(cntrl_header);
						close(sock_index);
						FD_CLR(sock_index, &master_list);
	        				//return FALSE;
	    				}

    					/* Get control code and payload length from the header */
        				//BUILD_BUG_ON(sizeof(struct CONTROL_HEADER) != CNTRL_HEADER_SIZE);
					if(sizeof(struct CONTROL_HEADER)!=CNTRL_HEADER_SIZE)
					{
						cerr<<"sizeof(struct CONTROL_HEADER) != CNTRL_HEADER_SIZE"<<endl;
					}

        				struct CONTROL_HEADER *header = (struct CONTROL_HEADER *) cntrl_header;
        				control_code = header->control_code;
        				payload_len = ntohs(header->payload_len);
					free(cntrl_header);
					
					if(payload_len != 0)
					{
	        				cntrl_payload = (char *) malloc(sizeof(char)*payload_len);
	        				bzero(cntrl_payload, payload_len);
        					if(recvALL(sock_index, cntrl_payload, payload_len) < 0)
						{
        				    		//remove_control_conn(sock_index);
        				    		free(cntrl_payload);
							close(sock_index);
							FD_CLR(sock_index, &master_list);
        				    		//return FALSE;
        					}
    					}

					/* Triage on control_code */
    					switch(control_code)
					{
        					case 0: author_response(sock_index);
         		       				break;
        					case 1: init_response(sock_index, cntrl_payload);
         		       				break;
            					/*.........
        					....... 
         					......*/
    					}

    					free(cntrl_payload);
                    			//else if isData(sock_index);
                		}//end of if sock_index == control_sock

            		}
        	}//end of for select
    	}//end of for(;;)
	return 0;
}//end of mainloop
