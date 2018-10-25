#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>

#include "common.h"
#include "base64.h"

/*  
Rtsp basic sequence:
	OPTIONS -> DESCRIBE -> SETUP -> PLAY -> TEARDOWN 

Examples:

	OPTIONS rtsp://192.168.11.128:554/test.264 RTSP/1.0
	CSeq: 2
	User-Agent: LibVLC/3.0.2 (LIVE555 Streaming Media v2016.11.28)

	RTSP/1.0 200 OK
	CSeq: 2
	Date: Wed, Aug 15 2018 13:26:07 GMT
	Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER, SET_PARAMETER



	DESCRIBE rtsp://192.168.11.128:554/test.264 RTSP/1.0
	CSeq: 3
	User-Agent: LibVLC/3.0.2 (LIVE555 Streaming Media v2016.11.28)
	Accept: application/sdp

	RTSP/1.0 200 OK
	CSeq: 3
	Date: Wed, Aug 15 2018 13:26:08 GMT
	Content-Base: rtsp://192.168.11.128/test.264/
	Content-Type: application/sdp
	Content-Length: 550

	v=0
	o=- 1534339567387498 1 IN IP4 192.168.11.128
	s=H.264 Video, streamed by the LIVE555 Media Server
	i=test.264
	t=0 0
	a=tool:LIVE555 Streaming Media v2018.02.28
	a=type:broadcast
	a=control:*
	a=range:npt=0-
	a=x-qt-text-nam:H.264 Video, streamed by the LIVE555 Media Server
	a=x-qt-text-inf:test.264
	m=video 0 RTP/AVP 96
	c=IN IP4 0.0.0.0
	b=AS:500
	a=rtpmap:96 H264/90000
	a=fmtp:96 packetization-mode=1;profile-level-id=4D4028;sprop-parameter-sets=Z01AKJpmA8ARPy4C3AQEBQAAAwPoAADqYOhgBGMAAF9eC7y40MAIxgAAvrwXeXCg,aO44gA==
	a=control:track1



	SETUP rtsp://192.168.11.128/test.264/track1 RTSP/1.0
	CSeq: 4
	User-Agent: LibVLC/3.0.2 (LIVE555 Streaming Media v2016.11.28)
	Transport: RTP/AVP;unicast;client_port=59966-59967

	RTSP/1.0 200 OK
	CSeq: 4
	Date: Wed, Aug 15 2018 13:26:08 GMT
	Transport: RTP/AVP;unicast;destination=192.168.11.1;source=192.168.11.128;client_port=59966-59967;server_port=6970-6971
	Session: 2DA945A5;timeout=65



	PLAY rtsp://192.168.11.128/test.264/ RTSP/1.0
	CSeq: 5
	User-Agent: LibVLC/3.0.2 (LIVE555 Streaming Media v2016.11.28)
	Session: 2DA945A5
	Range: npt=0.000-

	RTSP/1.0 200 OK
	CSeq: 5
	Date: Wed, Aug 15 2018 13:26:08 GMT
	Range: npt=0.000-
	Session: 2DA945A5
	RTP-Info: url=rtsp://192.168.11.128/test.264/track1;seq=47597;rtptime=3878558061


*/

#define DEFAULT_RTSP_PORT 554
#define MAX_LISTEN_SOCK_NUM 20
#define SESSION_NUM "5C01EACD"

pthread_t m_tid=0;

int  rtpPort  =0;  //client rtp port
int  rtcpPort =0;  //client rtcp port
int  ServerRtpPort = 6880;  //server rtp port
int  ServerRtcpPort = 6881; //server  rtcp port
char ClientIP[20]={0};

int  m_rtsp_start = 0; //when rtsp finish than it should be 1 else 0

