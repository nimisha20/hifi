//
//  main.cpp
//  eve
//
//  Created by Stephen Birarda on 4/22/13.
//  Copyright (c) 2013 High Fidelity, Inc. All rights reserved.
//

#include <sys/time.h>

#include <SharedUtil.h>
#include <AgentTypes.h>
#include <PacketHeaders.h>
#include <AgentList.h>
#include <AvatarData.h>

const int EVE_AGENT_LIST_PORT = 55441;
const float DATA_SEND_INTERVAL_MSECS = 10;

bool stopReceiveAgentDataThread;

void *receiveAgentData(void *args)
{
    sockaddr senderAddress;
    ssize_t bytesReceived;
    unsigned char incomingPacket[MAX_PACKET_SIZE];
    
    AgentList *agentList = AgentList::getInstance();
    Agent *avatarMixer = NULL;
    
    while (!::stopReceiveAgentDataThread) {
        if (agentList->getAgentSocket().receive(&senderAddress, incomingPacket, &bytesReceived)) { 
            switch (incomingPacket[0]) {
                case PACKET_HEADER_BULK_AVATAR_DATA:
                    // this is the positional data for other agents
                    // eve doesn't care about this for now, so let's just update the receive time for the
                    // avatar mixer - this makes sure it won't be killed during silent agent removal
                    avatarMixer = agentList->soloAgentOfType(AGENT_TYPE_AVATAR_MIXER);
                    
                    if (avatarMixer != NULL) {
                        avatarMixer->setLastRecvTimeUsecs(usecTimestampNow());
                    }
                    
                    break;
                default:
                    // have the agentList handle list of agents from DS, replies from other agents, etc.
                    agentList->processAgentData(&senderAddress, incomingPacket, bytesReceived);
                    break;
            }
        }
    }
    
    pthread_exit(0);
    return NULL;
}

int main(int argc, char* argv[]) {
    // create an AgentList instance to handle communication with other agents
    AgentList *agentList = AgentList::createInstance(AGENT_TYPE_AVATAR, EVE_AGENT_LIST_PORT);
    
    // start telling the domain server that we are alive
    agentList->startDomainServerCheckInThread();
    
    // start the agent list thread that will kill off agents when they stop talking
    agentList->startSilentAgentRemovalThread();
    
    // start the ping thread that hole punches to create an active connection to other agents
    agentList->startPingUnknownAgentsThread();
    
    pthread_t receiveAgentDataThread;
    pthread_create(&receiveAgentDataThread, NULL, receiveAgentData, NULL);
    
    // create an AvatarData object, "eve"
    AvatarData eve = AvatarData();
    
    unsigned char broadcastPacket[MAX_PACKET_SIZE];
    broadcastPacket[0] = PACKET_HEADER_HEAD_DATA;
    
    int numBytesToSend = 0;
    
    timeval thisSend;
    double numMicrosecondsSleep = 0;
    
    while (true) {
        // update the thisSend timeval to the current time
        gettimeofday(&thisSend, NULL);
        
        // find the current avatar mixer
        Agent *avatarMixer = agentList->soloAgentOfType(AGENT_TYPE_AVATAR_MIXER);
        
        // make sure we actually have an avatar mixer with an active socket
        if (avatarMixer != NULL && avatarMixer->getActiveSocket() != NULL) {
            // use the getBroadcastData method in the AvatarData class to populate the broadcastPacket buffer
            numBytesToSend = eve.getBroadcastData((broadcastPacket + 1));
            
            
            // use the UDPSocket instance attached to our agent list to send avatar data to mixer
            agentList->getAgentSocket().send(avatarMixer->getActiveSocket(), broadcastPacket, numBytesToSend);
        }
        
        // sleep for the correct amount of time to have data send be consistently timed
        if ((numMicrosecondsSleep = (DATA_SEND_INTERVAL_MSECS * 1000) - (usecTimestampNow() - usecTimestamp(&thisSend))) > 0) {
            usleep(numMicrosecondsSleep);
        }        
    }
    
    // stop the receive agent data thread
    stopReceiveAgentDataThread = true;
    pthread_join(receiveAgentDataThread, NULL);
    
    // stop the agent list's threads
    agentList->stopDomainServerCheckInThread();
    agentList->stopPingUnknownAgentsThread();
    agentList->stopSilentAgentRemovalThread();
}


