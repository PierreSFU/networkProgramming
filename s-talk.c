/* // Name studentNumber //** GIT privacy EDIT

Note: 

**** outputing to a txt.file gives me random garbage !!!! ****


- Beej's guide was used to help learn how to properly use sockets
	- "https://beej.us/guide/bgnet/" and/or https://github.com/beejjorgensen/bgnet

-  Some code was used from Brian Fraser as shown below
	- By Brian Fraser, Modified from Linux Programming Unleashed (book)
*/

// General
#include <stdio.h>
#include <stdlib.h>
#include <string.h>			// for strncmp()
#include <unistd.h>			// for close() and sleep
#include <pthread.h>
#include <stdbool.h>
#include <time.h>
//-

// Server
#include <sys/types.h>
#include <sys/socket.h>	
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h> // For inet_addr() - Which wasn't used but we'll keep this here
//-

#include "list.h"

#define MSG_MAX_LEN 1024


static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; // Initializes mutex
static pthread_mutex_t mutexFake = PTHREAD_MUTEX_INITIALIZER; // Initializes mutex we don't need a lock for
static pthread_cond_t condLockServer = PTHREAD_COND_INITIALIZER;
static pthread_cond_t condLockClient = PTHREAD_COND_INITIALIZER;


// Possible Race Conditions & Global variables

	// Sender
	static int PORT;

	static char remote_name[MSG_MAX_LEN]; // IP from args
	static char remote_port[MSG_MAX_LEN]; // PORT from args
	
	// For await_UDP_datagram
	static int socketDescriptor = 0;

	static struct sockaddr_in senderInfo; //** senderInfo stores information from who sent the packet
	static unsigned int senderLength = 0;

	// List tests
	List* listForServer; // Items server is recieving
	List* listForClient; // Items client is recieving

	// Note: this has too much memory for what we're doing
	// But I left it here because with a simple wait/signal condition
	// we could potentially hold x amount of mail if for some reason
	// we couldn't print to the screen right away
	static char memorySpaceServer[LIST_MAX_NUM_NODES][MSG_MAX_LEN];
	static char memorySpaceClient[LIST_MAX_NUM_NODES][MSG_MAX_LEN];
	
	// Threads
	static pthread_t keyboardInputThread;
	static pthread_t udpDatagramThread;
	static pthread_t printScreenThread;
	static pthread_t sendDataThread;

	// For sockFD
	static int sockfd;

	// For while loops
	static bool notReadyToExit = 1;


// Joins all threads
void threadJoin()
{
	pthread_join(keyboardInputThread, NULL);
	pthread_join(udpDatagramThread, NULL);
	pthread_join(printScreenThread, NULL);
	pthread_join(sendDataThread, NULL);
}

// Cancels all threads
void threadCancel()
{
	pthread_cancel(keyboardInputThread);
	pthread_cancel(printScreenThread);
	pthread_cancel(sendDataThread);
	pthread_cancel(udpDatagramThread);
}

// Triggered when we begin leaving
void cleanUp()
{	
	// Stop listening, Stop sending and cancel threads
	threadCancel();
	close(sockfd);
	close(socketDescriptor);
	
	// Make sure nothing is stuck because of mutexes
	pthread_mutex_unlock(&mutex);
	pthread_cond_signal(&condLockServer);
	pthread_cond_signal(&condLockClient);
	putchar('!'); // Stop waiting for STDIN due to Read()

	sleep(1);

	// Double check nothing is stuck because of mutexes 
	pthread_mutex_unlock(&mutex);
	pthread_cond_signal(&condLockServer);
	pthread_cond_signal(&condLockClient);
	
	// Free lists we were using, may abort weirdly
	// but I have no control over that

	if(List_count(listForServer) > 0){
		//printf("Deleted List Server\n");
		free(listForServer);
	}	
	if(List_count(listForClient) > 0){
		//printf("Deleted List Client\n");
		free(listForClient);
	}
}


