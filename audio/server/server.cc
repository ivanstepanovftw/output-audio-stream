#include <list>
#include <sstream>
#include <opus/opus.h>
#include <iostream>
#include <thread>
#include <boost/asio/ip/address.hpp>

#include <streamsome/pulseaudio.hh>
#include <streamsome/enet.hh>
#include <streamsome/utils.hh>

using namespace std;


using SAMPLE = opus_int16;
constexpr size_t SAMPLE_RATE = 48000;
bool swOptionAltRate = SAMPLE_RATE == 48000;
constexpr size_t FRAME_SIZE = SAMPLE_RATE/100;
constexpr size_t CHANNELS = 2;
constexpr uint16_t PORT = 59010;

ENetHost *server;
OpusEncoder *opusEnc[6];


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
        result << boost::asio::ip::address_v4(this->peer->address.host).to_string() << ":" << this->peer->address.port;
        return result.str();
    }
};

std::list<Connection> clientConn;




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



bool lastBufferNull = false;
size_t seqNum = 0;

/**
 *
 * return code:
 *   0: continue
 *   non-zero: stop calling handler
 */
size_t ss_callback(const void *inputBuffer)
{
    size_t s_buffer_size = sizeof(MsgHeader)+FRAME_SIZE*CHANNELS*sizeof(SAMPLE);

    uint8_t *s_buffer = new uint8_t[s_buffer_size];
    MsgHeader *s_header = reinterpret_cast<MsgHeader *>(s_buffer);
    SAMPLE *s_samples = reinterpret_cast<SAMPLE *>(s_buffer + sizeof(MsgHeader));

    bzero(s_header, sizeof(MsgHeader));
    memcpy(s_samples, inputBuffer, FRAME_SIZE*CHANNELS*sizeof(SAMPLE));

    int status;
    ENetPacket *packet;

    /// Fill the header
    s_header->seqnum = seqNum++;


    for (auto& i : clientConn) {
        if (i.dataRequested
        &&  i.peer->state == ENET_PEER_STATE_CONNECTED) {
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
                    //std::cerr << "Clearing outgoing msg queue, outgoing/sent queue sizes: "
                    //        << queueSize << "/" << enet_list_size(&i.peer->sentReliableCommands) << std::endl;
                    SS::clearSendQueue(i.peer, &i.peer->outgoingReliableCommands);
                } else if (!(s_header->seqnum%0x64) && queueSize > 1) {
                    //std::clog << "Outgoing/sent queue sizes: "
                    //        << queueSize << "/" << enet_list_size(&i.peer->sentReliableCommands) << std::endl;
                }
                if (s_header->seqnum % 100 == 0)
                    cout<<"sending packet, .seqnum="<<s_header->seqnum<<", .port="<<i.peer->address.port<<endl;
            }
            packet = enet_packet_create(s_buffer, s_buffer_size, (i.optionReliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT));
            enet_peer_send(i.peer, 0, packet);
            enet_host_flush(server);
        }
    }

    /// Requested event handling
    ENetEvent event {};
    status = enet_host_service(server, &event, 1);
    if (status < 0) {
        puts("Error returned from service call");
        return 1;
    }
    switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            Connection c{};
            c.peer = event.peer;
            c.protocolVersion = *reinterpret_cast<uint8_t *>(&event.data);
            c.protocolFull = false;
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
                            enet_peer_send(i.peer, 0, packet); // enet_packet_destroy() is not necessary
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
                &&  cur->peer->address.port == event.peer->address.port) {
                    std::clog << "Client " << cur->str() << " disconnected" << std::endl;
                    clientConn.erase(cur);
                }
            }
            break;
        }
        default:
            break;
    }

    delete[] s_buffer;

    return 0;
}

