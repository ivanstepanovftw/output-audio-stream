#pragma once

#include <enet/enet.h>
//
#include <streamsome/common.hh>


NAMESPACE_BEGIN(SS)

void clearSendQueue(ENetPeer *peer, ENetList *queue) {
    // https://hackage.haskell.org/package/henet-1.3.9.3/src/enet/peer.c
    int removed = 0;
    ENetListNode *cur = queue->sentinel.next;
    while (cur && cur != (ENetListIterator) queue)
    {
        ENetOutgoingCommand *outgoingCommand = (ENetOutgoingCommand *) enet_list_remove(cur);
        if (outgoingCommand->packet
            && !--outgoingCommand->packet->referenceCount)
            enet_packet_destroy(outgoingCommand->packet);

        peer->outgoingDataTotal = peer->outgoingDataTotal
                                  - (enet_uint32) enet_protocol_command_size(outgoingCommand->command.header.command)
                                  - outgoingCommand->fragmentLength;
        ENetChannel *channel = &peer->channels[outgoingCommand->command.header.channelID];
        if (channel->outgoingReliableSequenceNumber >= outgoingCommand->reliableSequenceNumber)
            channel->outgoingReliableSequenceNumber = outgoingCommand->reliableSequenceNumber - static_cast<enet_uint16>(1);
        removed++;
        cur = cur->next;
        enet_free(outgoingCommand);
    }
}

NAMESPACE_END(SS)
