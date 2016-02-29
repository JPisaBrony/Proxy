#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <netdb.h>

#define MAX_CONNECTIONS 5
#define BUFFERSIZE 256
#define CONTENTBUFFER 8192

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

int clientlist[20];
pthread_t threadlist[20];

int exitsocket(int threadsockfd, char* msg) {
    printf("%s\n", msg);
    // exit message
    printf("Client %d: closed session\n", threadsockfd);
    // close socket
    close(threadsockfd);
    return 0;
}

int logToFile(int socket, char* url, int size) {
    // open log file
    FILE *fp = fopen("proxy.log", "a+");
    // check pointer
    if(fp == NULL) {
        printf("Error opening log file");
        return 0;
    }
    // get time
    time_t t = time(NULL);
    struct tm *date = localtime(&t);
    // remove last char
    char *tmp = asctime(date);
    int len = strlen(tmp);
    tmp[len-1] = '\0';
    // get ip addr
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    getpeername(socket, (struct sockaddr *)&addr, &addr_size);
    char ip[20];
    sprintf(ip, "%d.%d.%d.%d",
        ((int)addr.sin_addr.s_addr&0xFF),
        ((int)(addr.sin_addr.s_addr&0xFF00)>>8),
        ((int)(addr.sin_addr.s_addr&0xFF0000)>>16),
        ((int)(addr.sin_addr.s_addr&0xFF000000)>>24));
    // show requests
    printf("%s: %s %s %d\n", tmp, ip, url, size);
    // write to log
    fprintf(fp, "%s: %s %s %d\n", tmp, ip, url, size);
    // close file
    fclose(fp);
    return 0;
}

int writeToCache(char* url, char* contents) {
    // open cache file
    FILE *fp = fopen(url, "w");
    // check pointer
    if(fp == NULL) {
        printf("Error opening cache file");
        return 0;
    }
    // write contents
    fprintf(fp, "%s", contents);
    // close file
    fclose(fp);
    return 0;
}

char* readFromCache(char* url) {
    char *buffer = NULL;
    int length;
    // open cache file
    FILE *fp = fopen(url, "r");
    // check pointer
    if(fp == NULL) {
        printf("Error opening cache file");
        return 0;
    }
    // get length
    fseek(fp, 0, SEEK_END);
    length = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    // allocate buffer
    buffer = malloc(length + 1);
    // read file
    fread(buffer, length, 1, fp);
    // close file
    fclose(fp);
    return buffer;
}

/*this function will listen to one socket and communicate with one client*/ 
void *onesocket (int threadsockfd) {
    int n, i, exitflag = 1, session;
    char buffer[BUFFERSIZE];
    char content_buffer[CONTENTBUFFER];
    char *message = NULL, *name = NULL, website[256];
    struct sockaddr_in server;
    struct hostent *host;
    
    while(exitflag) {
        // read from client
        n = read(threadsockfd, buffer, BUFFERSIZE-1);
        
        // error checking
        if (n < 0)
            exitsocket(threadsockfd, "Error receiving request");
        
        // check if message is EXIT
        if (strcmp (buffer, "EXIT\n") == 0) {
            // exit the loop
            exitflag = 0;
        } else {
            // create new socket
            session = socket(AF_INET, SOCK_STREAM, 0);
            // check if session was created properly
            if (session < 0) 
                return exitsocket(threadsockfd, "Error opening socket");
            
            // get the name of the website
            name = strtok(buffer, " ");
            name = strtok(NULL, " ");
            name = strtok(name, "/");
            name = strtok(NULL, "/");
            // check if user input proper string
            if(name != NULL) {
                sprintf(website, "%s", name);
            } else {
                char* msg = "Invalid request, try\nGET http://www.google.com/\n";
                write(threadsockfd, msg, strlen(msg));
                return exitsocket(threadsockfd, "No host defined");
            }
            
            // get hostname
            host = gethostbyname(website);
            // check if hostname exists
            if (host == NULL)
                return exitsocket(threadsockfd, "Error, can't get hostname");
            
            // zero server variable
            bzero((char *) &server, sizeof(server));
            // set address family
            server.sin_family = AF_INET;
            // set ip address of host
            bcopy((char *)host->h_addr, (char *)&server.sin_addr.s_addr, host->h_length);
            // set ip address of host
            server.sin_port = htons(80);
            // connect
            if (connect(session, (struct sockaddr *) &server,sizeof(server)) < 0)
                return exitsocket(threadsockfd, "Error connecting from proxy");
            // check if website is in the cache
            if(access(website, F_OK) == -1) {
                // create message to send
                asprintf(&message, "GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", website);
                // send message
                write(session, message, strlen(message));
                // read reply
                read(session, content_buffer, CONTENTBUFFER-1);
                // write to cache
                writeToCache(website, content_buffer);
            } else {
                // read from cache
                char* cachedata = readFromCache(website);
                sprintf(content_buffer, "%s", cachedata);
                free(cachedata);
            }
            
            // send responds to client
            write(threadsockfd, content_buffer, strlen(content_buffer));
            // close current socket
            close(session);
            // log
            logToFile(threadsockfd, website, strlen(content_buffer));
        }
    }
    // exit message
    printf("Client %d: closed session\n", threadsockfd);
    // close current socket
    close(threadsockfd);
    return 0;
}

int main(int argc, char *argv[]) {
    int sockfd, newsockfd, port, n, exitflag = 1, cli = 0, i;
    socklen_t client_len;
    struct sockaddr_in server, client;
    
    // disable stdout buffering
    setbuf(stdout, NULL);
    
    // check arguments
    if (argc < 2) {
        fprintf(stderr,"ERROR, no port provided\n");
        exit(1);
    }
    // create new socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    // check if socket was opened properly
    if (sockfd < 0) 
        error("ERROR opening socket");
    
    // zero server variable
    bzero((char *) &server, sizeof(server));
    // get port number from command line
    port = atoi(argv[1]);
    // set address family
    server.sin_family = AF_INET;
    // set ip address of server
    server.sin_addr.s_addr = INADDR_ANY;
    // set ip address of host
    server.sin_port = htons(port);
    
    // bind socket to address
    if (bind(sockfd, (struct sockaddr *) &server, sizeof(server)) < 0)
        error("ERROR on binding");
    
    // while user hasn't exited
    while(exitflag) {
        // listen for connections
        listen(sockfd, MAX_CONNECTIONS);
        // get size of client address
        client_len = sizeof(client);
        // create new socket
        newsockfd = accept(sockfd, (struct sockaddr *) &client, &client_len);
        // error checking
        if (newsockfd < 0)
            error("ERROR on accept");
        
        printf("create new socket: %d\n", newsockfd);
        // thread variable
        pthread_t thread;
        // create new thread
        pthread_create(&thread, NULL, onesocket, newsockfd);
        // add thread to the thread list
        threadlist[cli] = thread;
        // add client to client list
        clientlist[cli] = newsockfd;
        // incement client number
        cli++;
    }
    // close socket
    close(sockfd);
    // close threads
    for(i = 0; i < 20; i++)
        pthread_join(threadlist[i],NULL);
    
    return 0;
}
