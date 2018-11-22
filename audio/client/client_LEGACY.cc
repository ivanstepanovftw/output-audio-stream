///////////////////////////////////////
///////////////////////////////////////
///////////////////////////////////////
//
// Created by root on 01.12.17.
//
#include <mutex>
#include <vector>
#include <iostream>
#include <thread>
#include <enet/enet.h>
#include <cstring>
#include <portaudio.h>
#include <opus/opus.h>
#include <boost/circular_buffer.hpp>

using namespace std;
using namespace chrono;

#define SAMPLE opus_int16

#define CHECK(x) { if(!(x)) { \
fprintf(stderr, "%s:%i: failure at: %s\n", __FILE__, __LINE__, #x); \
_exit(1); } }

enum MsgId {
    MsgSoundData    = 0x0,
    MsgDataRequest  = 0x1,
    MsgDataFaster   = 0x2,
    MsgDataSlower   = 0x3,
    MsgDataPing     = 0x4,
};

struct MsgHeader {
    MsgId id;
    enet_uint32 seqnum;
    enet_uint32 flags;
    enet_uint32 misc2;
};

OpusDecoder *decoder;
ENetPeer *peer;
ENetHost *host;
bool optionReliable = true;

enet_uint16 PORT            = 59010;
int         SAMPLE_RATE     = 48000;
int         FRAME_SIZE      = 480;
int         CHANNELS        = 2;
size_t      MAX_FRAME_SIZE  = 0;
#define LEN(a) (sizeof(a)/sizeof(*(a)))


boost::circular_buffer<SAMPLE> CaptureBuffer;

bool isIdle = true;

int openedTimes = 0;