//we use this function to get first sps and pps from a 264-file 
//,then base64 decrypt ,finally use in rtsp DESCRIBE step
int getFirstSPS(char *filepath ,char **BaseStr ,int *Len)
{
	FILE *fp =NULL;
	char *pBuff =NULL;
	unsigned int fileLen=0;
	unsigned int readlen=0;
	unsigned int totalreadLen=0;
	unsigned int position =0;
	unsigned int spsPosition=0;
	unsigned int ppsPosition=0;
	unsigned int frameEndPosition=0;
	char getFrameEnd = 0; //when get sps and pps it should be 1
	char *ptr=NULL;
	char readbuf[1024];

	char spsBuff[1024]={0};
	char ppsBuff[1024]={0};
	unsigned char spsBase64Buff[128]={0};
	unsigned char ppsBase64Buff[128]={0};
	int  spsLen =0;
	char ppsLen =0;
	int  spsBase64Len=0;
	int  ppsBase64Len=0;

	if((fp=fopen(filepath,"r"))==NULL)
	{
		printf("get file error\n");	
		return -1;
	}

	//get the file len
	fseek(fp,0,SEEK_END);
	fileLen = ftell(fp);
	rewind(fp);
	
	//malloc spcae for cache one frame and the whole file 
	pBuff      = (char*)malloc(fileLen);
	memset(pBuff,0x0,fileLen);

	//get all data into file-buffer
	while(!feof(fp))
	{
		memset(readbuf,0x0,1024);
		readlen = fread(readbuf,1,1024,fp);
		memcpy(pBuff+totalreadLen,readbuf,readlen);
		totalreadLen += readlen;
	}
	
	if(totalreadLen!=fileLen)
	{
		printf("read file error");
		fclose(fp);
		return -1;
	}
	
	fclose(fp);
	ptr = pBuff;
	
	//get sps then get pps
	while(!getFrameEnd)
	{
		if(
		   ptr[position]  ==0x00 //start code
  		&& ptr[position+1]==0x00 //start code
   		&& ptr[position+2]==0x00 //start code
   		&& ptr[position+3]==0x01 //start code
   		&& (ptr[position+4]&0x1F)==NALU_T_SPS_FRAME //sps
	    )
		{
			spsPosition=position;
			position+=START_CODE_LEN;
			while(!getFrameEnd)
			{
				if(
				   ptr[position]  ==0x00 //start code
		  		&& ptr[position+1]==0x00 //start code
		   		&& ptr[position+2]==0x00 //start code
		   		&& ptr[position+3]==0x01 //start code
		   		&& (ptr[position+4]&0x1F)==NALU_T_PPS_FRAME //pps
	    		)
	    		{
	    			ppsPosition=position;
					position+=START_CODE_LEN;
					while(!getFrameEnd)
					{
						if(
						   ptr[position]  ==0x00 //start code
				  		&& ptr[position+1]==0x00 //start code
				   		&& ptr[position+2]==0x00 //start code
				   		&& ptr[position+3]==0x01 //start code
			    		)
			    		{
			    			frameEndPosition=position;
							getFrameEnd=1; //get sps and pps end
							break;
			    		}
						position++;
					}
	    		}
				position++;
			}
		}
		position++;
	}

	spsLen = ppsPosition-spsPosition-START_CODE_LEN; 
	memcpy(spsBuff,pBuff+spsPosition+START_CODE_LEN,spsLen);  
	ppsLen = frameEndPosition-ppsPosition-START_CODE_LEN; 
	memcpy(ppsBuff,pBuff+ppsPosition+START_CODE_LEN,ppsLen); 
	
	if(mbedtls_base64_encode(spsBase64Buff,sizeof(spsBase64Buff),&spsBase64Len,spsBuff,spsLen)!=0)
	{
		printf("sps base64 decrypt error\n");
	}
	
	if(mbedtls_base64_encode(ppsBase64Buff,sizeof(ppsBase64Buff),&ppsBase64Len,ppsBuff,ppsLen)!=0)
	{
		printf("pps base64 decrypt error\n");
	}

	*Len=spsBase64Len+ppsBase64Len;
	*BaseStr = malloc(*Len);
	sprintf(*BaseStr,"%s,%s",spsBase64Buff,ppsBase64Buff);
	
	
	free(pBuff);
	pBuff=NULL;
}

int CreateTcpServer(int *sockfd)
{
	//int sockfd,client_fd; 
	struct sockaddr_in my_addr; 

	if((*sockfd=socket(AF_INET,SOCK_STREAM,0))==-1)
	{ 
		printf("socket setup err\n"); 
		return -1; 
	} 

	int err,sock_reuse=1; 
	err=setsockopt(*sockfd,SOL_SOCKET,SO_REUSEADDR,(char *)&sock_reuse,sizeof(sock_reuse)); 
	if(err!=0){ 
		printf("reuse addr error\n"); 
		return -1; 
	} 

	my_addr.sin_family=AF_INET; 
	my_addr.sin_port=htons(DEFAULT_RTSP_PORT); 
	my_addr.sin_addr.s_addr=INADDR_ANY; 
	bzero(&(my_addr.sin_zero),8); 

	if(bind(*sockfd,(struct sockaddr *)&my_addr,sizeof(struct sockaddr))==-1)
	{ 
		printf("bind error\n"); 
		return -1; 
	} 

	if((listen(*sockfd,MAX_LISTEN_SOCK_NUM))==-1)
	{ 
		printf("lisen error \n"); 
		return -1; 
	} 
}

int getBuffLen(char *p)
{
	int len=0;
	char *ptmp = p;
	while(*ptmp!=0)
	{
		ptmp++;
		len++;
	}
	return len;
}

