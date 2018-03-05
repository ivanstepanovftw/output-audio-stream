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
#include <thread>
#include <iostream>
#include <zconf.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <portaudio.h>
#include <opus/opus.h>
#include <pulse/pulseaudio.h>
#include <enet/enet.h>
#include <vector>
#include <cmath>
#include <ifaddrs.h>
#include <cstring>
#include <mutex>
using namespace std;


#define LEN(a) (sizeof(a)/(sizeof(*(a))))
#define UNUSED(x) ((void)(x))

#define SAMPLE opus_int16
#define SAMPLE_RATE 48000
#define FRAME_SIZE 480
#define CHANNELS 2
#define MAX_PACKET_SIZE 1352
#define PORT 59010

#define CHECK(x) { if(!(x)) { \
fprintf(stderr, "%s:%i: failure at: %s\n", __FILE__, __LINE__, #x); \
_exit(1); } }

static inline void log_pa(PaError err, bool isFatal = false) {
	clog<<"PortAudio: "<<Pa_GetErrorText(err)<<endl;
	if (err != paNoError && isFatal)
		exit(err);
}

void fatalError(string s1, string s2 = "") {
	if (s2 == "") {
		perror(s1.c_str());
	} else {
		clog<<s1<<s2<<endl;
	}
	exit(-1);
}

void log(string s1, bool isFatal = false) {
	clog<<(isFatal?"Error: ":"Warn: ")<<s1<<endl;
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
		//isEmpty constructor
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
		//if head and tail are equal, we are isEmpty
		return head_ == tail_;
	}
	
	bool isFull(void) {
		//If tail is ahead the head by 1, we are isFull
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

/* Declaration */
circular_buffer<SAMPLE> CaptureBuffer(1920);
SAMPLE *buffer = (SAMPLE *) operator new[](1920+16);


bool lastBufferNull = false;

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
};

Connection clientConn[10];
OpusEncoder *opusEnc[6];
uint32_t seqNum = 0;
bool swOptionAltRate = false;

int openedTimes = 0;


ENetHost *server;


//TODO UNDONE ...
void clearSendQueue(ENetPeer *peer, ENetList *queue) {
	//https://hackage.haskell.org/package/henet-1.3.9.3/src/enet/peer.c
	int removed = 0;
	ENetListNode *currentCommand = queue->sentinel.next;
	while((ENetListIterator) queue != currentCommand) {
		ENetOutgoingCommand *outgoingCommand = (ENetOutgoingCommand *) enet_list_remove(currentCommand);
		if (outgoingCommand->packet
		&& !--outgoingCommand->packet->referenceCount)
			enet_packet_destroy(outgoingCommand->packet);
		
		peer->outgoingDataTotal = peer->outgoingDataTotal
		                          - (enet_uint32) enet_protocol_command_size(outgoingCommand->command.header.command)
		                          - outgoingCommand->fragmentLength;
		ENetChannel *channel = &peer->channels[outgoingCommand->command.header.channelID];
		if (channel->outgoingReliableSequenceNumber >= outgoingCommand->reliableSequenceNumber)
			channel->outgoingReliableSequenceNumber = outgoingCommand->reliableSequenceNumber - (enet_uint16) 1;
		removed++;
		currentCommand = currentCommand->next;
		enet_free(outgoingCommand);
	}
	printf("Removed %d outgoing messages\n", removed);
}


//TODO UNDONE
// local variable allocation has failed, the output may be wrong!
void handleDataRequest(int i, MsgHeader *header) {
	clog<<"header->flags = 0x"<<hex<<header->flags<<endl;
	if (header->flags & 1) {
		clientConn[i].optionReliable = 1<<0;
		puts("Sending audio data in reliable mode");
	} else {
		clientConn[i].optionReliable = 0;
		puts("Sending audio data in best-effort mode");
	}
	// Mask    | Descriprion
	// --------+-------------------------
	// [ 000 ] | без сжатия
	// [ 140 ] | сжатие 20  kbps (voice)
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
		puts("Low quiality (20 kbps)");
	}
	clientConn[i].optionCompress = v4 | v5;
	if (clientConn[i].optionCompress)
		printf("Sending compressed audio data to host %d at rate code %d\n",
				i,
				clientConn[i].optionCompress);
	else
		printf("Sending uncompressed audio data to host %d\n", i);
	clientConn[i].dataRequested = 1;
}