/// FIXME:XXX: portaudio not working at all
static int playCallback(
        [[gnu::unused]] const void *inputBuffer,
        [[gnu::unused]] void *outputBuffer,
        [[gnu::unused]] unsigned long framesPerBuffer,
        [[gnu::unused]] const PaStreamCallbackTimeInfo *timeInfo,
        [[gnu::unused]] PaStreamCallbackFlags statusFlags,
        [[gnu::unused]] void *userData)
{
    SAMPLE *out = (SAMPLE *) outputBuffer;

    ENetEvent event;
    MsgHeader *h;
    SAMPLE *b;
    int b_frames = 0;
    int status;
    while (true) {
        status = enet_host_service(host, &event, 1000);
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
            //todo msgheader contains channels
            for(int i=0; i<FRAME_SIZE*CHANNELS; i++) {
                CaptureBuffer.push_back(b[i]);
            }

            if (CaptureBuffer.empty())
                isIdle = true;
            else if (CaptureBuffer.full())
                isIdle = false;

            if (isIdle) {
                break;
            }

            for(size_t i=0; i < framesPerBuffer*CHANNELS;) {
                out[i++] = CaptureBuffer.front();
                CaptureBuffer.pop_front();
            }

            enet_packet_destroy(event.packet);
            return paContinue;

        case ENET_EVENT_TYPE_DISCONNECT:
            puts("Disconnection succeeded.");
            break;

        case ENET_EVENT_TYPE_NONE:
            puts("Timeout while waiting ENet event.");
            break;

        case ENET_EVENT_TYPE_CONNECT:
            MsgHeader header;
            memset(&header, 0, sizeof(header));
            header.id = MsgDataRequest;
            header.flags = 0x1;

            //TODO while(true): обрабатываем запросы на сервер, например, с командной строки
            ENetPacket *packet = enet_packet_create((void *)&header, sizeof(header), ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(peer, 0, packet);
            enet_host_flush(host);
            clog<<"Connection success"<<endl;
            break;
    }

    //memset(out, 0, sizeof(SAMPLE)*framesPerBuffer*CHANNELS);
    return paContinue;
}


PaStream *Open() {
    /* Initialize. */
    CHECK(Pa_Initialize() == paNoError);

    PaStreamParameters output_parameters;
    output_parameters.device = Pa_GetDefaultOutputDevice();
    CHECK(output_parameters.device != paNoDevice);
    CHECK(output_parameters.device != paUseHostApiSpecificDeviceSpecification);
    output_parameters.channelCount = CHANNELS;
    output_parameters.sampleFormat = paInt16;
    output_parameters.hostApiSpecificStreamInfo = NULL;

    const PaDeviceInfo *input_info = Pa_GetDeviceInfo(output_parameters.device);
    CHECK(input_info != nullptr);
    output_parameters.suggestedLatency = input_info->defaultLowOutputLatency;

    /* Create and start stream */
    PaStream *stream;
    PaError ret = Pa_OpenStream(&stream,
                                NULL,
                                &output_parameters,
                                input_info->defaultSampleRate,
                                (unsigned)(FRAME_SIZE),
                                paClipOff,
                                playCallback,
                                NULL);

    if (ret != paNoError) {
        fprintf(stderr, "Pa_OpenStream failed: (err %i) %s\n", ret, Pa_GetErrorText(ret));
        if (stream)
            Pa_CloseStream(stream);
        return nullptr;
    }

    CHECK(Pa_StartStream(stream) == paNoError);
    openedTimes++;

    const PaStreamInfo *streamInfo = Pa_GetStreamInfo(stream);
    clog<<"[!] Audio stream info: sample rate = "<<streamInfo->sampleRate
            <<", input latency = "<<streamInfo->inputLatency<<" sec"<<endl;

    return stream;
}



int main(int argc, char *argv[]) {
    int err;
    OpusDecoder *decoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err);
    if (!decoder || err != OPUS_OK) {
        std::cerr << "Opus decoder create failed: " << opus_strerror(err) << std::endl;
        return EXIT_FAILURE;
    }

    std::clog << "Starting network client" << std::endl;
    if (enet_initialize() != 0) {
        std::cerr << "An error occurred while initializing ENet" << std::endl;
        return EXIT_FAILURE;
    }
    atexit(enet_deinitialize);

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

    ENetEvent event;
    ssize_t ping = -1;
    high_resolution_clock::time_point t1;

    while (ping < 0) {
        errno = 0;
        int status = enet_host_service(host, &event, 1);
        if (status < 0) {
            std::cerr << "Error returned from service call: " << std::strerror(errno) << std::endl;
            return EXIT_FAILURE;
        }

        switch(event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                MsgHeader header{};
                // allow server to send us audio
                header.id = MsgDataRequest;
                header.flags = optionReliable?0x1:0;  // reliable or best-effort mode
                ENetPacket *packet = enet_packet_create(&header, sizeof(header), ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(peer, 0, packet);
                enet_host_flush(host);

                // send server ping request
                header.id = MsgDataPing;
                packet = enet_packet_create((void *)&header, sizeof(header), ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(peer, 0, packet);
                enet_host_flush(host);
                t1 = high_resolution_clock::now();
                clog<<"ping sent"<<endl;
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                MsgHeader *header = reinterpret_cast<MsgHeader *>(event.packet->data + 0);
                switch(header->id) {
                    case MsgSoundData:break;
                    case MsgDataRequest:break;
                    case MsgDataFaster:break;
                    case MsgDataSlower:break;
                    case MsgDataPing:
                        ping = duration_cast<duration<typeof(ping)>>(high_resolution_clock::now() - t1).count();
                        clog<<"ping: "<<ping<<" ms."<<endl;
                        clog<<"formula: samples = ms * sample_rate"<<endl;
                        MAX_FRAME_SIZE = (static_cast<size_t>((ping*SAMPLE_RATE)/FRAME_SIZE)+1)*FRAME_SIZE;
                        clog<<"formula: finally = "<<MAX_FRAME_SIZE<<endl;

                        //allocate memory
                        CaptureBuffer.resize(MAX_FRAME_SIZE);
                        clog<<"CaptureBuffer.size():  "<<CaptureBuffer.size()<<endl;
                        break;
                }
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT:
                return -111;

            case ENET_EVENT_TYPE_NONE:
                break;
        }
    }

    PaStream *stream = Open();
    while (true) {
        if (Pa_IsStreamActive(stream)) {
            usleep(10*1000);
        } else {
            Pa_StopStream(stream);
            stream = Open();
            if (!stream)
                break;
        }
    }
}