int enetHost() {

    int opus_err = OPUS_OK;

    for(int i = 1; i < 6; i++) {
        int bitrate;
        switch(i) {
            case 1: bitrate = 64'000; break;
            case 2: bitrate = 128'000; break;
            case 3: bitrate = 256'000; break;
            case 5: bitrate = 20'000; break;
            default: continue;
        }
        opusEnc[i] = opus_encoder_create(48000, CHANNELS, OPUS_APPLICATION_AUDIO, &opus_err);
        if (opus_err) {
            opusEnc[i] = nullptr;
            std::cerr << "Warning: could not initialize Opus encoder #"<<i<<" (" << opus_strerror(opus_err) << ")" << std::endl;
            continue;
        }
        opus_encoder_ctl(opusEnc[i], OPUS_SET_BITRATE(bitrate));
        opus_encoder_ctl(opusEnc[i], OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    }

    // ENet initialization
    if (enet_initialize() != 0) {
        std::clog << "An error occurred while initializing network communication" << std::endl;
        exit(-1);
    }
    atexit(enet_deinitialize);

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = PORT;

    errno = 0;
    server = enet_host_create(
            &address, // the address to bind the server host to
            32,       // allow up to 32 clients and/or outgoing connections
            1,        // allow up to 2 channels to be used, 0 and 1
            0,        // assume any amount of incoming bandwidth
            0         // assume any amount of outgoing bandwidth
    );
    if (!server) {
        std::clog << "Error: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }


    // This is where we'll store the devices
    SS::pa_devicelist_t pa_input_devicelist[16];
    SS::pa_devicelist_t pa_output_devicelist[16];

    if (pa_get_devicelist(pa_input_devicelist, pa_output_devicelist) < 0) {
        fprintf(stderr, "failed to get device list\n");
        return 1;
    }

    //for (int ctr = 0; ctr < 16; ctr++) {
    //    if (! pa_output_devicelist[ctr].initialized) {
    //        break;
    //    }
    //    printf("=======[ Output Device #%d ]=======\n", ctr);
    //    printf("Description: %s\n", pa_output_devicelist[ctr].description);
    //    printf("Name: %s\n", pa_output_devicelist[ctr].name);
    //    printf("Index: %d\n", pa_output_devicelist[ctr].index);
    //    printf("\n");
    //}
    //
    //for (int ctr = 0; ctr < 16; ctr++) {
    //    if (! pa_input_devicelist[ctr].initialized) {
    //        break;
    //    }
    //    printf("=======[ Input Device #%d ]=======\n", ctr);
    //    printf("Description: %s\n", pa_input_devicelist[ctr].description);
    //    printf("Name: %s\n", pa_input_devicelist[ctr].name);
    //    printf("Index: %d\n", pa_input_devicelist[ctr].index);
    //    printf("\n");
    //}

    // We got devices, lets determine monitor
    std::string device_entity;
    for (auto& out : pa_output_devicelist) {
        if (!out.initialized) break;
        if (!device_entity.empty()) break;
        for (auto& in : pa_input_devicelist) {
            if (!in.initialized) break;
            if (!device_entity.empty()) break;
            if (std::string(in.name).find(std::string(out.name)) != std::string::npos) {
                device_entity = in.name;
            }
        }
    }
    // In case if device still not found
    for (auto& in : pa_input_devicelist) {
        if (!in.initialized) break;
        if (!device_entity.empty()) break;
        std::string s = in.name;
        std::string suffix = ".monitor";
        //if (std::string(in.name).ends_with(".monitor")) { // TODO[c++20]
        if (s.size() >= suffix.size() && 0 == s.compare(s.size()-suffix.size(), suffix.size(), suffix))
            device_entity = in.name;
    }
    // In case if device still not found
    for (auto& in : pa_input_devicelist) {
        if (!in.initialized) break;
        if (!device_entity.empty()) break;
        device_entity = in.name;
    }
    std::cout << "Using device: " << device_entity << std::endl;

    // PulseAudio initialization
    SAMPLE *buf = new SAMPLE[FRAME_SIZE*CHANNELS];
    pa_sample_spec ss {};
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = SAMPLE_RATE;
    ss.channels = CHANNELS;
    //PA_STREAM_ADJUST_LATENCY
    pa_simple *s = NULL;
    int error;
    /* Create the recording stream */
    if (!(s = pa_simple_new(
            NULL,
            "StreamSome",
            PA_STREAM_RECORD,
            device_entity.c_str(),
            "simple linux backdoor example",
            &ss,
            NULL,
            NULL,
            &error)))
    {
        fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
        return 1;
    }

    for (;;) {
        // record some data...
        memset(buf, 0, FRAME_SIZE*CHANNELS*sizeof(SAMPLE));
        if (pa_simple_read(s, buf, FRAME_SIZE*CHANNELS*sizeof(SAMPLE), &error) < 0) {
            fprintf(stderr, __FILE__": pa_simple_read() failed: %s\n", pa_strerror(error));
            return 1;
        }
        // ...and call function
        if (ss_callback(buf) != 0)
            break;
    }
    if (s)
        pa_simple_free(s);

    for(size_t i=1; i<6; i++) {
        opus_encoder_destroy(opusEnc[i]);
    }

    delete[] buf;
    return 0;
}


/**
 * Execute command, return output
 */
static
std::string
execute(const std::string& cmd)
{
    char buffer[256];
    std::string result;
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        throw std::runtime_error("popen() failed!");
    try {
        while (!feof(pipe))
            if (fgets(buffer, 256, pipe) != nullptr)
                result += buffer;
    } catch (...) {
        pclose(pipe);
        throw;
    }
    pclose(pipe);
    return result;
}


int main(int argc, char *argv[]) {
    using namespace std;
    using namespace std::string_literals;

    /// Terminate other instances of this executable.
    /// TODO: idk is it good practice, maybe we just should suggest that command if port is in use?
    string kill_instances = ""s+"(kill $(pgrep -f '" + argv[0] + "$' | head -n -1) 2>&1) >/dev/null";
    clog<<kill_instances<<endl;
    clog<<execute(kill_instances)<<endl;

    //TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TO TODO boost::asio::ip::make_address("");

    SS::print_ipv4s();
    cout<<"#2. ip: " <<SS::getIPAddress()<<endl;

    std::thread audioThread(enetHost);
    audioThread.join();


    return 0;
}
