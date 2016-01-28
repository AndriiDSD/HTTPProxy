/*
 * proxy.c - A Simple Sequential Web proxy
 *
 * Course Name: 14:332:456-Network Centric Programming
 * Assignment 2
 * Student Name: Andrii Hlyvko
 * 
 * This program acts as a proxy between the browser and the server. This proxy
 * takes client requests and passes them to the server. If the server responds the proxy
 * forwards this response to the client and logs the information about the request in a log
 *file named proxyLog.txt. The argument to this proxy is the port number that it 
 *will be listening to and the version that should be run(-thread or -process).
 */ 
#include <stdlib.h>
#include <ctype.h>
#include "csapp.h"
#include <stdio.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>

/*
 * Function prototypes
 */
int setup_client_socket();
void handleProcessRequests(int,int,struct sockaddr_in);
void *handleThreadRequests(void*);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
int strToInt();
int getPortNum(char *);
void saveLog(char *);
void getInfo(char *,char **,char **,int *,char **,char **);

pthread_t thread;
sem_t threadMutex;//a mutex to be used by threads
sem_t processMutex;//a mutex to be used by processes
/* 
 * main - Main routine for the proxy program 
 * Inside main the socket for this proxy is initialized. After the socket is 
 *initialized this method calls the handleProcessRequest or the 
 *handleThreadRequest function that will handle
 * individual client requests depending on flag provided.
 */
struct req{//struct to be passed as argument to pthread_create as function argument
  int proxy;//socket that this proxy is listening to
  int client;//individual client socket descriptor
  struct sockaddr_in browsr;//address of the client browser
};
int main(int argc, char **argv)
{
  
  /* Check the number of arguments */
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <port number> -thread(or -process)\n", argv[0]);
    exit(1);
  }
  int proxyPort=strToInt(argv[1]); // the first argument is the proxy port
  if(proxyPort<1024||proxyPort>65535) //if proxy port is not in the range of valid ports quit the program
    {
      perror("./concurrent-proxy:Non Valid Proxy Port\n");
      exit(1);
    }
  if(strcmp(argv[2],"-process")!=0 && strcmp(argv[2],"-thread")!=0)
    {
      fprintf(stderr, "Usage: %s <port number> -thread(or -process)\n", argv[0]);
      exit(1);
    }
  /////////////////////////Set up connection to client
  int proxyFD=setup_client_socket();
  
  struct sockaddr_in proxyAddr; //set up the client connection
  bzero(&proxyAddr,sizeof(proxyAddr));
  proxyAddr.sin_family=AF_INET;
  proxyAddr.sin_addr.s_addr=htonl(INADDR_ANY);
  proxyAddr.sin_port=htons(proxyPort);
  
  if(bind(proxyFD,(struct sockaddr *)&proxyAddr,sizeof(proxyAddr))==-1)//bind the socket to address
    {
      fprintf(stderr,"./concurrent-proxy:bind %s\n", strerror (errno));
      close(proxyFD);
      exit(1);
    }
  if(listen(proxyFD,5)==-1)//listen to requests
    {
      fprintf(stderr,"./concurrent-proxy:listen %s\n", strerror (errno));
      close(proxyFD);
      exit(1);
    }

  sem_init(&threadMutex,0,1);//mutex for threads
  sem_init(&processMutex,1,1);//mutex for processes

  struct req* arg;
  while(1)
    {      
      struct sockaddr_in browser;//address of the client browser
      socklen_t browserLen;
      int clientFD=-1;
      pid_t processID;
      int status;
      bzero(&browser,sizeof(browser));
      clientFD=accept(proxyFD,(struct sockaddr *)&browser,&browserLen);//accept connection to current client
      if(clientFD==-1)
	{
	  fprintf(stderr,"./concurrent-proxy:accept(main) %s\n", strerror (errno));
	  exit(1);
	}
      if(strcmp(argv[2],"-process")==0)
	{
	  if((processID=fork())==0)//create child process
	    {
	      handleProcessRequests(proxyFD,clientFD,browser);//handle individual client requests
	      exit(0);
	    }
	  close(clientFD);
	}
      else if(strcmp(argv[2],"-thread")==0)
	{
	  arg= (struct req*)malloc(sizeof(struct req));
	  arg->proxy = proxyFD;
	  arg->client=clientFD;
	  arg->browsr=browser;
	  int stat=pthread_create(&thread,NULL,handleThreadRequests,arg);//create a new thread 
	  if(stat){
	    printf("./concurrent-proxy return code from pthread_create() is %d\n", stat);
	    exit(-1);
	  }
	}
      else
	{
	  fprintf(stderr,"Usage: %s <port number> -thread(or -process)\n",argv[0]);
	  exit(1);
	}
      
    }
  close(proxyFD);
  return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
		      char *uri, int size)
{
  time_t now;
  char time_str[MAXLINE];
  unsigned long host;
  unsigned char a, b, c, d;
  
  /* Get a formatted time string */
  now = time(NULL);
  strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));
  /* 
   * Convert the IP address in network byte order to dotted decimal
   * form. Note that we could have used inet_ntoa, but chose not to
   * because inet_ntoa is a Class 3 thread unsafe function that
   * returns a pointer to a static variable (Ch 13, CS:APP).
   */
  host = ntohl(sockaddr->sin_addr.s_addr);
  a = host >> 24;
  b = (host >> 16) & 0xff;
  c = (host >> 8) & 0xff;
  d = host & 0xff;
  
  /* Return the formatted log entry string */
  sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri,size);
}


