#pragma clang diagnostic push
#pragma ide diagnostic ignored "modernize-use-auto"
//
// Created by root on 16.11.17.
//

//
//  Created by root on 11.11.17.
//
//  It is wifisound for linux, rewritten in c++.
//
//  Used examples:
//	    ENet:
//	        http://enet.bespin.org/Tutorial.html
//      opus:
//          https://chromium.googlesource.com/chromium/deps/opus/+/1.1.1/doc/trivial_example.c
//      pulseaudio:
//          https://freedesktop.org/software/pulseaudio/doxygen/pacat-simple_8c-example.html
//          https://freedesktop.org/software/pulseaudio/doxygen/parec-simple_8c-example.html
//          https://github.com/tercatech/lep/blob/d75c9def112becf935ea8a7a040775b8059a5250/src/audio.cpp
//          https://www.freedesktop.org/wiki/Software/PulseAudio/Documentation/Developer/Clients/Samples/AsyncDeviceList/
//      socket:
//          http://ntrg.cs.tcd.ie/undergrad/4ba2/multicast/antony/example.html
//
//  Unused examples, idk why i wrote them, but:
//      alsa:
//          http://www.alsa-project.org/alsa-doc/alsa-lib/_2test_2latency_8c-example.html#a37
//          http://www.alsa-project.org/alsa-doc/alsa-lib/_2test_2pcm_8c-example.html#a56
//          https://stackoverflow.com/questions/28562224/snd-pcm-hw-params-set-period-size-near-doesnt-return-the-good-value#28584181

#include <iostream>
#include <thread>
#include <mutex>
#include <zconf.h>
#include <portaudio.h>
#include <opus/opus.h>
#include <pulse/pulseaudio.h>
#include <vector>
#include <cmath>
#include <list>
#include <cstring>
#include <pulse/simple.h>
#include <pulse/error.h>
//
#include <streamsome/common.hh>
#include <streamsome/utils.hh>
#include <sstream>


using SAMPLE = opus_int16;
constexpr size_t SAMPLE_RATE = 48000;
constexpr size_t FRAME_SIZE = 480;
constexpr size_t CHANNELS = 2;
constexpr uint16_t PORT = 59010;
ENetHost *server;

#define CHECK(x) { if(!(x)) { \
fprintf(stderr, "%s:%i: failure at: %s\n", __FILE__, __LINE__, #x); \
_exit(1); } }

static inline void log_pa(PaError err, bool isFatal = false) {
    std::cerr<<"PortAudio: "<<Pa_GetErrorText(err)<<std::endl;
    if (err != paNoError && isFatal)
        exit(err);
}

void fatalError(std::string s1, std::string s2 = "") {
    if (s2 == "") {
        perror(s1.c_str());
    } else {
        std::clog<<s1<<s2<<std::endl;
    }
    exit(-1);
}

void log(std::string s1, bool isFatal = false) {
    std::clog<<(isFatal?"Error: ":"Warn: ")<<s1<<std::endl;
    if (isFatal)
        exit(-1);
}

char *ipToString(uint32_t hostNet) {
    uint32_t v2 = ntohl(hostNet);
    char *result;
    asprintf(&result, "%d.%d.%d.%d",
             (uint8_t) (v2>>24),
             (uint8_t) (v2>>16),
             (uint8_t) (v2>>8),
             (uint8_t) (v2));
    return result;
}


template<class T>
class circular_buffer {
public:
    circular_buffer(size_t size) :
            buf_(std::unique_ptr<T[]>(new T[size])),
            size_(size) {
        //empty constructor
    }

    void put(T item) {
        std::lock_guard<std::mutex> lock(mutex_);

        buf_[head_] = item;
        head_ = (head_ + 1)%size_;

        if (head_ == tail_) {
            tail_ = (tail_ + 1)%size_;
        }
    }

    T get(void) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (isEmpty()) {
            return T();
        }

        //Read data and advance the tail (we now have a free space)
        auto val = buf_[tail_];
        tail_ = (tail_ + 1)%size_;

        return val;
    }

    void reset(void) {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = tail_;
    }

    bool isEmpty(void) {
        //if head and tail are equal, we are empty
        return head_ == tail_;
    }

    bool isFull(void) {
        //If tail is ahead the head by 1, we are full
        return ((head_ + 1)%size_) == tail_;
    }

    size_t size(void) {
        return size_ - 1;
    }

private:
    std::mutex mutex_;
    std::unique_ptr<T[]> buf_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t size_;
};


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

