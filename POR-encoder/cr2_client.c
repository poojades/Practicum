#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include "eccwrapper.h"
#include "encwrapper.h"

#define v 1024/32
#define w 4096/32
#define LEN 16;
#define BLOCK_SIZE 32;
#define q 1000;

static unsigned char * kjc;
static unsigned char * buf;
unsigned char * u_temp;
unsigned int * u;

void initChal(unsigned char * kchal, unsigned char* kind, long t, int w, int v)
{
	// generate kjc for all challenges from kchal
	keygen_init();
	seeding(kchal);
	int length = LEN * q;
	keygen(kjc, length);

	//compute u for all challenges from kind
	
	keygen_init();
	seeding(kind);
	keygen(u_temp, len);
	
	u = (*(unsigned int *)u_temp);
	for(int k=0;k<q;k++)
	{	
		u[k] = u[k] % w;
	}

	int i,j;
	for (j=0;j<q;j++)
	{
		i=0;
		buf[j*20+i] = (unsigned char)(j+1);
		for(i=4;i<20;i++)
		{
			buf[j*20+i] = kjc[i];
		}
		buf[j*20+i] = (unsigned char)u[j];
	}
}

int verify(char * response, unsigned char * kje)
{
	// Split response into Mj and Qj
	int l;
	char * Mj, Qj;
	int l=0;
	for(m=0;m<BLOCK_SIZE;m++)
	{
		Mj[m] = response[l];
		l++;
	}
	for(m=0;m<BLOCK_SIZE;m++)
	{
		Qj[m] = response[l];
		l++;
	}
		
	// calculate DecQj	
	char * DecQj = malloc(BLOCK_SIZE);
	enc_init(kje);
 	decrypt(Qj,DecQj);
	
	// compare Mj and DecQj
	int correct=1;
	int i;
	for(i=0;i<BLOCK_SIZE;i++)
	{
		if(Mj[i] == DecQj[i]) 
		{
			correct = 1;
		}
		else
		{
			correct = 0;
			break;
		}
	}	
	if(correct == 0)
		return 0;
	else
		return 1;
}

main()
{
// socket programming -  to send j, kjc and u, to the server

	if((kjc = malloc(q * LEN))==NULL)
	{ 
		fprintf(stderr, "failed to allocate memory for kjc.\n");
	}

	unsigned long len = q * sizeof(unsigned int);
	
	if(u_temp = malloc(q * sizeof(unsigned char) * sizeof(unsigned int)))==NULL)
	{ 
		fprintf(stderr, "failed to allocate memory for utemp.\n");
	}

	if(u = malloc(q * sizeof(unsigned int)))==NULL)
	{ 
		fprintf(stderr, "failed to allocate memory for u.\n");
	}

	if((buf = malloc(q* (2*sizeof(unsigned int) + LEN)))==NULL)
	{ 
		fprintf(stderr, "failed to allocate memory for buf.\n");
	}

	unsigned char * challenge_buffer;

	int sockfd ;
	struct sockaddr_in	serv_addr;
	int i;
	char * message;
	char * response;

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("Unable to create socket\n");
		exit(0);
	}

	serv_addr.sin_family		= AF_INET;
	serv_addr.sin_addr.s_addr	= inet_addr("127.0.0.1");
	serv_addr.sin_port		= htons(4000);

	if ((connect(sockfd, (struct sockaddr *) &serv_addr,
						sizeof(serv_addr))) < 0) {
		printf("Unable to connect to server\n");
		exit(0);
	}

	for(i=0; i < 100; i++) message[i] = '\0';
	recv(sockfd, message, 100, 0);
	// prints - connected to server
	printf("%s\n", message);

	float epsilon = 0.0;
	int counter_verified_incorrect = 0;
	
	init_challenge(kchal, kind, q, t, w, v);

	// for loop for each challenge

	int i = 0;
	for(j=0;j<q;j++)
	{
		if((challenge_buffer = malloc(2*sizeof(unsigned int) + LEN))==NULL)
		{ 
			fprintf(stderr, "failed to allocate memory for buf.\n");
		}
		
		int k;
		for(k=0;k<24;k++) // size of challenge = 24
		{
			challenge_buffer[k] = buf[i];
		}
		i+=24;
		// sends challenge to server
		send(sockfd, challenge_buffer, (2*sizeof(unsigned int) + LEN), 0);

		//receive response from server
		recv(sockfd, response, 64, 0);

		// some random kje - CHANGE
		unsigned char * kje = "xyz";
		int v= verify(response, kje);

		if(v == 0)
		{
			printf("Verifed not correct");
			counter_verified_incorrect++;
		}
		else
		{
			printf("Verified correct");
			counter_verified_correct++;
		}
		free(challenge_buffer);
	}

	epsilon = counter_verified_incorrect/(counter_verified_incorrect + counter_verified_correct);
	printf("Epsilon = %f",epsilon);

	close(sockfd);
	free(kjc);
	free(u_temp);
	free(u);
	free(buf);
}