/*
 *This method converts a string to int. It is used to 
 *convert the proxy port argument.
 * @args char *st - the string to be converted.
 */
int strToInt(char *st)
{
  int x=0;
  if(st==NULL)
    return -1;
  if(*st=='\0')
    return -1;
  char *ch;
  for(ch=st;*ch!='\0';ch++)
    {
      if(!isdigit(*ch))
	x=-1;
    }
  if(x!=-1)
    x=(int)atoi(st);
  return x;
}


/*
 *saveLog method takes a string as a parameter and saves it in the proxy log file on disc.
 *It is used to log all clients requests in a file named proxyLog.txt.
 */
void saveLog(char *log)
{
  if(log==NULL)
    return;
  if(*log=='\0')
    return;
  FILE *fd=fopen("proxyLog.txt","a");//open log file in append mode
  if(fd==NULL)
    return;
  for(;*log!='\0';log++)
    {
      fputc(*log,fd);
    }
  fputc('\n',fd);
  fputc('\n',fd);
  fclose(fd);
}

/*
 *getInfo method is user for parsing the url. It extracts the method, server name, server port,
 * url, and http version from the client request.
 * @args char *request- the http request of the client.
 * @args char **command - location in memory where the http method will be saved.
 * @args char **servr - location in memory where the server name will be saved.
 * @args int *portNum - the port number of the destination.
 * @args char ** url - location in memory where the url will be saved.
 * @args char **version - location in memory where the version of http request will be saved.
 */
void getInfo(char *request,char **command,char **servr,int *portNum,char **url,char **version)
{
  char *cmd=(char *)malloc(sizeof(char));
  char *server=(char *)malloc(sizeof(char));
  char *uri=(char *)malloc(sizeof(char));
  if (cmd==NULL||server==NULL)         //check if allocation failed
    {
      fprintf(stderr,"getCommand: %s\n",strerror(errno));
	exit(1);
    }
  //extracr the command
  int count=0;
  for(;*request!='\0';request++)
    {
      if((int)*request==32)//got to space
	{
	break;
	}
      cmd[count]=*request;
      count++;
      char *temp=realloc(cmd,(count+1)*sizeof(char));
     if(temp==NULL)                     //check if malloc failed
       {
	 fprintf(stderr,"getCommand: %s\n",strerror(errno));
	 exit(1);
       }
	cmd=temp;

    }
  if(*request=='\0')
    {
      perror("./concurrent-proxy: Incorrect HTTP format.\n");
    }
  cmd[count]='\0';
  *command=cmd;
  request++;//skip space
  //check if have http:// and skip it
  if(*(request)=='h'&*(request+1)=='t'&*(request+2)=='t'&*(request+3)=='p'&*(request+4)==':'&*(request+5)=='/')
    request=request+7;
  //extract server name
  count=0;
  for(;*request!='\0';request++)
    {
      if(*request=='/'||*request==':')// /=47or :=58
	{
	break;
	}
      server[count]=*request;
      count++;
      char *temp=realloc(server,(count+1)*sizeof(char));
     if(temp==NULL)                     //check if malloc failed
       {
	 fprintf(stderr,"getCommand: %s\n",strerror(errno));
	 exit(1);
       }
	server=temp;
    }
  if(*request=='\0')
    {
      perror("./concurrent-proxy: Incorrect HTTP format.\n");
    }
  server[count]='\0';
  *servr=server;
  //extract port
  if(*request==58)//got to : character
    {
      *request++;
      int x=0;
      for(;*request!=47;request++)
	{
	  if(*request==' '||*request=='\0')
	    break;
	  x=x+(*request-48);
	  x=x*10;
	}
        if(*request=='\0')
	  {
	    perror("./concurrent-proxy: Incorrect HTTP format.\n");
	  }
      x=x/10;
      *portNum=x;
    }
  else
    *portNum=80;
  
  //now extract the url
   count=0;
  for(;*request!=' ';request++)
    {
      if(*request=='\0')
	break;
      uri[count]=*request;
      count++;
      char *temp=realloc(uri,(count+1)*sizeof(char));
     if(temp==NULL)                     //check if malloc failed
       {
	 fprintf(stderr,"getCommand: %s\n",strerror(errno));
	 exit(1);
       }
	uri=temp;
    }
  if(*request=='\0')
    {
      perror("./concurrent-proxy: Incorrect HTTP format.\n");
    }
  uri[count]='\0';
  *url=uri;

  //extract the http version
  
    char *httpV=(char *)malloc(sizeof(char));
    request++;
    count=0;
    for(;*request!='\0';request++)
    {
      if(*request=='\r'||*request=='\n')
	break;
      httpV[count]=*request;
      count++;
      char *temp=realloc(httpV,(count+1)*sizeof(char));
     if(temp==NULL)                     //check if malloc failed
       {
	 fprintf(stderr,"getCommand: %s\n",strerror(errno));
	exit(1);
       }
	httpV=temp;

    }
  httpV[count]='\0';
  *version=httpV;
}


