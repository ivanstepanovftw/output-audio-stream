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

using namespace std;
using namespace chrono;

#define SAMPLE opus_int16

#define UNUSED(x) ((void)(x))
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

ENetHost *host;
OpusDecoder *decoder;

enet_uint16 PORT            = 59010;
int         SAMPLE_RATE     = 48000;
int         FRAME_SIZE      = 480;
int         CHANNELS        = 2;
int         MAX_FRAME_SIZE  = 0;
#define LEN(a) (sizeof(a)/sizeof(*(a)))



/**
* Important Usage Note: This library reserves one spare entry for queue-full detection
* Otherwise, corner cases and detecting difference between full/empty is hard.
* You are not seeing an accidental off-by-one.
*/
ENetPeer *peer;
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
		return size_;
	}

private:
	std::mutex mutex_;
	std::unique_ptr<T[]> buf_;
	size_t head_ = 0;
	size_t tail_ = 0;
	size_t size_;
};

circular_buffer<SAMPLE> *CaptureBuffer = nullptr;

bool isIdle = true;

int openedTimes = 0;

static int playCallback(
		const void *inputBuffer, void *outputBuffer,
		unsigned long framesPerBuffer,
		const PaStreamCallbackTimeInfo *timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userData) {
	UNUSED(inputBuffer); UNUSED(timeInfo); UNUSED(statusFlags); UNUSED(userData);
	SAMPLE *out = (SAMPLE *) outputBuffer;
	
	ENetEvent event;
	MsgHeader *h;
	SAMPLE *b;
	int b_frames = 0;
	int status;
	while (true) {
		status = enet_host_service(host, &event, 0);
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
			b_frames = (int)(event.packet->dataLength/CHANNELS/sizeof(SAMPLE))-sizeof(MsgHeader);
			//todo msgheader contains channels
			
			for(int i=0; i<FRAME_SIZE*CHANNELS; i++) {
				CaptureBuffer->put(b[i]);
			}
			
			if (CaptureBuffer->isEmpty())
				isIdle = true;
			else if (CaptureBuffer->isFull())
				isIdle = false;
			
			if (isIdle) {
				break;
			}
			
			for(int i=0; i<framesPerBuffer*CHANNELS;) {
				out[i++] = CaptureBuffer->get();
			}
			
			enet_packet_destroy(event.packet);
			return paContinue;
		
		case ENET_EVENT_TYPE_DISCONNECT:
			puts("Disconnection succeeded.");
			break;
		
		case ENET_EVENT_TYPE_NONE:
//			puts("None");
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
	
	memset(out, 0, sizeof(SAMPLE)*framesPerBuffer*CHANNELS);
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
	//создаём клиента ENet
	host = enet_host_create(nullptr   /* create a host                                */,
	                        1         /* only allow 1 outgoing connection             */,
	                        2         /* allow up to 2 channels to be used, 0 and 1   */,
	                        0         /* assume any amount of incoming bandwidth      */,
	                        0         /* assume any amount of outgoing bandwidth      */);
	if (!host) {
		fprintf(stderr, "An error occurred while trying to create an ENet host host.\n");
		exit(EXIT_FAILURE);
	}
	
	//подключаемся
	const char* ip = "192.168.0.100";
	ENetAddress address;
	enet_address_set_host(&address, ip);
	address.port = PORT;
	clog<<"Connecting to "<<ip<<":"<<address.port<<endl;
	peer = enet_host_connect(host, &address, 2, 0/*42*/);
	CHECK(peer != nullptr);
	
	MsgHeader gotHeader;
	ENetEvent event;
	long ping = -1;
	high_resolution_clock::time_point t1;
	
	while (ping < 0) {
		int status = enet_host_service(host, &event, 1);
		if (status < 0) {
			puts("Error returned from service call");
			return 22;
		}
		
		switch(event.type) {
			case ENET_EVENT_TYPE_RECEIVE:
				gotHeader.id = ((MsgHeader *) event.packet->data)->id;
				gotHeader.flags = ((MsgHeader *) event.packet->data)->flags;
				switch(gotHeader.id) {
					case MsgSoundData:break;
					case MsgDataRequest:break;
					case MsgDataFaster:break;
					case MsgDataSlower:break;
					case MsgDataPing:
						ping = duration_cast<duration<long>>(high_resolution_clock::now() - t1).count();
						clog<<"ping: "<<ping<<" ms."<<endl;
						clog<<"formula: samples = ms * sample_rate"<<endl;
						MAX_FRAME_SIZE = ((int)((ping*SAMPLE_RATE)/FRAME_SIZE)+2)*FRAME_SIZE;
						clog<<"formula: finally = "<<MAX_FRAME_SIZE<<endl;
						
						//allocate memory
						CaptureBuffer = new circular_buffer<SAMPLE>((size_t)(MAX_FRAME_SIZE));
						clog<<"CaptureBuffer.size():  "<<CaptureBuffer->size()<<endl;
						break;
				}
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				return -111;
			
			case ENET_EVENT_TYPE_NONE:
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
				
				usleep(50*1000);
				header.id = MsgDataPing;
				packet = enet_packet_create((void *)&header, sizeof(header), ENET_PACKET_FLAG_RELIABLE);
				enet_peer_send(peer, 0, packet);
				enet_host_flush(host);
				t1 = high_resolution_clock::now();
				clog<<"ping sent"<<endl;
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
			usleep(10*1000);
		}
	}
}