//OPTIONS
int _OPTIONS(int sockfd ,char *URL)
{
	char recBuf[1024]={0};
	char *p=NULL;
	char res[100]={0};
	char Response[500]={0};
	int Seq=0;
	int responseLen=0;

	//receive client message
	memset(recBuf,0x0,1024);
	recv(sockfd,recBuf,1024,0);
	p=recBuf;

	printf("\r\n=================================\n%s\n",p);
	
	sscanf(recBuf,"OPTIONS %[^ ] %*s",URL);
	while(*p!='\r') p++;
	p+=2;  //skip '\r\n'

	sscanf(p,"CSeq: %[0-9]",res);
	Seq=atoi(res);

	//response
	sprintf(Response,"RTSP/1.0 200 OK\r\n"
					 "CSeq: %d\r\n"
					 "Date: Mon, Jul 21 2018 17:01:56 GMT\r\n"
					 "Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY\r\n"
					 "\r\n"
					 ,Seq);
	
	responseLen = getBuffLen(Response);
	
	send(sockfd,Response,responseLen,0);
	printf("\r\n\n%s\n",Response);
	
	return 0;
}

//DESCRIBE
int _DESCRIBE(int sockfd ,char *URL ,char *BaseStr,int base64Len)
{
	char recBuf[1024]={0};
	char Response[2048]={0};
	int  HeadLen=0;
	char HeadBuf[300]={0};
	char sdpBuf[800]={0};
	int  sdpLen=0;
	char res[100]={0};
	char IP[20]={0};
	char *p=NULL;
	int  Seq=0;
	int  responseLen=0;
	
	//receive client message
	memset(recBuf,0x0,1024);
	recv(sockfd,recBuf,1024,0);
	p=recBuf;

	printf("\r\n=================================\n%s\n",p);
	
	while(*p!='\r') p++;
	p+=2;  //skip '\r\n'

	sscanf(p,"CSeq: %[0-9]",res);
	Seq=atoi(res);

	//response
	sscanf(URL,"%*[^:]://%[0-9.]",IP);
    sprintf(sdpBuf,
					"v=0\r\n"
					"o=- 1534339567387498 1 IN IP4 %s\r\n"
					"s=H.264 Video, streamed by lihaoyuan\r\n"
					"i=test.264\r\n"
					"t=0 0\r\n"
					"a=tool:lihaoyuan private test\r\n"
					"a=type:broadcast\r\n"
					"a=control:*\r\n"
					"a=range:npt=0-\r\n"
					"a=x-qt-text-nam:H.264 Video, streamed by lihaoyuan\r\n"
					"a=x-qt-text-inf:test.264\r\n"
					"m=video 0 RTP/AVP 96\r\n"
					"c=IN IP4 0.0.0.0\r\n"
					"b=AS:500\r\n"
					"a=rtpmap:96 H264/90000\r\n"
					"a=fmtp:96 packetization-mode=1;profile-level-id=4D4028;sprop-parameter-sets=%s\r\n"
					"a=control:track1\r\n"
					,IP,BaseStr);
	sdpLen = getBuffLen(sdpBuf);
	
	sprintf(HeadBuf, "RTSP/1.0 200 OK\r\n"
					 "CSeq: %d\r\n"
					 "Date: Mon, Jul 21 2014 09:07:56 GMT\r\n"
					 "Content-Base: %s\r\n"
					 "Content-Type: application/sdp\r\n"
					 "Content-Length: %d\r\n"
				 	 "\r\n"
				 	 ,Seq,URL,sdpLen);
	
	HeadLen = getBuffLen(HeadBuf);

	responseLen = HeadLen + sdpLen;
	
	memcpy(Response,HeadBuf,HeadLen);
	memcpy(Response+HeadLen,sdpBuf,sdpLen);

	send(sockfd,Response,responseLen,0);
	printf("\r\n\n%s\n",Response);
	return 0;
}


