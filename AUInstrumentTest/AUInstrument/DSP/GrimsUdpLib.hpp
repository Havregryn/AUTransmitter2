//
//  GrimsUdpLib.hpp
//  UDPAU
//
//  Created by Hallgrim Bratberg on 08/08/2021.
//

#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// For testing only:
// #include <math.h>

#include <fcntl.h>


#define RECEIVER_PORT "4968" // Transmitters must connect to this port!
#define MAX_CHANNELS 2
#define SAMPLES_PR_MSG 4096
#define MSG_BUFS 16
#define MSG_PTRS 8

struct audioMsg {
    uint16_t seqNr;
    uint16_t framesCount;
    uint16_t channel;
    int audioBufIx;
    float* audioBuf;
};


class UdpAudioReceiver {
public:
    UdpAudioReceiver() {}
    
    void init(int channelCount){
        this->channelCount = channelCount;
        if(!socketIsOpen) {createSocket();}
        for(int ch = 0; ch < MAX_CHANNELS; ch++) {
            for(int i = 0; i < MSG_PTRS; i++) {
                msgRcvd[ch][i].audioBuf = NULL;
            }
         }
        for(int i = 0; i < MSG_BUFS; i++) {
        }
        nowPlayedSeqNr = 0;
        maxSeqNrRcvd = -1;
    }
    
    void receive() {
        char *msgBuf = &msgRingBuf[msgRingBufIx][0];
        
        if(sockfd > 0) {
            numbytes = recv(sockfd, msgBuf, msgBufSize, 0);
            if(numbytes > 0) {
                currentMsgBufIx = 0;
                memcpy(&seqNrRcvd, msgBuf, sizeof(uint16_t));
                currentMsgBufIx += sizeof(uint16_t);
                
                memcpy(&framesRcvd, &msgBuf[currentMsgBufIx], sizeof(uint16_t));
                currentMsgBufIx += sizeof(uint16_t);
                
                memcpy(&channelRcvd, &msgBuf[currentMsgBufIx], sizeof(uint16_t));
                currentMsgBufIx += sizeof(uint16_t);
                
                
                if(seqNrRcvd > maxSeqNrRcvd) {maxSeqNrRcvd = seqNrRcvd; }
                // If nothing has been played yet nowPlayedSeqNr must be updated
                // to the correct start seqNr:
                if(nowPlayedSeqNr == 0) {nowPlayedSeqNr = seqNrRcvd; }
                
                struct audioMsg* msg = &msgRcvd[channelRcvd][seqNrRcvd % MSG_PTRS];
                
                msg->seqNr = seqNrRcvd;
                msg->framesCount = framesRcvd;
                msg->channel = channelRcvd;
                msg->audioBufIx = 0;
                msg->audioBuf = (float *) &msgBuf[currentMsgBufIx];
                
                msgRingBufIx = ++msgRingBufIx % MSG_BUFS;
            }
        }
    }
            
    void receiveAndGetSample(float* outBuf, int frameOffset, int channel) {
        // Kalles inni frameloop inni channel loop
        
        nowPlayedMsg = &msgRcvd[channel][nowPlayedSeqNr % MSG_PTRS];
    
        // PART I : AUDIO IS READY IN BUFFER:
        
        if(nowPlayedMsg->audioBuf != NULL) {
            
            // nowPlayedMsg contains the sample we need now.
            // Deliver this sample, update counters/pointers and return:
            outBuf[frameOffset] = nowPlayedMsg->audioBuf[audioBufIx];
            
            if(audioBufIx == nowPlayedMsg->framesCount - 1) {
                // The last sample of this messsage has been read:
                nowPlayedMsg->audioBuf = NULL;
                
                // Hardcoded channel number:
                if(channel == 1) {
                    audioBufIx = 0;
                    nowPlayedSeqNr = ++nowPlayedSeqNr % UINT16_MAX;
                }
            } else if(channel == 1) {
                // Last channel but not last sample in the message:
                audioBufIx++;
            }
            return;
            
        }
        
        // PART II: NO AUDIO IN BUFFER: NEW MESSAGE IS NEEDED!:
        
        // audioBufIx = 0: Need a new msg to read from:
        for(int rpt = 0; rpt < 2; rpt++) {
            receive();
        }
        
        // Play if msg is found, otherwise return silence:
        if(nowPlayedMsg->audioBuf != NULL) {
            outBuf[frameOffset] = nowPlayedMsg->audioBuf[audioBufIx];
            // This must be the first frame of the msg, we need only a simple counter update:
            if(channel == channelCount - 1) { audioBufIx++; }
        } else {
            outBuf[frameOffset] = 0.0f;
        }
    }
    
    void deallocate(){
        close(sockfd);
        socketIsOpen = false;
    }
    
private:
    // Socket and udp properties:
    int sockfd;
    bool socketIsOpen = false;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    long numbytes;
    struct sockaddr_storage their_addr;
    socklen_t addr_len;
    
    // Audio related properties:
    static const int msgBufSize = (sizeof(float) * SAMPLES_PR_MSG) + (sizeof(int) * 10);
    char msgRingBuf[MSG_BUFS][msgBufSize];
    int msgRingBufIx;
    
    // Pointer array, connecting seqNr with relevant msg buffer:
    struct audioMsg msgRcvd[MAX_CHANNELS][MSG_PTRS];
    
    int nowPlayedSeqNr;
    int maxSeqNrRcvd;
    
    uint16_t seqNrRcvd;
    uint16_t channelRcvd;
    uint16_t framesRcvd;
    
    struct audioMsg *nowPlayedMsg;
    int currentMsgBufIx;
    int audioBufIx; // Used when reading samples from msg audio bufs.
    
    
    int channelCount = 0;
    
    bool debugprinted = false;
    
    
    
    
    
    
    // Private functions:
    int createSocket(){
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET6;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_PASSIVE;
        
        if((rv = getaddrinfo(NULL, RECEIVER_PORT, &hints, &servinfo)) != 0){
          fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
          return 1;
        }

        /* Looping through all results, use the first working socket */
        for(p = servinfo; p != NULL; p = p->ai_next){
            if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
                perror("talker: socket");
                continue;
            }
            
            // Bind: Tell the OS incoming on this port should be sent to this socket:
            if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
                  perror("listener: bind");
            }
            
            // Set nonblocking flag:
            int flags = fcntl(sockfd, F_GETFL, 0);
            flags = flags | O_NONBLOCK;
            fcntl(sockfd, F_SETFL, flags);
            break;
        }

        if(p == NULL){
          fprintf(stderr, "Failed to create socket\n");
          return 2;
        }
        printf("A socket was successfully set up!\n");
        socketIsOpen = true;
        return 0;
    }
};