//OpusDecoder *decoder;
//TODO UNDONE
static int recordCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo *timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData) {
	UNUSED(outputBuffer);
	UNUSED(timeInfo);
	UNUSED(statusFlags);
	UNUSED(userData);
	
	const SAMPLE *in = (const SAMPLE *) inputBuffer;
	bool audioCaptureActive = false;
	if (inputBuffer) {
		for(int i = 0; i < framesPerBuffer*CHANNELS; i++) {
			CaptureBuffer.put(in[i]);
		}
		audioCaptureActive = true;
		lastBufferNull = false;
	} else {
		if (!lastBufferNull) {
			puts("Record callback received null buffer");
			lastBufferNull = true;
		}
		for(int i = 0; i < framesPerBuffer*CHANNELS; i++) {
			CaptureBuffer.put(0);
		}
	}
	memset(buffer, 0, sizeof(MsgHeader));

	for(int i=0; !CaptureBuffer.isEmpty(); i++) {
		*(SAMPLE *) ((char *) &buffer[i] + sizeof(MsgHeader)) = CaptureBuffer.get();
	}
	
	unsigned long size = sizeof(MsgHeader) + sizeof(SAMPLE)*framesPerBuffer*CHANNELS;
	bool bitRateUsed[6] {false};
	uint8_t bufferComp[MAX_PACKET_SIZE];
	int bufferCompLen = 0;
	int status;
	int queueSize;
	ENetPacket *packet;
	time_t oldestTime;
	int oldesti;
	unsigned long sendSize;
	char *sendBuffer;
	
	for(int i = 0; i < 10; i++) {
		if (clientConn[i].peer
	    && clientConn[i].dataRequested
	    && clientConn[i].peer->state == ENET_PEER_STATE_CONNECTED
	    && clientConn[i].optionCompress)
			bitRateUsed[clientConn[i].optionCompress] = true;
	}
	for(int e = 1; e < 6; e++) {
		if (bitRateUsed[e]) {
			if (opusEnc[e]) {
				bufferCompLen = opus_encode(opusEnc[e],
				                            buffer + 8,
				                            FRAME_SIZE,
				                            &bufferComp[16],
				                            MAX_PACKET_SIZE);
				if (bufferCompLen > 0)
					bufferCompLen += sizeof(MsgHeader);
			}
		}
	}
	
	MsgHeader header {};
	header.seqnum = seqNum++;
//	cout<<openedTimes<<":"<<seqNum<<endl;
	if (swOptionAltRate)
		header.flags |= 2u;
	for(int i = 0; i < 10; i++) {
		if (clientConn[i].peer
	    && clientConn[i].dataRequested
	    && clientConn[i].peer->state == ENET_PEER_STATE_CONNECTED) {
			sendBuffer = (char *) buffer;
			sendSize = size;
			if (clientConn[i].optionCompress
			&&  bufferCompLen > 0) {
				sendBuffer = (char *) bufferComp;
				sendSize = bufferCompLen;
			}
			
			//Обработка флагов
			if (clientConn[i].optionReliable)
				header.flags |= 1u;
			else
				header.flags &= 0xFFFFFFFE;
			unsigned int v4;
			unsigned int v5;
			unsigned int v6;
			unsigned int v7;
			unsigned int v8;
			header.flags &= 0xFFFFFE3F;
			if (clientConn[i].optionCompress & 1<<0)
				v4 = 8<<3;
			else
				v4 = 0;
			header.flags |= v4;
			v5 = header.flags | v4;
			if (clientConn[i].optionCompress & 1<<1)
				v6 = 8<<4;
			else
				v6 = 0;
			header.flags = v5 | v6;
			v7 = v5 | v6;
			if (clientConn[i].optionCompress & 1<<2)
				v8 = 8<<5;
			else
				v8 = 0;
			header.flags = v7 | v8;
			
			*(MsgHeader *) sendBuffer = header;
			if (header.seqnum % 0xF == 0) {
				queueSize = (int) enet_list_size(&clientConn[i].peer->outgoingReliableCommands);
				if (clientConn[i].optionReliable && queueSize > 75) {
					printf("Clearing outgoing msg queue, outgoing/sent queue sizes: %d/%lu\n",
					       queueSize,
					       enet_list_size(&clientConn[i].peer->sentReliableCommands));
					clearSendQueue(clientConn[i].peer, &clientConn[i].peer->outgoingReliableCommands);
				} else if (!(header.seqnum%0x64) && queueSize > 1) {
					printf("Outgoing/sent queue sizes: %d/%lu\n",
					       queueSize,
					       enet_list_size(&clientConn[i].peer->sentReliableCommands));
				}
			}
			packet = enet_packet_create(sendBuffer, sendSize, (clientConn[i].optionReliable?
			                                                   ENET_PACKET_FLAG_RELIABLE:
			                                                   ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT));
			enet_peer_send(clientConn[i].peer, 0, packet);
			enet_host_flush(server);
		}
	}
	// обработка событий
	ENetEvent event;
	status = enet_host_service(server, &event, 1);
	
	if (status < 0) {
		puts("Error returned from service call");
		return paComplete;
	}
//	else if (status == 0)
//		return 0;
	
	int i;
	MsgHeader gotHeader;
	switch (event.type) {
		case ENET_EVENT_TYPE_CONNECT:
			printf("A new host connected from %s:%u (prot ver %d)\n",
			       ipToString(event.peer->address.host),
			       event.peer->address.port,
			       *((unsigned char*)&(event.data)));
			oldesti = 0;
			oldestTime = time(0LL) + 100;
			for(i = 0; i < 10 && clientConn[i].peer; i++) { //fixme тут всё правильно?
				if (clientConn[i].timeTag < oldestTime) {
					oldestTime = clientConn[i].timeTag;
					oldesti = i;
				}
			}
			if (i > 9) {
				i = oldesti;
				if (clientConn[i].peer->address.host != event.peer->address.host
			    ||  clientConn[i].peer->address.port != event.peer->address.port) {
					if (clientConn[i].protocolVersion)
						enet_peer_disconnect(clientConn[i].peer, 101);
				} else
					puts("Connect for same ip/port as current connection, skipping disconnect");
//				delete clientConn[i]; or ...?
				memset(&clientConn[i], 0, sizeof(clientConn[i]));
			}
//			for(int j = 0; j < 10; j++) {
//				if (clientConn[j].peer && clientConn[j].protocolFull != 1) {
//					if (clientConn[j].protocolVersion)
//						enet_peer_disconnect(clientConn[j].peer, 101);
//					memset(&clientConn[j], 0, sizeof(clientConn[j]));
//				}
//			}
			clientConn[i].peer = event.peer;
			clientConn[i].protocolVersion = *((unsigned char*)&(event.data));
			clientConn[i].protocolFull = event.data<0;
			clientConn[i].optionReliable = 1;
			clientConn[i].optionCompress = 0;
			clientConn[i].dataRequested = 0;
			clientConn[i].timeTag = time(0);
			return paContinue;
			
		case ENET_EVENT_TYPE_RECEIVE:
			for(i = 0; i < 10; i++) {
				if (clientConn[i].peer
			        && clientConn[i].peer->address.host == event.peer->address.host
			        && clientConn[i].peer->address.port == event.peer->address.port)
				{
					gotHeader.id = ((MsgHeader *) event.packet->data)->id;
					gotHeader.flags = ((MsgHeader *) event.packet->data)->flags;
					switch(gotHeader.id) {
						case MsgDataRequest:
							puts("Got Data request");
							handleDataRequest(i, (MsgHeader*) event.packet->data);
							break;
						case MsgDataFaster:
//							nextSleep = 1;
							puts("Got Faster request");
							break;
						case MsgDataSlower:
//							nextSleep = 44;
							puts("Got Slower request");
							break;
						case MsgDataPing:
							header.id = MsgDataPing;
							sendBuffer = new char[sizeof(header)];
							*(MsgHeader *)sendBuffer = header;
							clog<<"Got ping request, sizeof(sendBuffer)*sizeof(char): "<<sizeof(sendBuffer)*sizeof(char)<<endl;
							packet = enet_packet_create(sendBuffer, sizeof(sendBuffer)*sizeof(char), ENET_PACKET_FLAG_RELIABLE);
							enet_peer_send(clientConn[i].peer, 0, packet);
							enet_host_flush(server);
							break;
						default:
							printf("Got unrecognized message ID %d\n", gotHeader.id);
							break;
					}
				}
			}
			enet_packet_destroy(event.packet);
			return paContinue;
		
		case ENET_EVENT_TYPE_DISCONNECT:
			printf("Client %s:%u disconnected\n", ipToString(event.peer->address.host), event.peer->address.port);
			for(i = 0; i < 10; i++) {
				if (clientConn[i].peer
				    && clientConn[i].peer->address.host == event.peer->address.host
				    && clientConn[i].peer->address.port == event.peer->address.port) {
					memset(&clientConn[i], 0, sizeof(clientConn[i]));
					break;
				}
			}
//			event.peer -> data = NULL;
			return paContinue;
		
		default:
			return paContinue;
	}
}