struct Connection {
    unsigned int protocolVersion;
    bool protocolFull;
    bool optionReliable;
    unsigned int optionCompress;
    bool dataRequested;
    ENetPeer *peer;
    time_t timeTag;

    std::string str() {
        std::ostringstream result;
        result<<ipToString(this->peer->address.host) << ":" << this->peer->address.port;
        return result.str();
    }
};

std::list<Connection> clientConn;
OpusEncoder *opusEnc[6];
uint32_t seqNum = 0;
bool swOptionAltRate = false;

int openedTimes = 0;


circular_buffer<SAMPLE> CaptureBuffer(FRAME_SIZE*CHANNELS*2);
bool lastBufferNull = false;

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
    std::clog << "Removed " << removed << " outgoing messages" << std::endl;
}


//TODO UNDONE
// local variable allocation has failed, the output may be wrong!
void handleDataRequest(Connection& i, MsgHeader *header)
{
    std::clog<<"header->flags = 0x"<<std::hex<<header->flags<<std::dec<<std::endl;
    if (header->flags & 1) {
        i.optionReliable = true;
        puts("Sending audio data in reliable mode");
    } else {
        i.optionReliable = false;
        puts("Sending audio data in best-effort mode");
    }
    // Mask    | Descriprion
    // --------+-------------------------
    // [ 000 ] | без сжатия
    // [ 140 ] | сжатие 20  kbps
    // [ 040 ] | сжатие 64  kbps
    // [ 080 ] | сжатие 128 kbps
    // [ 0c0 ] | сжатие 256 kbps
    int v3 = 0;
    if (header->flags & 0x80) {
        v3 = 1<<1;
    }

    int v4 = v3 | ((header->flags & 0x40) != 0);

    int v5 = 0;
    if (header->flags & 0x100) {
        v5 = 1<<2;
        puts("Low quality (20 kbps)");
    }
    i.optionCompress = v4 | v5;
    if (i.optionCompress)
        std::cout << "Sending compressed audio data to host " << i.str()
                << " at rate code "<<i.optionCompress
                << std::endl;
    else
        std::cout << "Sending uncompressed audio data to host " << i.str()
                << std::endl;
    i.dataRequested = true;
}


