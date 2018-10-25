## Makefile 
all:
	gcc -o rtp newProcess.c rtspHandle.c base64.c -I./ -lpthread
clean:
	rm rtp
