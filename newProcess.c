/* 
*  This file is write for rtp learning ,user can compile this file 
*  ,and use the execute obj to transport H264 file to a specific 
*  machine where have an VLC software on it .And have to say that 
*  when i test this file ,i use the enviroment as big-endian ,and
*  i trust it could be used in little-endian .But if error happen 
*  ,you may fistly consider when enviroment meets error or other else.
*  What's more ,In such a H264 file ,normally it firstly comes a
*  start code and then comes a sps frame then comes a pps frame ,
*  if your file is not this rank ,you may modify getFirstSPS this function
*  to fix to you rank.
*  Further more ,this file have two mode :
*      1) simple rtp 
*      2) rtsp
*      User can use "USE_RTSP" switch to choice whether use rtp mode or rtsp
*      When define USE_RTSP ,means using rtsp mode:
*          1)running this utility
*          2)OPEN YOUR VLC NETWORK-STREAMING WINDOW ,enter URL like:
*            rtsp://192.168.50.10/test.264
*      When no define USE_RTSP ,means using rtp mode:
*          1)running this utility with VLC sofeware address as argment
*          2)OPEN YOUR VLC FILE-STREANING WINDOW ,choose your sdp file and then 
*            press PLAY.
*
*  Copyright@lihaoyuan 2018-08-28
*sa
*  Email: lhy09027@126.com
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/sem.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <linux/random.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "common.h"

#ifdef USE_RTSP
extern int  rtpPort;        //client  rtp  port
extern int  rtcpPort;       //client  rtcp port
extern int  ServerRtpPort;  //server  rtp  port
extern int  ServerRtcpPort; //server  rtcp port
extern char ClientIP[20];
extern int  m_rtsp_start;
#endif

//get local ip ,here we use eth3 
void getLocalIP(char ip[32])
{
    int inet_sock;  
    struct ifreq ifr;   

    inet_sock = socket(AF_INET, SOCK_DGRAM, 0);  
    strcpy(ifr.ifr_name, IFNAME);  
    ioctl(inet_sock, SIOCGIFADDR, &ifr);  
    strcpy(ip, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
	
}

int createUdpSocket(void)
{
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd == -1)
	{
		return -1;
	}
	return sockfd;
}

int initClientAddr(struct sockaddr_in *ClientAddr,char *clientIP)
{
    memset(ClientAddr,0,sizeof(struct sockaddr_in));
 #ifndef USE_RTSP
    ClientAddr->sin_port=htons(RTP_MEDIA_TRANSPORT_PORT);
 #else
 	ClientAddr->sin_port=htons(rtpPort);
 #endif
    ClientAddr->sin_family = AF_INET;
    inet_aton(clientIP,&ClientAddr->sin_addr);
}

#ifdef USE_RTSP
int initRtcpClientAddr(struct sockaddr_in *ClientAddr,char *clientIP)
{
    memset(ClientAddr,0,sizeof(struct sockaddr_in));
 	ClientAddr->sin_port=htons(rtcpPort);
 
    ClientAddr->sin_family = AF_INET;
    inet_aton(clientIP,&ClientAddr->sin_addr);
}
#endif

#ifdef USE_RTSP
//in fast our send rtp package as a client ,need no to specific port ,but in rtsp protocol it have 
//declared ,thus we must use the specific port 
int initRtpServerAddr(int ServerFd ,char ip[32])
{
    struct sockaddr_in Server;
	Server.sin_family = AF_INET;
    Server.sin_port = htons(ServerRtpPort);	
	Server.sin_addr.s_addr = inet_addr(ip);
    if(bind(ServerFd,(struct sockaddr*)&Server,sizeof(Server))<0)
    {
        printf("bind server port error");
        return -1;
    }
	return 0;
}

int initRtcpServerAddr(int ServerFd ,char ip[32])
{
    struct sockaddr_in Server;
	Server.sin_family = AF_INET;
    Server.sin_port = htons(ServerRtcpPort);	
	Server.sin_addr.s_addr = inet_addr(ip);
    if(bind(ServerFd,(struct sockaddr*)&Server,sizeof(Server))<0)
    {
        printf("bind server port error");
        return -1;
    }
	return 0;
}
#endif

void ModifyRtpMFlag(RTP_H *head,int mValue)
{
	unsigned char M0 = 0x60;  //when m is 0 ,then the second byte of rtp header is 0x60   -> 0 110  0000
	unsigned char M1 = 0xE0;  //when m is 1 ,then the second byte of rtp header is 0xe0   -> 1 110  0000
	if(mValue==0)
	{
		*((char*)head+1)=M0;
	}
	else
	{
		*((char*)head+1)=M1;
	}
}

int main(int argc ,char **argv)
{
	FILE *fpMedia=NULL;
	char *pBuff=NULL;
	char *pFrameBuff=NULL;
	char readbuf[1024];
	char sendbuf[MAX_SOCKET_PACKET_LEN];
	char *p=NULL;
	RTP_H *rtpH=NULL;
	int rtpSocketFd =-1;
	int rtcpSocketFd = -1;
	unsigned int totalreadLen=0;
	unsigned int readlen=0;
	unsigned int oneFrameLen;
	unsigned int fileLen  = 0;
	unsigned int position = 0;
	unsigned int sendLen  = 0;
	int haveSendLen= 0;
	int startSend = 0;  //only get sps or pps then satrt send
	int sendPackageNum = 1;  //contemporary package to be send
	unsigned char headtmp[2]={0x80,0x60};
	
	struct sockaddr_in ClientAddr;  //client rtp port
	struct sockaddr_in ClientRtcpAddr;   //client rtcp port
	
	char localip[32]={0};

	//slice
	unsigned char SliceNum=0;
	unsigned char SliceIndicator =0;
	unsigned char SliceHeader =0;

	//convert
	unsigned short l_sn=FIRST_SERIAL_NUM;  //in fact here we use 0
	unsigned int   l_ts=FIRST_TIMESTAMP;   //in fact here we use 0
	unsigned int   l_ssrc = 0x12345678;

	unsigned char naluType=0;
	
#ifndef USE_RTSP
	//uage
	if(argc<=1)
	{
		printf("usage:\n"\
			   "1.rtp test\n"\
			   "  1)vlc load sdp file ,need to be modify according to IP ,port need not to be modify\n"\
			   "     c=IN IP4 10.0.0.3\n"\
			   "     m=video 9000 RTP/AVP 96\n"\
			   "     a=rtpmap:96 H264/90000\n"\
			   "  2)IPC run as server ,you should specific user ip\n"
			   "     %s <client ip>\n"
			   "2.rtsp test\n"
			   "     TODO\n",argv[0]);
		return -1;
	}
#endif

	//get media file handler
	if((fpMedia=fopen(MEDIA_FILE_PATH,"r"))==NULL)
	{
		printf("write file %s error\n",MEDIA_FILE_PATH);
		return -1;
	}


	//get the file len
	fseek(fpMedia,0,SEEK_END);
	fileLen = ftell(fpMedia);
	rewind(fpMedia);
	
	
	//malloc spcae for cache one frame and the whole file 
	pFrameBuff = (char*)malloc(FRAME_BUFF_SIZE);
	pBuff      = (char*)malloc(fileLen);
	memset(pBuff,0x0,fileLen);


	//get all data into file-buffer
	while(!feof(fpMedia))
	{
		memset(readbuf,0x0,1024);
		readlen = fread(readbuf,1,1024,fpMedia);
		memcpy(pBuff+totalreadLen,readbuf,readlen);
		totalreadLen += readlen;
	}
	if(totalreadLen!=fileLen)
	{
		printf("read file error");
		fclose(fpMedia);
		return -1;
	}
	fclose(fpMedia);
	

	//prepare for rtp header
	rtpH=(RTP_H *)malloc(sizeof rtpH);
	memset(rtpH,0x0,sizeof rtpH);
	/*
	rtpH->V    = 2;           //init version 2
	rtpH->P    = 0;           //no filling
	rtpH->X    = 0;           //no extend 
	rtpH->CC   = 0;           //no csrc
	rtpH->M    = 0;           //default 0 ,will be modify
	rtpH->PT   = 96;          //H264
	*/

	*((char*)rtpH)=0x80;  
	*((char*)rtpH+1)=0x60; 
	rtpH->SN   = htons(l_sn);           //default 0 ,will be modify
	rtpH->TS   = htonl(l_ts);           //default 0 ,will be modify
	rtpH->SSRC = htonl(l_ssrc);  //specific 0x12345678

	//prepare for rtp socket
	rtpSocketFd = createUdpSocket();
	if(rtpSocketFd < 0)
	{
		printf("create rtp udp socket error\n");
		return -1;
	}