//TODO UNDONE
static int recordCallback(
        [[gnu::unused]] const void *inputBuffer,
        [[gnu::unused]] void *outputBuffer,
        [[gnu::unused]] unsigned long framesPerBuffer,
        [[gnu::unused]] const PaStreamCallbackTimeInfo *timeInfo,
        [[gnu::unused]] PaStreamCallbackFlags statusFlags,
        [[gnu::unused]] void *userData)
{
    if (inputBuffer) {
        for(size_t i = 0; i < framesPerBuffer*CHANNELS; i++) {
            CaptureBuffer.put(reinterpret_cast<const SAMPLE *>(inputBuffer)[i]);
        }
        lastBufferNull = false;
    } else {
        if (!lastBufferNull) {
            std::cerr << "Record callback received null buffer" << std::endl;
            lastBufferNull = true;
        }
        for(size_t i = 0; i < framesPerBuffer*CHANNELS; i++) {
            CaptureBuffer.put(0);
        }
    }


    size_t s_buffer_size = sizeof(MsgHeader)+FRAME_SIZE*CHANNELS*sizeof(SAMPLE);
    uint8_t *s_buffer = new uint8_t[s_buffer_size];
    MsgHeader *s_header = reinterpret_cast<MsgHeader *>(s_buffer);
    SAMPLE *s_samples = reinterpret_cast<SAMPLE *>(s_buffer + sizeof(MsgHeader));

    bool bitRateUsed[6] {false};
    ssize_t s_compressed_size_max = 1352;
    ssize_t s_compressed_size = 0;
    uint8_t s_compressed[s_compressed_size_max];
    int status;
    ENetPacket *packet;


    // Fill samples from our ring buffer
    for(size_t i=0; !CaptureBuffer.isEmpty(); i++) {
        s_samples[i] = CaptureBuffer.get();
    }

    // Compress if necessary
    for (auto& i : clientConn) {
        if (i.peer
            && i.dataRequested
            && i.peer->state == ENET_PEER_STATE_CONNECTED
            && i.optionCompress) {
            bitRateUsed[i.optionCompress] = true;
            std::clog<<"asdasd: "<<i.peer->state<<std::endl;
        }
    }
    for (int e = 1; e < 6; e++) {
        if (bitRateUsed[e]) {
            if (opusEnc[e]) {
                s_compressed_size = opus_encode(opusEnc[e],
                                                s_samples,
                                                FRAME_SIZE,
                                                s_compressed,
                                                s_compressed_size_max);
                if (s_compressed_size > 0)
                    s_compressed_size += sizeof(MsgHeader);
            }
        }
    }

    /// Fill the header
    s_header->seqnum = seqNum++;

    for (auto& i : clientConn) {
        if (i.peer
            && i.dataRequested
            && i.peer->state == ENET_PEER_STATE_CONNECTED) {
            if (i.optionCompress
                &&  s_compressed_size > 0) {
                memcpy(s_samples, s_compressed, s_compressed_size_max);
                s_buffer_size = s_compressed_size;
            }

            //Обработка флагов
            if (i.optionReliable)
                s_header->flags |= 1u<<0u;
            if (swOptionAltRate)
                s_header->flags |= 1u<<1u;

            s_header->flags &= 0xFFFFFE3F;

            if (i.optionCompress & 1u<<0u)
                s_header->flags |= 8u<<3u;
            if (i.optionCompress & 1u<<1u)
                s_header->flags |= 8u<<4u;
            if (i.optionCompress & 1u<<2u)
                s_header->flags |= 8u<<5u;

            if (s_header->seqnum % 10 == 0) {
                size_t queueSize = enet_list_size(&i.peer->outgoingReliableCommands);
                if (i.optionReliable && queueSize > 75) {
                    std::cerr << "Clearing outgoing msg queue, outgoing/sent queue sizes: "
                            << queueSize << "/" << enet_list_size(&i.peer->sentReliableCommands) << std::endl;
                    clearSendQueue(i.peer, &i.peer->outgoingReliableCommands);
                } else if (!(s_header->seqnum%0x64) && queueSize > 1) {
                    std::clog << "Outgoing/sent queue sizes: "
                            << queueSize << "/" << enet_list_size(&i.peer->sentReliableCommands) << std::endl;
                }
            }
            packet = enet_packet_create(s_buffer, s_buffer_size, (i.optionReliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT));
            enet_peer_send(i.peer, 0, packet);
            enet_host_flush(server);
        }
    }

    /// Requested event handling
    ENetEvent event;
    status = enet_host_service(server, &event, 1);
    if (status < 0) {
        puts("Error returned from service call");
        return paComplete;
    }
    switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            Connection c{};
            c.peer = event.peer;
            c.protocolVersion = *reinterpret_cast<uint8_t *>(&event.data);
            c.protocolFull = event.data<0;
            c.optionReliable = true;
            c.optionCompress = 0;
            c.dataRequested = false;
            c.timeTag = time(nullptr);
            clientConn.push_back(c);
            std::cout << "A new host (connected " << clientConn.size() << ") connected from " << c.str() << " (prot ver " << +c.protocolVersion << ")" << std::endl;
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            for(Connection& i : clientConn) {
                if (i.peer->address.host == event.peer->address.host
                    && i.peer->address.port == event.peer->address.port) {
                    MsgHeader *header = reinterpret_cast<MsgHeader *>(event.packet->data + 0);
                    switch(header->id) {
                        case MsgDataRequest:
                            std::cout << "Got data request" << std::endl;
                            handleDataRequest(i, header);
                            break;
                        case MsgDataFaster:
                            std::cout << "Got faster request" << std::endl;
                            break;
                        case MsgDataSlower:
                            std::cout << "Got slower request" << std::endl;
                            break;
                        case MsgDataPing:
                            s_header->id = MsgDataPing;
                            std::cout << "Got ping request" << std::endl;
                            packet = enet_packet_create(s_buffer, sizeof(s_header), ENET_PACKET_FLAG_RELIABLE);
                            enet_peer_send(i.peer, 0, packet);
                            enet_host_flush(server);
                            break;
                        default:
                            std::cout << "Got unrecognized request: " << header->id << std::endl;
                            break;
                    }
                }
            }
            enet_packet_destroy(event.packet);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            auto i = clientConn.begin();
            while(i != clientConn.end()) {
                auto cur = i++;
                if (cur->peer->address.host == event.peer->address.host
                    && cur->peer->address.port == event.peer->address.port) {
                    std::clog << "Client " << cur->str() << " disconnected" << std::endl;
                    clientConn.erase(cur);
                }
            }
            // event.peer -> data = NULL;
            break;
        }
        default:
            break;
    }
    return paContinue;
}


/* The Sample format to use */