// Waits for Server to type something
void* await_keyboard_input() 
{
	while (notReadyToExit) 
	{
		char mailServerHolder[MSG_MAX_LEN] = "";
		long cleaner = 1;

		cleaner = read(0, mailServerHolder, MSG_MAX_LEN);
		if(cleaner <= 0) {
			notReadyToExit = 0;
		}

		// This is required to remove the newline
		// NOTE: this may cause errors with your test function
		if(mailServerHolder[strlen(mailServerHolder) - 1] == '\n'){
			mailServerHolder[strlen(mailServerHolder) - 1] = 0;
		}

		// Is list full? - Not necessary for our implementation
		if(List_count(listForClient) >= LIST_MAX_NUM_NODES){
			//printf("We can't hold anymore space!!");
		}

		// RACE CONDITION!
		pthread_mutex_lock(&mutex);
		{
			strcpy(memorySpaceClient[List_count(listForClient)], mailServerHolder);
			List_prepend(listForClient, &memorySpaceClient[List_count(listForClient)]);
		}
		pthread_mutex_unlock(&mutex);
		// END RACE CONDITION

		// Necessary if-statement
		if(notReadyToExit == 1){
			pthread_cond_signal(&condLockServer);
			pthread_cond_wait(&condLockServer, &mutexFake);
		}
		
	}

	//pthread_join(keyboardInputThread, NULL);
	pthread_exit(NULL);
}


// Await message from remote client and store it into listForServer
// UDP INPUT
void* await_UDP_datagram()
{
	// Get address from sender
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;                   // Connection may be from network
	sin.sin_addr.s_addr = htonl(INADDR_ANY);    // Host to Network long, //** Willing to work on any address
	sin.sin_port = htons(PORT);                 // Host to Network short
	
	// Create the socket for UDP ALSO checks for errors
	if( (socketDescriptor = socket(PF_INET, SOCK_DGRAM, 0)) == -1){ // Left this as PF_INET on purpose
		//printf("\nError in socket\n");
		notReadyToExit = 0;
	}

	// Bind the socket to the port (PORT) that we specify ALSO check for error
	if(bind (socketDescriptor, (struct sockaddr*) &sin, sizeof(sin)) != 0) //** Bind connects me to the port for UDP
	{
		//printf("\nBind error");
		notReadyToExit = 0;
	}

	while (notReadyToExit) {
		// Will change sin (the address) to be the address of the client.
		senderLength = sizeof(senderInfo);
		char messageRx[MSG_MAX_LEN]; //**  Create our buffer size of MSG_MAX_LEN

		int bytesRx = recvfrom(socketDescriptor,  //**  recvfrom populates senderInfo, tells us who sent msg
			messageRx, MSG_MAX_LEN, 0, //** Fill buffer with whatever data came from the network
			(struct sockaddr *) &senderInfo, &senderLength);

		// Better some data than none, but let's just say something happened
		if(bytesRx > MSG_MAX_LEN)
		{
			//printf("File somehow too big");
		}

		// Make it null terminated (so string functions work):
		int terminateIdx = (bytesRx < MSG_MAX_LEN) ? bytesRx : MSG_MAX_LEN - 1;
		messageRx[terminateIdx] = 0;  //** Put a null at the end so we know when the message ends

   		// sends message if possible
		if(List_count(listForServer) >= LIST_MAX_NUM_NODES){
			//printf("This only occurs if the list is full, which is impossible");
		}
		else
		{
			//printf("\nBytes: %d\n", List_count(listForServer));

			// RACE CONDITIONS!
			pthread_mutex_lock(&mutex);
			
				strcpy(memorySpaceServer[List_count(listForServer)], messageRx);
				List_prepend(listForServer, memorySpaceServer[0]);
			
			pthread_cond_signal(&condLockClient);
			pthread_cond_wait(&condLockClient, &mutex);

			pthread_mutex_unlock(&mutex);
			//- END RACE CONDITION
		}

	}

	close(socketDescriptor);
	
	//pthread_join(udpDatagramThread, NULL);
	pthread_exit(NULL);
}