//DONE
PaStream *Open() {
	/* Initialize. */
	CHECK(Pa_Initialize() == paNoError);
	
	PaStreamParameters input_parameters;
	/* Set record device (monitor of Default). */
	input_parameters.device = Pa_GetDefaultInputDevice();
	CHECK(input_parameters.device != paNoDevice);
	CHECK(input_parameters.device != paUseHostApiSpecificDeviceSpecification);
	input_parameters.channelCount = CHANNELS;
	input_parameters.sampleFormat = paInt16;
	const PaDeviceInfo *input_info = Pa_GetDeviceInfo(input_parameters.device);
	CHECK(input_info != nullptr);
	input_parameters.suggestedLatency = input_info->defaultLowInputLatency;
	input_parameters.hostApiSpecificStreamInfo = NULL;
	
	clog<<"Portaudio reported default input device sample rate is "<<input_info->defaultSampleRate
	    <<" (may not be accurate)"<<endl;
	
	/* Create and start stream */
	PaStream *stream;
	PaError ret = Pa_OpenStream(&stream,
	                            &input_parameters,
	                            NULL,
	                            input_info->defaultSampleRate,
	                            FRAME_SIZE,
	                            paClipOff,
	                            recordCallback,
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
	
//	int opus_err = OPUS_OK;
//	decoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &opus_err);
//	CHECK(opus_err == OPUS_OK);
	return stream;
}

//DONE
void enetHost() {
	int opus_err = OPUS_OK;
	
	for(int i = 1; i < 6; ++i) {
		if (i == 4) {
			opusEnc[i] = nullptr;
			continue;
		}
		opusEnc[i] = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &opus_err);
		if (opus_err) {
			opusEnc[i] = nullptr;
			printf("Warning, could not initialize Opus encoder %d (%s)\n", (unsigned int) i, opus_strerror(opus_err));
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
	
	puts("Initializing server network communication");
	if (enet_initialize() != 0)
		fatalError("An error occurred while initializing network communication");
	atexit(enet_deinitialize);
	
	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = PORT;
	printf("Using default port number %d\n", address.port);
	
	server = enet_host_create(&address  /* the address to bind the server host to             */,
	                          32        /* allow up to 32 clients and/or outgoing connections */,
	                          CHANNELS  /* allow up to 2 channels to be used, 0 and 1         */,
	                          0         /* assume any amount of incoming bandwidth            */,
	                          0         /* assume any amount of outgoing bandwidth            */);
	if (!server) {
		if (*__errno_location() == 98)
			clog<<"Network port busy (another SoundWire Server may already be running)"<<endl;
		else
			clog<<"An error occurred while creating network server host, network port may be busy (try reboot)"<<endl;
		exit(EXIT_FAILURE);
	}
	
	PaStream *stream = Open();
	
	while(true) {
		if (Pa_IsStreamActive(stream)) {
			usleep(10*1000);
		} else {
			Pa_StopStream(stream);
			stream = Open();
			usleep(10*1000);
		}
	}
}


char *result;
bool everFound = false;

char *GetIPAddress() {
	char buf[64];
	ifaddrs *myaddrs;
	in_addr *in_addr;
	sockaddr_in *s4;
	ifaddrs *ifa;
	bool found;
	
	found = 0;
	if (getifaddrs(&myaddrs) != 0) {
		perror("getifaddrs");
	} else {
		for(ifa = myaddrs; ifa; ifa = ifa->ifa_next) {
			if (ifa->ifa_addr && (ifa->ifa_flags & 1) == 1 && ifa->ifa_addr->sa_family == 2) {
				s4 = (sockaddr_in *) ifa->ifa_addr;
				in_addr = &s4->sin_addr;
				if (inet_ntop(ifa->ifa_addr->sa_family, &s4->sin_addr, buf, 0x40u) == 0LL) {
					printf("%s: inet_ntop failed!\n", ifa->ifa_name);
				} else if ((in_addr->s_addr & 0xFF) != 127) {
					if (found == 1) {
						if (everFound != 1)
							printf("Possible alternate IP address: %s\n", buf);
					} else {
						result = new char[64];
						strcpy(result, buf);
						found = 1;
						if (everFound != 1)
							printf("This server's probable IPv4 address: %s\n", buf);
					}
				}
			}
		}
		freeifaddrs(myaddrs);
	}
	if (found) {
		everFound = 1;
	} else {
		if (everFound != 1)
			puts("Could not determine this server's IPv4 address");
	}
	return result;
}


int main() {
	GetIPAddress();
	thread audioThread(enetHost);
	audioThread.join();
}
