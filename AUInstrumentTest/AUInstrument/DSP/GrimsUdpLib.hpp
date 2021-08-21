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


#define RECEIVER_PORT "4973" // Transmitters must connect to this port!
#define MAX_CHANNELS 2
#define FRAMES_PR_MSG 1024
#define MSG_BUFS 1024
#define MSG_PTRS 1024

#define MSGS_BUFFERED 4

struct audioMsg {
    uint16_t seqNr;
    uint16_t framesCount;
    uint16_t channels;
    int audioBufIx[MAX_CHANNELS];
    float* audioBuf[MAX_CHANNELS];
};


class UdpAudioReceiver {
public:
    UdpAudioReceiver() {}
    
    void init(int channelCount){
        this->channelCount = channelCount;
        printf("Channelcount: %d\n", channelCount);
        if(!socketIsOpen) {createSocket();}
        for(int ch = 0; ch < MAX_CHANNELS; ch++) {
            for(int i = 0; i < MSG_PTRS; i++) {
                msgRcvd[i].audioBuf[ch] = NULL;
                msgRcvd[i].audioBufIx[ch] = 0;
                
            }
            nowPlayedSeqNr[ch] = 0;
         }
        maxSeqNrRcvd = -1;
        hasStartedPlaying = false;
        packetsRcvd = 0;
        msgRingBufUsed = 0;
        msgsInBuffer = 0;
    }
    
    void receive() {
        char *msgBuf = &msgRingBuf[msgRingBufIx][0];
        
        if(sockfd > 0) {
            numbytes = recv(sockfd, msgBuf, msgBufSize, 0);
            if(numbytes > 0) {
                
                packetsRcvd++;
                msgRingBufUsed++;
                
                currentMsgBufIx = 0;
                memcpy(&seqNrRcvd, msgBuf, sizeof(uint16_t));
                currentMsgBufIx += sizeof(uint16_t);
                //if(packetsRcvd < 100) printf("Received seqNr: %d\n", seqNrRcvd);
                
                memcpy(&framesRcvd, &msgBuf[currentMsgBufIx], sizeof(uint16_t));
                currentMsgBufIx += sizeof(uint16_t);
                
                memcpy(&channelsRcvd, &msgBuf[currentMsgBufIx], sizeof(uint16_t));
                currentMsgBufIx += sizeof(uint16_t);
                
                
                /*
                if(packetsRcvd < 10){
                    printf("Msg no %d rcvd, seqNr: %d,  frCnt: %d, ch: %d bytes: %d\n",
                           (int)packetsRcvd, seqNrRcvd, framesRcvd, channelsRcvd, (int)numbytes);
                }
                 */
                
                
                
                if(seqNrRcvd > maxSeqNrRcvd) {maxSeqNrRcvd = seqNrRcvd; }
                // If nothing has been played yet nowPlayedSeqNr must be updated
                // to the correct start seqNr:
                if(packetsRcvd == 1) {
                    for(int ch = 0; ch < channelsRcvd; ch++) {
                        nowPlayedSeqNr[ch] = seqNrRcvd;
                    }
                }
                
                struct audioMsg* msg = &msgRcvd[seqNrRcvd % MSG_PTRS];
                
                msg->seqNr = seqNrRcvd;
                msg->framesCount = framesRcvd;
                msg->channels = channelsRcvd;
                for(int ch = 0; ch < channelsRcvd; ch++) {
                    msg->audioBufIx[ch] = 0;
                    msg->audioBuf[ch] = (float *) &msgBuf[currentMsgBufIx];
                    currentMsgBufIx += (framesRcvd * sizeof(float));
                }
                // Det som kommer herfra er rent, blir distorted senere i kjeden:
                
                msgRingBufIx = ++msgRingBufIx % MSG_BUFS;
                msgsInBuffer++;
            }
        }
    }
            
    void receiveAndGetSample(float* outBuf, int frameOffset, int channel) {
        // Kalles inni frameloop inni channel loop
        
        
        if(msgsInBuffer < MSGS_BUFFERED) receive();
        
        
        // Building a buffer before starting to play:
        if(packetsRcvd < MSGS_BUFFERED) {
            outBuf[frameOffset] = 0.0f;
            return;
        }
        
       
        
        if(!hasStartedPlaying) { hasStartedPlaying = true; }
        
        nowPlayedMsg = &msgRcvd[nowPlayedSeqNr[channel] % MSG_PTRS];
    
        if(nowPlayedMsg->audioBuf[channel] != NULL) {
            // nowPlayedMsg contains the sample we need now.
            // Deliver this sample, update counters/pointers and return:
            outBuf[frameOffset] = nowPlayedMsg->audioBuf[channel][nowPlayedMsg->audioBufIx[channel]];
            
            
            if(nowPlayedMsg->audioBufIx[channel] == nowPlayedMsg->framesCount - 1) {
                // The last sample of this channel has been read:
                nowPlayedMsg->audioBuf[channel] = NULL;
                
                nowPlayedSeqNr[channel] = ++nowPlayedSeqNr[channel] % UINT16_MAX;
                msgRingBufUsed--;
                msgsInBuffer--;
                
            } else {
                nowPlayedMsg->audioBufIx[channel]++;
            }
            return;
        }
        
        
        // Default: Play silence
        outBuf[frameOffset] = 0.0f;
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
    static const int msgBufSize = (sizeof(float) * FRAMES_PR_MSG * MAX_CHANNELS) + (sizeof(int) * 10);
    char msgRingBuf[MSG_BUFS][msgBufSize];
    int msgRingBufIx;
    int msgRingBufUsed;
    
    // Pointer array, connecting seqNr with relevant msg buffer:
    struct audioMsg msgRcvd[MSG_PTRS];
    
    int nowPlayedSeqNr[MAX_CHANNELS];
    int maxSeqNrRcvd;
    
    uint16_t seqNrRcvd;
    uint16_t channelsRcvd;
    uint16_t framesRcvd;
    
    struct audioMsg *nowPlayedMsg;
    int currentMsgBufIx;
    
    
    int channelCount = 0;
    
    bool hasStartedPlaying;
    int packetsRcvd;
    
    int msgsInBuffer;
    
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
