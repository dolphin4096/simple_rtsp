#include<stdio.h>

#define USE_RTSP  //when no use rtsp ,pls do not define it

#define FIRST_TIMESTAMP 0
#define FIRST_SERIAL_NUM 0

#define RTP_MEDIA_TRANSPORT_PORT 9000    //rtp port
#define FRAME_BUFF_SIZE 1024*1024

#define MAX_SOCKET_PACKET_LEN 1500
#define MAX_NALU_LEN          1400

#define MEDIA_FILE_SAMPLE_FREQUENCY 90000 //media file sample frequency
#define MEDIA_FRAME_RATE 30 //media frame rate 30 frame/s
#define MEDIA_TIME_STAMP_ACCUMULATE (MEDIA_FILE_SAMPLE_FREQUENCY/MEDIA_FRAME_RATE) // fz/fr 90000/30

#define START_CODE_LEN 4  //start code in h264 file is  "0x00 0x00 0x00 0x01"

#define IFNAME "eth3"
#define MEDIA_FILE_PATH "./test.h264"  


//nalu media type declare here
enum nalu_media_type
{
	NALU_T_UNKNOW=0,  
	NALU_T_P_FRAME=1,  //p frame 
	NALU_T_PA=2,
	NALU_T_PB,
	NALU_T_PC,
	NALU_T_I_FRAME,     //i frame
	NALU_T_ENHANCE,
	NALU_T_SPS_FRAME,   //sps
	NALU_T_PPS_FRAME,   //pps
	NALU_T_DEVIDER,
	NALU_SERIAL_END,
	NALU_STREAM_END,
	NALU_FILL_BUF,
	NALU_RESERVE,
};

//piece of slice declare here
enum nalu_slice_type
{
	SLICE_STAP_A=24,
	SLICE_STAP_B,
	SLICE_STAP_C,
	SLICE_MTA,
	SLICE_FU_A=28,   //FU-A
	SLICE_FU_B,
	SLICE_RESERVR,
};

//rtp header declare here

typedef struct NodeHeader1
{
	unsigned char  V:2;      //version
	unsigned char  P:1;      //control fill bit
	unsigned char  X:1;      //extend
	unsigned char  CC:4;     //muxer filter count
	unsigned char  M:1;      //NALU last slice
	unsigned char  PT:7;     //payload type
	unsigned short SN:16;    //serial num
	unsigned int   TS:32;    //time stamp
	unsigned int   SSRC:32;  //ssrd identific
//	unsigned long  CSRC:32;  //csrc id (option)
}RTP_H;


int ProcessRtsp(void);



