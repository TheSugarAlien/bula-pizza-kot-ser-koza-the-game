#include<stdio.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<string.h>
#include <arpa/inet.h>
#include <fcntl.h> // for open
#include <unistd.h> // for close
#include<pthread.h>

char client_message[2000];
char buffer[1024];
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void *socketThread(void *arg) {
    int newSocket = *((int *)arg);
    pthread_mutex_lock(&lock);

    int client_id = rand();
    printf("Klient otrzymał ID: %d\n", client_id);

    char id_message[50];
    snprintf(id_message, sizeof(id_message), "Twoje ID: %d\n", client_id);
    send(newSocket, id_message, strlen(id_message), 0);

    pthread_mutex_unlock(&lock);

    int n;
    while ((n = recv(newSocket, client_message, sizeof(client_message), 0)) > 0) {
        printf("Klient %d przesłał wiadomość: %s\n", client_id, client_message);
        send(newSocket, client_message, strlen(client_message), 0);
        memset(client_message, 0, sizeof(client_message));
    }

    printf("Klient %d rozłączył się\n", client_id);
    close(newSocket);
    pthread_exit(NULL);
}


int main(){
  int serverSocket, newSocket;
  struct sockaddr_in serverAddr;
  struct sockaddr_storage serverStorage;
  socklen_t addr_size;

  //Create the socket.
  serverSocket = socket(PF_INET, SOCK_STREAM, 0);

  // Configure settings of the server address struct
  // Address family = Internet
  serverAddr.sin_family = AF_INET;

  //Set port number, using htons function to use proper byte order
  serverAddr.sin_port = htons(1100);

  //Set IP address to localhost
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);


  //Set all bits of the padding field to 0
  memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero);

  //Bind the address struct to the socket
  bind(serverSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr));

  //Listen on the socket
  if(listen(serverSocket,50)==0)
    printf("Listening\n");
  else
    printf("Error\n");
    pthread_t thread_id;

    while(1)
    {
        //Accept call creates a new socket for the incoming connection
        addr_size = sizeof serverStorage;
        newSocket = accept(serverSocket, (struct sockaddr *) &serverStorage, &addr_size);

        if( pthread_create(&thread_id, NULL, socketThread, &newSocket) != 0 )
           printf("Failed to create thread\n");

        pthread_detach(thread_id);
        //pthread_join(thread_id,NULL);
    }
  return 0;
}