/**
 *This method creates a socket for the client that uses this proxy.
 * @return int socket descriptor to be used by this proxy.
 */
int setup_client_socket()
{
  int proxyFD=0;
	if((proxyFD=socket(AF_INET,SOCK_STREAM,0))==-1)//set up a socket to listen to requests form browser
	  {
	    fprintf(stderr,"./concurrent-proxy:socket %s\n", strerror (errno));
	    exit(1);
	  }
	int yes=1;
	if(setsockopt(proxyFD,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes))==-1)//reuse address
	  {
	    perror("setsockopt\n");//failed to set socket options
	    exit(1);
	  }
	return proxyFD;
}

/*
 *handleProcessRequests function takes a client socket descriptor, the proxy socket, and the sockaddr of 
 *browser as arguments and processes
 *the individual clients requests.
 * @args int proxyFD - is the socket descriptor of this proxy.
 * @args clientFD - is the socket descriptor of individual client
 * @args browser - is the sockaddr of the client browser
 */
void handleProcessRequests(int proxyFD,int clientFD,struct sockaddr_in browser)
{
	  char request[10000];
	  recv(clientFD,request,sizeof(request),0);
	  if(strncmp(request,"GET",3)!=0)//close if the request is not get
	    {
	      // close(clientFD);
	      return;//continue listening
	    }
	  
	  char *command,*serverName,*url,*httpVersion;//extract all the required information
	  int serverPort=-1;                          //to send request to server
	  getInfo(request,&command,&serverName,&serverPort,&url,&httpVersion);//parses the URL
	  if(*command=='\0'||*serverName=='\0'||*url=='\0'||serverPort<=0)
	    {
	      perror("./concurrent-proxy: Erorr parsing url\n");
	      // close(proxyFD);
	      exit(1);
	    }
	  
	  struct hostent *dest;
	  dest=gethostbyname(serverName); //get the destenation address
	  if(dest==NULL) //check if the server address is correct
	    {
	      fprintf(stderr,"./proxy:gethostbyname %s\n", strerror (errno));
	      exit(1);
	    }
	  int destFD;
	  struct sockaddr_in destAddr;
	  if((destFD=socket(AF_INET,SOCK_STREAM,0))<0)//open a socket to the destenation
	    {
	      fprintf(stderr,"./concurrent-proxy:socket %s\n", strerror (errno));
	      exit(1);
	    }
	  bzero(&destAddr,sizeof(destAddr));
	  destAddr.sin_family=AF_INET;
	  destAddr.sin_addr.s_addr=htonl(INADDR_ANY);//
	  bcopy((char*)dest->h_addr,(char*)&destAddr.sin_addr.s_addr,dest->h_length);
	  destAddr.sin_port=htons(serverPort);
	  if(connect(destFD,(struct sockaddr*)&destAddr,sizeof(destAddr))<0)  //connect to destenation
	    {
	      fprintf(stderr,"./concurrent-proxy:connect %s\n", strerror (errno));
	      exit(1);
	    }
	  char httpRequest[strlen(command)+strlen(url)+strlen(httpVersion)+10+strlen(serverName)];
	  sprintf(httpRequest,"%s %s %s\r\nHost:%s\r\n\r\n",command,url,httpVersion,serverName);
	  int n=write(destFD,httpRequest,strlen(httpRequest));
	  if (n < 0) 
	    perror("ERROR writing to socket");
	  char resp[15000];
	  int bytesRecieved=0;
	  while((n=recv(destFD,resp,sizeof(resp),0))>0)//keep recieving the htlm file
	    {
	      send(clientFD,resp,n,0); //sent the current buffer to client
	      bytesRecieved=bytesRecieved+n;
	    }
	  if(bytesRecieved>0)//if there was a response make a log entry
	    {
	      sem_wait(&processMutex);//try dectementing sepaphore
	      char *htp="http://www.";//start saving the log entry
	      char *logUrl=(char*)malloc(sizeof(char)*MAXLINE);
	      strcat(logUrl,htp);                        //by concatinating server name and url
	      strcat(logUrl,serverName);
	      strcat(logUrl,url);
	      strcat(logUrl,"\0");
	      char logEntry[MAXLINE];//try to save to the log file
	      format_log_entry(logEntry,&browser,logUrl,bytesRecieved);
	      saveLog(logEntry);
	      sem_post(&processMutex);//increment semaphore
	    }	  
}

