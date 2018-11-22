/***
  This file is part of PulseAudio.
  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.
  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <cstdio>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <iostream>
#include <enet/enet.h>
#include <opus/opus.h>
#include <boost/circular_buffer.hpp>
#include <chrono>


#define BUFSIZE 1024

using SAMPLE = opus_int16;
constexpr size_t SAMPLE_RATE = 48000;
bool swOptionAltRate = SAMPLE_RATE == 48000;
constexpr size_t FRAME_SIZE = SAMPLE_RATE/100;
constexpr size_t CHANNELS = 2;
constexpr uint16_t PORT = 59010;

OpusDecoder *decoder;
ENetPeer *peer;
ENetHost *host;
bool optionReliable = true;
boost::circular_buffer<SAMPLE> CaptureBuffer;

enum MsgId {
    MsgSoundData    = 0x0,
    MsgDataRequest  = 0x1,
    MsgDataFaster   = 0x2,
    MsgDataSlower   = 0x3,
    MsgDataPing     = 0x4,
};

//todo msgheader contains channels
struct MsgHeader {
    MsgId id;
    enet_uint32 seqnum;
    enet_uint32 flags;
    enet_uint32 misc2;
};




int main(int argc, char*argv[]) {
    using namespace std;
    using namespace std::chrono;
    using namespace std::string_literals;

    if (getuid() == 0)
        cerr<<"PulseAudio almost cannot be launched as root"<<endl;



    /* The Sample format to use */
    pa_sample_spec ss {};
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = SAMPLE_RATE;
    ss.channels = 2;
    pa_simple *s = NULL;
    int ret = 1;
    int error;
    /* Create a new playback stream */
    if (!(s = pa_simple_new(
            NULL,
            "StreamSome",
            PA_STREAM_PLAYBACK,
            NULL,
            "playback",
            &ss,
            NULL,
            NULL,
            &error))) {
        fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
        exit(1);
    }


    errno = 0;
    host = enet_host_create(
            nullptr,  // the address to bind the server host to
            1,        // allow up to 32 clients and/or outgoing connections
            1,        // allow up to 2 channels to be used, 0 and 1
            0,        // assume any amount of incoming bandwidth
            0         // assume any amount of outgoing bandwidth
    );
    if (!host) {
        std::cerr << "An error occurred while trying to create an ENet client host: " << std::strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }


    ENetAddress address;
    const char* ip = "192.168.31.21";
    enet_address_set_host(&address, ip);
    address.port = PORT;

    //подключаемся
    errno = 0;
    peer = enet_host_connect(host, &address, 2, 0/*42*/);
    if (!peer) {
        std::cerr << "No available peers for initiating an ENet connection: " << std::strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }

    //ENetEvent event;
    //ssize_t ping = -1;
    //high_resolution_clock::time_point t1;
    //
    //while (ping < 0) {
    //    errno = 0;
    //    int status = enet_host_service(host, &event, 1);
    //    if (status < 0) {
    //        std::cerr << "Error returned from service call: " << std::strerror(errno) << std::endl;
    //        return EXIT_FAILURE;
    //    }
    //
    //    switch(event.type) {
    //        case ENET_EVENT_TYPE_CONNECT: {
    //            MsgHeader header{};
    //            // allow server to send us audio
    //            header.id = MsgDataRequest;
    //            header.flags = optionReliable?0x1:0;  // reliable or best-effort mode
    //            ENetPacket *packet = enet_packet_create(&header, sizeof(header), ENET_PACKET_FLAG_RELIABLE);
    //            enet_peer_send(peer, 0, packet);
    //            enet_host_flush(host);
    //
    //            // send server ping request
    //            header.id = MsgDataPing;
    //            packet = enet_packet_create((void *)&header, sizeof(header), ENET_PACKET_FLAG_RELIABLE);
    //            enet_peer_send(peer, 0, packet);
    //            enet_host_flush(host);
    //            t1 = high_resolution_clock::now();
    //            clog<<"ping sent"<<endl;
    //            break;
    //        }
    //        case ENET_EVENT_TYPE_RECEIVE: {
    //            MsgHeader *header = reinterpret_cast<MsgHeader *>(event.packet->data + 0);
    //            switch(header->id) {
    //                case MsgSoundData:break;
    //                case MsgDataRequest:break;
    //                case MsgDataFaster:break;
    //                case MsgDataSlower:break;
    //                case MsgDataPing: {
    //                    ping = duration_cast<duration<typeof(ping)>>(high_resolution_clock::now() - t1).count();
    //                    clog<<"ping: "<<ping<<" ms."<<endl;
    //                    clog<<"formula: samples = ms * sample_rate"<<endl;
    //                    ssize_t MAX_FRAME_SIZE = (static_cast<size_t>((ping*SAMPLE_RATE)/FRAME_SIZE)+1)*FRAME_SIZE;
    //                    clog<<"formula: finally = "<<MAX_FRAME_SIZE<<endl;
    //
    //                    //allocate memory
    //                    CaptureBuffer.resize(MAX_FRAME_SIZE);
    //                    clog<<"CaptureBuffer.size():  "<<CaptureBuffer.size()<<endl;
    //                    break;
    //                }
    //            }
    //            break;
    //        }
    //        case ENET_EVENT_TYPE_DISCONNECT:
    //            return -111;
    //
    //        case ENET_EVENT_TYPE_NONE:
    //            break;
    //    }
    //}


    bool isIdle = true;
    SAMPLE *out = new SAMPLE[FRAME_SIZE*CHANNELS];

    for (;;) {
        ENetEvent event;
        MsgHeader *h;
        SAMPLE *b;
        int b_frames = 0;
        int status;
        while (true) {
            status = enet_host_service(host, &event, 10'000);
            if (status < 0) {
                puts("Error returned from service call");
                continue;
            } else if (status == 0) {
                break;
            } else
                break;
        }

        switch(event.type) {
            case ENET_EVENT_TYPE_RECEIVE:
                h = (MsgHeader *)(event.packet->data);
                b = (SAMPLE *)(event.packet->data + sizeof(MsgHeader));
                b_frames = (int)(event.packet->dataLength-sizeof(MsgHeader))/CHANNELS/sizeof(SAMPLE);
                for(int i=0; i<FRAME_SIZE*CHANNELS; i++) {
                    CaptureBuffer.push_back(b[i]);
                }

                if (CaptureBuffer.empty()) {
                    static size_t capturebuffer_count = 0;
                    clog<<"CaptureBuffer is empty #"<<capturebuffer_count++<<endl;
                    isIdle = true;
                }
                else if (CaptureBuffer.full()) {
                    static bool capturebuffer_once = false;
                    if (!capturebuffer_once) {
                        clog<<"CaptureBuffer is full "<<endl;
                        capturebuffer_once = true;
                    }
                    isIdle = false;
                }

                if (isIdle) {
                    break;
                }

                for(size_t i=0; i < FRAME_SIZE*CHANNELS;) {
                    out[i++] = CaptureBuffer.front();
                    CaptureBuffer.pop_front();
                }
                //std::cout<<+out[0]<<std::endl;
                if (pa_simple_write(s, out, FRAME_SIZE*CHANNELS*sizeof(SAMPLE), &error) < 0) {
                    fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(error));
                    goto finish;
                }

                enet_packet_destroy(event.packet);
                continue;

            case ENET_EVENT_TYPE_DISCONNECT:
                std::clog << "Disconnected." << std::endl;

                /* Make sure that every single sample was played */
                if (pa_simple_drain(s, &error) < 0) {
                    fprintf(stderr, __FILE__": pa_simple_drain() failed: %s\n", pa_strerror(error));
                    goto finish;
                }
                return 1;
                break;

            case ENET_EVENT_TYPE_NONE:
                static size_t timeout_count = 0;
                cout<<"Timeout 10s while waiting ENet event #"<<timeout_count++<<endl;
                break;

            case ENET_EVENT_TYPE_CONNECT:
#define UNRELIABLE_FRAGMENT 0x0
#define RELIABLE ENET_PACKET_FLAG_RELIABLE
                MsgHeader header{};
                header.id = MsgDataRequest;
                header.flags = UNRELIABLE_FRAGMENT;

                /// Request the data
                ENetPacket *packet = enet_packet_create((void *)&header, sizeof(header), ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(peer, 0, packet);
                enet_host_flush(host);
                clog<<"Connection success"<<endl;

                CaptureBuffer.set_capacity(FRAME_SIZE*CHANNELS);
                break;
        }
    }


    for (;;) {
        uint8_t buf[BUFSIZE];
        ssize_t r;
#if 1
        pa_usec_t latency;
        if ((latency = pa_simple_get_latency(s, &error)) == (pa_usec_t) -1) {
            fprintf(stderr, __FILE__": pa_simple_get_latency() failed: %s\n", pa_strerror(error));
            goto finish;
        }
        fprintf(stderr, "%0.0f usec    \r", (float)latency);
#endif
        /* Read some data ... */
        if ((r = read(STDIN_FILENO, buf, sizeof(buf))) <= 0) {
            if (r == 0) /* EOF */
                break;
            fprintf(stderr, __FILE__": read() failed: %s\n", strerror(errno));
            goto finish;
        }
        /* ... and play it */
        if (pa_simple_write(s, buf, (size_t) r, &error) < 0) {
            fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(error));
            goto finish;
        }
    }
    /* Make sure that every single sample was played */
    if (pa_simple_drain(s, &error) < 0) {
        fprintf(stderr, __FILE__": pa_simple_drain() failed: %s\n", pa_strerror(error));
        goto finish;
    }
    ret = 0;
    finish:
    if (s)
        pa_simple_free(s);
    return ret;
}