#ifndef USE_RTSP
	initClientAddr(&ClientAddr,argv[1]);
#else
	//get local network device ip
	getLocalIP(localip);
	
	//prepare for rtcp socket
	rtcpSocketFd = createUdpSocket();
	if(rtcpSocketFd < 0)
	{
		printf("create rtcp udp socket error\n");
		return -1;
	}

	//allocate space
	initRtpServerAddr(rtpSocketFd,localip);
	initRtcpServerAddr(rtcpSocketFd,localip);

	//rtsp transport
	ProcessRtsp();

	while(m_rtsp_start==0)
	{
		sleep(1);
	}
	
	initClientAddr(&ClientAddr,ClientIP);
	initRtcpClientAddr(&ClientRtcpAddr,ClientIP);

#endif

	//get frame and send 
	p=pBuff;
	while(position<=fileLen)
	{
		//get one frame 
		if(p[position]==0x00 &&
		   p[position+1]==0x00 &&
		   p[position+2]==0x00 && 
		   p[position+3]==0x01)
		{
			oneFrameLen=0;
			position+=4;
			memset(pFrameBuff,0x0,FRAME_BUFF_SIZE);
			
			while(position<=fileLen)
			{
				pFrameBuff[oneFrameLen]=p[position];
				oneFrameLen++;
			
				if(p[position+1]==0x00 &&
		   		   p[position+2]==0x00 &&
		   		   p[position+3]==0x00 && 
		   		   p[position+4]==0x01)
		   		{
		   			if(startSend>=1)
		   				printf("Frame len [%d]",oneFrameLen);
		   			break;
		   		}
				
				position++;
			}	
		}

		//get one frame type
		//printf("frame is %02x ",pFrameBuff[0]);
		naluType = pFrameBuff[0]&0x1F;
		switch(naluType)
		{
			case NALU_T_P_FRAME:
				if(startSend>=1)
					printf(" p frame\n");
				break;
			case NALU_T_I_FRAME:
				if(startSend>=1)
				 	printf(" i frame\n");
				break;
			case NALU_T_SPS_FRAME:
				startSend=1;
				if(startSend>=1)
					printf(" sps frame");
				break;
			case NALU_T_PPS_FRAME:
				if(startSend>=1)
					printf(" pps frame");
				break;
			default:
				printf(" unknow frame type\n");
				break;
			
		}

		//send one frame
		if(startSend==1)
		{
			
			if(oneFrameLen<=MAX_NALU_LEN) //frame-len < max-nalu-len ,no need to split
			{
				//RTP   +   +---------------+  +  nalu-data
                //          |0|1|2|3|4|5|6|7|
                //          +-+-+-+-+-+-+-+-+
                //          |F|NRI|  Type   |
                //          +---------------+
                
				//assemble rtp header
				//rtpH->M = 1;
				//if type is sps or pps ,then M bit should be 0
				if((naluType == NALU_T_SPS_FRAME) || (naluType == NALU_T_PPS_FRAME))
				{
					ModifyRtpMFlag(rtpH,0);
				}
				else
				{
					ModifyRtpMFlag(rtpH,1);
				}
				sendLen=0;

				//assemble send package
				memset(sendbuf,0x0,MAX_SOCKET_PACKET_LEN);
				memcpy(sendbuf,rtpH,sizeof(RTP_H));
				sendLen += sizeof(RTP_H);
				memcpy(sendbuf+sendLen,pFrameBuff,oneFrameLen);
				sendLen += oneFrameLen; 

				if((haveSendLen=sendto(rtpSocketFd,sendbuf,sendLen,0,(struct sockaddr *)&ClientAddr,sizeof(struct sockaddr_in)))==-1)
				{
					printf("send udp err %d\n",__LINE__);
					return -1;
				}

				printf(" slice 1 sendLen %d\n",sendLen);
				//rtpH->SN++;
				//rtpH->TS += MEDIA_TIME_STAMP_ACCUMULATE;
				l_sn++;
				rtpH->SN = htons(l_sn);
				//if type is sps or pps ,then sn increase but timestamp should not change
				if((naluType != NALU_T_SPS_FRAME) && (naluType != NALU_T_PPS_FRAME))
				{
					l_ts += MEDIA_TIME_STAMP_ACCUMULATE;
					rtpH->TS = htonl(l_ts);
				}
			}
			else
			{
				//RTP   +   +---------------+  +  +---------------+ + nalu-data
                //          |0|1|2|3|4|5|6|7|     |0|1|2|3|4|5|6|7|
                //          +-+-+-+-+-+-+-+-+     +-+-+-+-+-+-+-+-+
                //          |F|NRI|  Type1  |     |S|E|R|  Type2  |
                //          +---------------+     +---------------+
				
				//get how many slice should be split
				SliceNum = oneFrameLen/MAX_NALU_LEN;
				if(oneFrameLen%MAX_NALU_LEN > 0) //if have tail
				{
					SliceNum++;
				}
				//printf(" slice %d\n",SliceNum);

				//assemble nalu indicator and header (partially)
				SliceIndicator  = 0;
				SliceIndicator |= pFrameBuff[0]&0xE0;
				SliceIndicator |= SLICE_FU_A;
				
				SliceHeader = 0;
				SliceHeader |= pFrameBuff[0]&0x1F;

				sendPackageNum=1;
				while(sendPackageNum<=SliceNum)
				{
					sendLen=0;
					memset(sendbuf,0x0,MAX_SOCKET_PACKET_LEN);
					
					if(sendPackageNum==1)  //the first package
					{
						//rtpH->M=0;
						ModifyRtpMFlag(rtpH,0);
						SliceHeader |= 0x80; //set "S=1"
						SliceHeader &= 0x9F; //set "E=0,R=0"

						//assemble send package
						memcpy(sendbuf,rtpH,sizeof(RTP_H));
						sendLen += sizeof(RTP_H);
						memcpy(sendbuf+sendLen,&SliceIndicator,sizeof(char));
						sendLen += sizeof(char);
						memcpy(sendbuf+sendLen,&SliceHeader,sizeof(char));
						sendLen += sizeof(char);
						memcpy(sendbuf+sendLen,(pFrameBuff+1),MAX_NALU_LEN-1); //original nalu header occupy one byte,so need to delete
						sendLen += MAX_NALU_LEN-1;
						if((haveSendLen=sendto(rtpSocketFd,sendbuf,sendLen,0,(struct sockaddr *)&ClientAddr,sizeof(struct sockaddr_in)))==-1)
						{
							printf("send udp err %d\n",__LINE__);
							return -1;
						}
					}
					else if(sendPackageNum==SliceNum)
					{
						//rtpH->M=1;
						ModifyRtpMFlag(rtpH,1);
						SliceHeader |= 0x40; //set "E=1"
						SliceHeader &= 0x5F; //set "S=0,R=0"

						//assemble send package
						memcpy(sendbuf,rtpH,sizeof(RTP_H));
						sendLen += sizeof(RTP_H);
						memcpy(sendbuf+sendLen,&SliceIndicator,sizeof(char));
						sendLen += sizeof(char);
						memcpy(sendbuf+sendLen,&SliceHeader,sizeof(char));
						sendLen += sizeof(char);
						memcpy(sendbuf+sendLen,pFrameBuff+(sendPackageNum-1)*MAX_NALU_LEN,oneFrameLen-(sendPackageNum-1)*MAX_NALU_LEN); //original nalu header occupy one byte,so need to delete
						sendLen += oneFrameLen-(sendPackageNum-1)*MAX_NALU_LEN;

						if((haveSendLen=sendto(rtpSocketFd,sendbuf,sendLen,0,(struct sockaddr *)&ClientAddr,sizeof(struct sockaddr_in)))==-1)
						{
							printf("send udp err %d\n",__LINE__);
							return -1;
						}
					}
					else if(sendPackageNum>1 && sendPackageNum<SliceNum)
					{
						//rtpH->M=0;
						ModifyRtpMFlag(rtpH,0);
						SliceHeader &= 0x1F; //set "S=0,E=0,R=0"

						//assemble send package
						memcpy(sendbuf,rtpH,sizeof(RTP_H));
						sendLen += sizeof(RTP_H);
						memcpy(sendbuf+sendLen,&SliceIndicator,sizeof(char));
						sendLen += sizeof(char);
						memcpy(sendbuf+sendLen,&SliceHeader,sizeof(char));
						sendLen += sizeof(char);
						memcpy(sendbuf+sendLen,pFrameBuff+(sendPackageNum-1)*MAX_NALU_LEN,MAX_NALU_LEN); //original nalu header occupy one byte,so need to delete
						sendLen += MAX_NALU_LEN;
						if((haveSendLen=sendto(rtpSocketFd,sendbuf,sendLen,0,(struct sockaddr *)&ClientAddr,sizeof(struct sockaddr_in)))==-1)
						{
							printf("send udp err %d\n",__LINE__);
							return -1;
						}

					}
					sendPackageNum++;
					//rtpH->SN++;
					l_sn++;
					rtpH->SN = htons(l_sn);
					usleep(10*1000); //10ms
				}//send one giant frame end
				//rtpH->TS += MEDIA_TIME_STAMP_ACCUMULATE;
				l_ts += MEDIA_TIME_STAMP_ACCUMULATE;
				rtpH->TS = htonl(l_ts);
			}
		}	
		position++;
	}

	free(pBuff);
	free(pFrameBuff);
	close(rtpSocketFd);
#ifdef USE_RTSP
	close(rtcpSocketFd);
#endif
	return 0;

}