PaStream *
Open() {
    int error;
    PaError err;
    /* Create a new playback stream */
    //s = pa_simple_new(NULL,               // Use the default server.
    //                  "StreamSome",       // Our application's name.
    //                  PA_STREAM_NODIRECTION,
    //                  NULL,               // Use the default device.
    //                  "playback",         // Description of our stream.
    //                  &ss,                // Our sample format.
    //                  NULL,               // Use default channel map
    //                  NULL,               // Use default buffering attributes.
    //                  &error
    //);
    //if (!s) {
    //    fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
    //}
    printf( "PortAudio version: 0x%08X\n", Pa_GetVersion());
    printf( "Version text: '%s'\n", Pa_GetVersionInfo()->versionText );

    /* Initialize. */
    CHECK(Pa_Initialize() == paNoError);
    atexit([](){ Pa_Terminate(); });


    PaStreamParameters input_parameters;
    /* Set record device (monitor of Default). */
    std::cout<<""<<std::endl;
    std::cout<<"Pa_GetDeviceCount(): "<<Pa_GetDeviceCount()<<std::endl;
    std::cout<<"Pa_GetDefaultInputDevice(): "<<Pa_GetDefaultInputDevice()<<std::endl;
    std::cout<<"Pa_GetDefaultOutputDevice(): "<<Pa_GetDefaultOutputDevice()<<std::endl;
    std::cout<<""<<std::endl;
    input_parameters.device = Pa_GetDefaultInputDevice();
    CHECK(input_parameters.device != paNoDevice);
    CHECK(input_parameters.device != paUseHostApiSpecificDeviceSpecification);
    input_parameters.channelCount = CHANNELS;
    input_parameters.sampleFormat = paInt16;
    const PaDeviceInfo *input_info = Pa_GetDeviceInfo(input_parameters.device);
    CHECK(input_info != nullptr);
    input_parameters.suggestedLatency = input_info->defaultLowInputLatency;
    input_parameters.hostApiSpecificStreamInfo = NULL;


    std::clog << "input_info->hostApi: " << input_info->hostApi << std::endl;
    std::clog << "input_info->name: " << input_info->name << std::endl;
    std::clog << "input_info->defaultSampleRate: "<<input_info->defaultSampleRate << std::endl;

    /* Create and start stream */
    PaStream *stream;
    err = Pa_OpenStream(
            &stream,
            &input_parameters,
            nullptr,
            input_info->defaultSampleRate,
            FRAME_SIZE,
            paClipOff,
            recordCallback,
            nullptr
    );
    if (err != paNoError) {
        std::cerr << "Pa_OpenStream failed: (err " << err << ":" << Pa_GetErrorText(err)<< ")" << std::endl;
        if (stream)
            Pa_CloseStream(stream);
        exit(1);
        return nullptr;
    }

    CHECK(Pa_StartStream(stream) == paNoError);
    openedTimes++;

    const PaStreamInfo *streamInfo = Pa_GetStreamInfo(stream);
    std::clog << "[!] Audio stream info: sample rate = " << streamInfo->sampleRate
            <<", input latency = " << streamInfo->inputLatency << " sec"
            << std::endl;

    return stream;
}


void enetHost()
{
    int opus_err = OPUS_OK;

    for(int i = 1; i < 6; i++) {
        opusEnc[i] = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &opus_err);
        if (opus_err) {
            opusEnc[i] = nullptr;
            std::cerr << "Warning: could not initialize Opus encoder #"<<i<<" (" << opus_strerror(opus_err) << ")" << std::endl;
            continue;
        }
        int bitrate;
        switch(i) {
            case 1:
                bitrate = 64000;
                break;
            case 3:
                bitrate = 256000;
                break;
            case 5:
                bitrate = 20000;
                break;
            default:
                bitrate = 128000;
        }
        opus_encoder_ctl(opusEnc[i], OPUS_SET_BITRATE(bitrate));
        opus_encoder_ctl(opusEnc[i], OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    }

    std::cout << "Initializing server network communication" << std::endl;
    if (enet_initialize() != 0)
        fatalError("An error occurred while initializing network communication");
    atexit(enet_deinitialize);

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = PORT;
    std::cout << "Using default port number " << address.port << std::endl;

    errno = 0;
    server = enet_host_create(
            &address, // the address to bind the server host to
            32,       // allow up to 32 clients and/or outgoing connections
            2,        // allow up to 2 channels to be used, 0 and 1
            0,        // assume any amount of incoming bandwidth
            0         // assume any amount of outgoing bandwidth
    );
    if (!server) {
        std::clog<<"Error: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }

    PaStream *stream = Open();

    while(true) {
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

int
main(int argc, char *argv[])
{
    SS::print_ipv4s();
    std::thread audioThread(enetHost);
    audioThread.join();
    return 0;
}

#pragma clang diagnostic pop