// UDP output to client the data + message
void* send_data()
{
	while(notReadyToExit)
	{
		//* RACE CONDITION
		pthread_cond_wait(&condLockServer, &mutexFake);

		pthread_mutex_lock(&mutex);

			List_last(listForClient); // Point to last item in the list

			char *temp_mail = List_remove(listForClient);

			
			for(int i = 0; i < List_count(listForClient); i++)
			{
				strcpy(memorySpaceClient[i],memorySpaceClient[i+1]);
			}
		
		pthread_mutex_unlock(&mutex);
		//- END RACE CONDITION
		
		// Initialize what we need to send data
		struct addrinfo hints, *servinfo;
		int errCheck;
		
		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		
		// Error checking
		if ((getaddrinfo(remote_name, remote_port, &hints, &servinfo)) != 0){
			//printf("get addr error");
			notReadyToExit = 0;
		}
		if ((sockfd = socket(servinfo->ai_family, SOCK_DGRAM, 0)) == -1){
			//printf("sockFD error");
			notReadyToExit = 0;
		}
		if (servinfo == NULL){
			//printf("Another fun error in addrinfo");
			notReadyToExit = 0;
		}
		

		// Concatenate whatever we want
		char testMessageTx[strlen(temp_mail)];
		if(strcmp(temp_mail,"!") == 0){
			sprintf(testMessageTx, "%s - User has left the session", temp_mail);
		}
		else{
			sprintf(testMessageTx, "%s", temp_mail);  //-Creates a string to reply with
		}

		testMessageTx[(strlen(temp_mail)+1)] = 0;
		senderLength = sizeof(senderInfo);  //- Remember senderInfo is our information from who it came from
		if ((errCheck = sendto(sockfd, testMessageTx, strlen(testMessageTx), 0,
				servinfo->ai_addr, servinfo->ai_addrlen)) == -1){
			//printf("Error sendto");
			notReadyToExit = 0;
		}

		freeaddrinfo(servinfo);

		if(strcmp(temp_mail,"!") == 0){
			//printf("We're leaving!");
			notReadyToExit = 0;
		}
		
		close(sockfd);

	pthread_cond_signal(&condLockServer);
	}

	//pthread_join(sendDataThread, NULL);
	pthread_exit(NULL);
}


// Take message off of one of the list and prints it to the SERVER
void* print_to_screen()
{
	

	while(notReadyToExit)
	{
		
		//* Race condition
		pthread_mutex_lock(&mutex);
		pthread_cond_wait(&condLockClient, &mutex);

			char messageFromClient[strlen(List_last(listForServer))];
			List_last(listForServer);

			strcpy(messageFromClient, List_remove(listForServer));

			// Remove extra line so we can do an actual comparison
			if(messageFromClient[strlen(messageFromClient) - 1] == '\n'){
				messageFromClient[strlen(messageFromClient) - 1] = 0;
			}

			// We are ready to exit
			if(strcmp(messageFromClient,"!") == 0){
				sleep(1); // NOTE: this is part of my exit routine CHeCK
				notReadyToExit = 0;
			}

			char myBuffer[MSG_MAX_LEN];
			strcpy(myBuffer, messageFromClient);
			
			// Outputs to screen
			write(1, myBuffer, sizeof(messageFromClient));

			// Copy to our memory location
			for(int i = 0; i < List_count(listForServer); i++){
				strcpy(memorySpaceServer[i],memorySpaceServer[i+1]);
			}

		//- End Race condition
		pthread_cond_signal(&condLockClient); // was in loop
		pthread_mutex_unlock(&mutex);

	}

	//pthread_join(printScreenThread, NULL);
	pthread_exit(NULL);
}


// Creates threads
void threadCreate()
{
	// So we can cancel instantly
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	
	pthread_create(&printScreenThread, NULL, print_to_screen, NULL);
	pthread_create(&sendDataThread, NULL, send_data, NULL);
	pthread_create(&udpDatagramThread, NULL,await_UDP_datagram, NULL);
	pthread_create(&keyboardInputThread, NULL,await_keyboard_input , NULL);
}


// Creates lists
void createLists()
{
	listForServer = List_create();
	listForClient = List_create();
}


int main(int argCount, char** args)
{
	// Gather necessary information from input arguments
	PORT = atoi(args[1]);
	sprintf(remote_name, "%s", args[2]);
	sprintf(remote_port, "%s", args[3]);

	createLists();

	threadCreate();

	// Loops until we are ready to exit, either an "!" was inputted
	// OR we received an error somewhere else
	while(notReadyToExit); 

	cleanUp();

	return 0;
}