//SETUP
int _SETUP(int sockfd ,char *URL ,char ClientIP[20] ,int *ClientRtpPort ,int *ClientRtcpPort)
{
	char recBuf[1024]={0};
	char Response[500]={0};
	char res[100]={0};
	char ServerIP[20]={0};
	char *p=NULL;
	int Seq=0;
	int responseLen=0;
	int client_rtp_port =0;
	int client_rtcp_port =0;

	//receive client message
	memset(recBuf,0x0,1024);
	recv(sockfd,recBuf,1024,0);
	p=recBuf;
	//printf("client ip is %s\n",ClientIP);
	printf("\r\n=================================\n%s\n",p);
	
	while(*p!='\r') p++;
	p+=2;  //skip '\r\n'

	sscanf(p,"CSeq: %[0-9]",res);
	Seq=atoi(res);

	while(*p!=0)
	{
		if(p[0]=='p' &&
		   p[1]=='o' &&
		   p[2]=='r' &&
		   p[3]=='t' &&
		   p[4]=='=')
		{
			break;
		}
		p++;
	}
	if(*p==0)
	{
		printf("can't read client port\n");
		return -1;
	}
	p+=5; //skip "port="
	sscanf(URL,"%*[^:]://%[0-9.]",ServerIP);

	//response
	sscanf(p,"%d-%d",&client_rtp_port,&client_rtcp_port);

	sprintf(Response,
					 "RTSP/1.0 200 OK\r\n"
					 "CSeq: %d\r\n"
					 "Date: Mon, Jul 21 2014 09:07:56 GMT\r\n"
					 "Transport: RTP/AVP;unicast;destination=%s;source=%s;client_port=%d-%d;server_port=%d-%d\r\n"
					 "Session: %s;timeout=65\r\n"
					 "\r\n"
					 ,Seq,ClientIP,ServerIP,client_rtp_port,client_rtcp_port,ServerRtpPort,ServerRtcpPort,SESSION_NUM);
	
	responseLen=getBuffLen(Response);
	*ClientRtpPort  = client_rtp_port;
	*ClientRtcpPort = client_rtcp_port;
		
	send(sockfd,Response,responseLen,0);
	printf("\r\n\n%s\n",Response);
	return 0;
}


//PLAY
int _PLAY(int sockfd ,char *URL)
{
	char recBuf[1024]={0};
	char *p=NULL;
	char res[100]={0};
	char Response[500]={0};
	int Seq=0;
	int responseLen=0;
	char firstsncache[20]={0};
	char firsttscache[20]={0};

	//receive client message
	memset(recBuf,0x0,1024);
	recv(sockfd,recBuf,1024,0);
	p=recBuf;

	printf("\r\n=================================\n%s\n",p);

	sscanf(recBuf,"OPTIONS %[^ ] %*s",URL);
	while(*p!='\r') p++;
	p+=2;  //skip '\r\n'

	sscanf(p,"CSeq: %[0-9]",res);
	Seq=atoi(res);

	//response
	sprintf(firstsncache,"%d",FIRST_SERIAL_NUM);
	sprintf(firsttscache,"%d",FIRST_TIMESTAMP);
				 
	sprintf(Response,"RTSP/1.0 200 OK\r\n"
					 "CSeq: %d\r\n"
					 "Date: Mon, Jul 21 2014 09:07:56 GMT\r\n"
					 "Range: npt=0.000-\r\n"
					 "Session: %s\r\n"
					 "RTP-Info: url=%s/track1;seq=%d;rtptime=%d\r\n"
					 "\r\n"
					 ,Seq,SESSION_NUM,URL,FIRST_SERIAL_NUM,FIRST_TIMESTAMP);

	responseLen=getBuffLen(Response);
					 
	send(sockfd,Response,responseLen,0);
	printf("\r\n\n%s\n",Response);
	return 0;
}


void *RtspClientMsgHandle(void *pVoid)
{
	int  ret=-1;
	int  serverFd=0;
	int  clientFd=0;
	char URL[50]={0};
	char *base64Buff=NULL;
	int  base64Len=0;
	
	struct sockaddr_in client_addr; 
	
	//Create tcp server
	ret=CreateTcpServer(&serverFd);
	if(ret<0)
	{
		printf("create server error\n");
		return;
	}

	//Wait for Client connect
	socklen_t sin_size=sizeof(struct sockaddr_in); 
	if((clientFd=accept(serverFd,(struct sockaddr *)&client_addr,&sin_size))==-1)
	{ 
		printf("accept error\n"); 
		return;
	} 
	
	//OPTIONS
	_OPTIONS(clientFd,URL);
	
	//DESCRIBE
	getFirstSPS(MEDIA_FILE_PATH,&base64Buff,&base64Len);
	_DESCRIBE(clientFd,URL,base64Buff,base64Len);
	free(base64Buff);
	
	//SETUP
	strcpy(ClientIP,(char*)inet_ntoa(client_addr.sin_addr));
	_SETUP(clientFd,URL,ClientIP,&rtpPort,&rtcpPort); 
	
	//PLAY
	_PLAY(clientFd,URL);
	//printf("get rtp port is %d rtcp is %d\n",rtpPort,rtcpPort);
	m_rtsp_start =1;
	
	//TEARDOWN 
	//TODO
	while(1)
	{
		sleep(1);
	}
		
	//End
	close(serverFd);
	close(clientFd);
 	return;
}

int ProcessRtsp(void)
{
	int ret=0;
	ret = pthread_create(&m_tid, NULL, RtspClientMsgHandle, NULL);
    if (0 != ret)
    {
		printf("create rtsp server thread error\n");
        return -1;
    }
}