/*
 *This function handles individual client requests. It is called when a new thread is created inside main.
 *@args ptr should be a pointer to a struct req.
 */
void *handleThreadRequests(void *ptr)
{
  
  struct req* arg= (struct req*)ptr;
  int proxyFD=arg->proxy;
  int clientFD=arg->client;
  struct sockaddr_in browser=arg->browsr;
  free(ptr);
  //if(pthread_detach(pthread_self())!=0)
  // {
  //  fprintf(stderr,"./concurrent-proxy: failed to detach thread\n");
  // }
  char request[10000];
  recv(clientFD,request,sizeof(request),0);
  if(strncmp(request,"GET",3)!=0)//close if the request is not get
    {
      return NULL;//continue listening
    }
  
  char *command,*serverName,*url,*httpVersion;//extract all the required information
  int serverPort=-1;                          //to send request to server
  getInfo(request,&command,&serverName,&serverPort,&url,&httpVersion);//parses the URL
  if(*command=='\0'||*serverName=='\0'||*url=='\0'||serverPort<=0)
    {
      perror("./proxy: Erorr parsing url\n");
      // close(proxyFD);
      return NULL;
    }
  
  struct hostent *dest;
  dest=gethostbyname(serverName); //get the destenation address
  if(dest==NULL) //check if the server address is correct
    {
      fprintf(stderr,"./concurrent-proxy:gethotbyname %s\n", strerror (errno));
      return NULL;
    }
  int destFD;
  struct sockaddr_in destAddr;
  if((destFD=socket(AF_INET,SOCK_STREAM,0))<0)//open a socket to the destenation
    {
      fprintf(stderr,"./proxy:socket: %s\n", strerror (errno));
      return NULL;
    }
  bzero(&destAddr,sizeof(destAddr));
  destAddr.sin_family=AF_INET;
  destAddr.sin_addr.s_addr=htonl(INADDR_ANY);//
  bcopy((char*)dest->h_addr,(char*)&destAddr.sin_addr.s_addr,dest->h_length);
  destAddr.sin_port=htons(serverPort);
  if(connect(destFD,(struct sockaddr*)&destAddr,sizeof(destAddr))<0)  //connect to destenation
    {
      fprintf(stderr,"./proxy:connect %s\n", strerror (errno));
      return NULL;
    }
  char httpRequest[strlen(command)+strlen(url)+strlen(httpVersion)+10+strlen(serverName)];
  sprintf(httpRequest,"%s %s %s\r\nHost:%s\r\n\r\n",command,url,httpVersion,serverName);
  int n=write(destFD,httpRequest,strlen(httpRequest));
  if (n < 0) 
    perror("ERROR writing to socket");
  char resp[15000];
  int bytesRecieved=0;
  while((n=recv(destFD,resp,sizeof(resp),0))>0)//keep recieving the htlm file
    {
      send(clientFD,resp,n,0); //sent the current buffer to client
      bytesRecieved=bytesRecieved+n;
    }
  if(bytesRecieved>0)//if there was a response make a log entry
    {
      sem_wait(&threadMutex);
      char *htp="http://www.";//start saving the log entry
      //char logUrl[MAXLINE];
      char *logUrl=(char*)malloc(sizeof(char)*MAXLINE);
      strcat(logUrl,htp);                        //by concatinating server name and url
      strcat(logUrl,serverName);
      strcat(logUrl,url);
      strcat(logUrl,"\0");
      char logEntry[MAXLINE];//try to save to the log file
      format_log_entry(logEntry,&browser,logUrl,bytesRecieved);
      saveLog(logEntry);
      sem_post(&threadMutex);
    }
  close(clientFD);
  return NULL;
}
