//
//  Created by root on 11.11.17.
//
//  It is wifisound for linux, rewritten in c++.
//
//  Used examples:
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
//  Known issues:
//      when changing volume, sound distorts and shows "too big latency" warn
//
#include <thread>
#include <iostream>
#include <zconf.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <portaudio.h>
#include <opus/opus.h>

using namespace std;


/*The frame size is hardcoded for this sample code but it doesn't have to be*/
#define SAMPLE_RATE 48000
#define CHANNELS 2
#define BITRATE 64536
#define MAX_PACKET_SIZE (10*1276)


#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
//#define HELLO_GROUP "225.0.0.37"
//#define GROUP "239.0.0.1"
#define GROUP "192.168.0.101"
#define PORT 0x7D00

//FIXME: тут мб местами поменять index и name?
typedef struct pa_devicelist {
	uint8_t initialized;
	char name[512];
	uint32_t index;
	char description[256];
} pa_devicelist_t;

void fatalError(string s1, string s2 = "") {
	if (s2=="") {
		perror(s1.c_str());
	} else {
		clog<<s1<<s2<<endl;
	}
	exit(-1);
}

//DONE!!!
void pa_clientlist_cb(pa_context *c, const pa_source_output_info *i, int eol, void *userdata) {
	pa_devicelist_t *pa_devicelist = (pa_devicelist_t *) userdata;
	clog<<"sizeof(pa_devicelist[ctr])/sizeof(*pa_devicelist): "<<(sizeof(pa_devicelist)/sizeof(*pa_devicelist));
	for(int ctr = 0; ctr < 16 && ctr<sizeof(pa_devicelist)/sizeof(*pa_devicelist); ctr++) {
		if (! pa_devicelist[ctr].initialized) {
			strncpy(pa_devicelist[ctr].name, i->name, 511);
			cout<<"source = "<<i->source<<endl;
			pa_devicelist[ctr].index = i->index;
//			strncpy(pa_devicelist[ctr].description, i->description, 255);
			pa_devicelist[ctr].initialized = 1;
			break;
		}
	}
}

// pa_mainloop will call this function when it's ready to tell us about a sink.
// Since we're not threading, there's no need for mutexes on the devicelist
// structure
//DONE!!!
void pa_sinklist_cb(pa_context *c, const pa_sink_info *l, int eol, void *userdata) {
	pa_devicelist_t *pa_devicelist = (pa_devicelist_t *) userdata;
	
	// If eol is set to a positive number, you're at the end of the list
	if (eol > 0) {
		return;
	}
	
	// We know we've allocated 16 slots to hold devices.  Loop through our
	// structure and find the first one that's "uninitialized."  Copy the
	// contents into it and we're done.  If we receive more than 16 devices,
	// they're going to get dropped.  You could make this dynamically allocate
	// space for the device list, but this is a simple example.
	for(int ctr = 0; ctr < 16; ctr++) {
		if (! pa_devicelist[ctr].initialized) {
			strncpy(pa_devicelist[ctr].name, l->name, 511);
			strncpy(pa_devicelist[ctr].description, l->description, 255);
			pa_devicelist[ctr].index = l->index;
			pa_devicelist[ctr].initialized = 1;
			break;
		}
	}
}

// See above.  This callback is pretty much identical to the previous
//DONE!!!
void pa_sourcelist_cb(pa_context *c, const pa_source_info *l, int eol, void *userdata) {
	pa_devicelist_t *pa_devicelist = (pa_devicelist_t *) userdata;
	
	if (eol > 0) {
		return;
	}
	
	for(int ctr = 0; ctr < 16; ctr++) {
		if (! pa_devicelist[ctr].initialized) {
//			strncpy(pa_devicelist[ctr].name, l->name, sizeof(pa_devicelist[ctr].name));
			strncpy(pa_devicelist[ctr].name, l->name, 511);
			strncpy(pa_devicelist[ctr].description, l->description, 255);
			pa_devicelist[ctr].index = l->index;
			pa_devicelist[ctr].initialized = 1;
			break;
		}
	}
}

// This callback gets called when our context changes state.  We really only
// care about when it's ready or if it has failed
//DONE!!!
void pa_state_cb(pa_context *c, void *userdata) {
	pa_context_state_t state = pa_context_get_state(c);
	switch(state) {
		// There are just here for reference
		case PA_CONTEXT_UNCONNECTED:
		case PA_CONTEXT_CONNECTING:
		case PA_CONTEXT_AUTHORIZING:
		case PA_CONTEXT_SETTING_NAME:
		default:
			break;
		case PA_CONTEXT_FAILED:
		case PA_CONTEXT_TERMINATED:
			*(int64_t *) userdata = 2;
			break;
		case PA_CONTEXT_READY:
			*(int64_t *) userdata = 1;
			break;
	}
}

//DONE!!!
long pa_get_devicelist(pa_devicelist_t *input, pa_devicelist_t *output, void *userdata) {
	// Define our pulse audio loop and connection variables
	pa_mainloop *pa_ml;
	pa_mainloop_api *pa_mlapi;
	pa_operation *pa_op = nullptr;
	pa_context *pa_ctx;
	
	// We'll need these state variables to keep track of our requests
	int state = 0;
	int pa_ready = 0;
	
	// Initialize our device lists
	memset(input, 0, sizeof(pa_devicelist_t)*16);
	memset(output, 0, sizeof(pa_devicelist_t)*16);
	
	// Create a mainloop API and connection to the default server
	pa_ml = pa_mainloop_new();
	pa_mlapi = pa_mainloop_get_api(pa_ml);
	pa_ctx = pa_context_new(pa_mlapi, "test");
	
	// This function connects to the pulse server
	pa_context_connect(pa_ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
	
	// This function defines a callback so the server will tell us it's state. Our callback will wait for the state
	// to be ready.  The callback will modify the variable to 1 so we know when we have a connection and it's ready.
	// If there's an error, the callback will set pa_ready to 2
	pa_context_set_state_callback(pa_ctx, pa_state_cb, &pa_ready);
	
	// Now we'll enter into an infinite loop until we get the data we receive or if there's an error
	for(;;) {
		// We can't do anything until PA is ready, so just iterate the mainloop and continue
		if (pa_ready == 0) {
			pa_mainloop_iterate(pa_ml, 1, nullptr);
			continue;
		}
		// We couldn't get a connection to the server, so exit out
		if (pa_ready == 2) {
			pa_context_disconnect(pa_ctx);
			pa_context_unref(pa_ctx);
			pa_mainloop_free(pa_ml);
			return -1;
		}
		// At this point, we're connected to the server and ready to make requests
		switch(state) {
			// State 0: we haven't done anything yet
			case 0:
				// This sends an operation to the server.  pa_sinklist_info is our callback function and a pointer to
				// our be passed to the callback The operation ID is stored in the  pa_op variable
				pa_op = pa_context_get_sink_info_list(pa_ctx, pa_sinklist_cb, output);
				
				// Update state for next iteration through the loop
				state++;
				break;
			case 1:
				// Now we wait for our operation to complete.  When it's complete our pa_output_devicelist is filled
				// out, and we move along to the next state
				if (pa_operation_get_state(pa_op) == PA_OPERATION_DONE) {
					pa_operation_unref(pa_op);
					
					// Now we perform another operation to get the source
					// (input device) list just like before.  This time we pass
					// a pointer to our input structure
					pa_op = pa_context_get_source_info_list(pa_ctx, pa_sourcelist_cb, input);
					// Update the state so we know what to do next
					state++;
				}
				break;
			case 2:
				if (pa_operation_get_state(pa_op) == PA_OPERATION_DONE) {
					pa_operation_unref(pa_op);
//					pa_operation* pa_context_get_source_output_info_list(pa_context *c, pa_source_output_info_cb_t cb, void *userdata);
					pa_op = pa_context_get_source_output_info_list(pa_ctx, pa_clientlist_cb, userdata);
					++state;
				}
			case 3:
				if (pa_operation_get_state(pa_op) == PA_OPERATION_DONE) {
					// Now we're done, clean up and disconnect and return
					pa_operation_unref(pa_op);
					pa_context_disconnect(pa_ctx);
					pa_context_unref(pa_ctx);
					pa_mainloop_free(pa_ml);
					return 0;
				}
				break;
			default:
				// We should never see this state
				fprintf(stderr, "in state %d\n", state);
				return -1;
		}
		// Iterate the main loop and go again.  The second argument is whether or not the iteration should block until
		// something is ready to be done. Set it to zero for non-blocking.
		pa_mainloop_iterate(pa_ml, 1, nullptr);
	}
}

//DONE!!!
long pa_set_output(uint32_t idx, uint32_t source_idx) {
	pa_mainloop *pa_ml;
	pa_mainloop_api *pa_mlapi;
	pa_operation *pa_op = nullptr;
	pa_context *pa_ctx;
	
	int state = 0;
	int pa_ready = 0;
	
	pa_ml = pa_mainloop_new();
	pa_mlapi = pa_mainloop_get_api(pa_ml);
	pa_ctx = pa_context_new(pa_mlapi, "test");
	
	pa_context_connect(pa_ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
	pa_context_set_state_callback(pa_ctx, pa_state_cb, &pa_ready);
	
	for(;;) {
		if (pa_ready == 0) {
			pa_mainloop_iterate(pa_ml, 1, nullptr);
			continue;
		}
		if (pa_ready == 2) {
			pa_context_disconnect(pa_ctx);
			pa_context_unref(pa_ctx);
			pa_mainloop_free(pa_ml);
			return -1;
		}
		switch(state) {
			case 0:
				pa_op = pa_context_move_source_output_by_index(pa_ctx, idx, source_idx, nullptr, nullptr);
				state++;
				break;
			case 1:
				if (pa_operation_get_state(pa_op) == PA_OPERATION_DONE) {
					goto exit;
				}
				break;
			default:
				fprintf(stderr, "in state %d\n", state);
				return -1;
		}
		pa_mainloop_iterate(pa_ml, 1, nullptr);
	}
  exit:
	pa_operation_unref(pa_op);
	pa_context_disconnect(pa_ctx);
	pa_context_unref(pa_ctx);
	pa_mainloop_free(pa_ml);
	return 0;
}

//DONE!!!
int set_device_monitor() {
	// This is where we'll store the input device list
	pa_devicelist_t pa_input_devicelist[16];
	clog<<"487 must be "<<sizeof(char)<<"*12420 but "<<sizeof(pa_input_devicelist)<<endl;
	
	// This is where we'll store the output device list
	pa_devicelist_t pa_output_devicelist[16];
	clog<<"491 must be "<<sizeof(char)<<"*12420 but "<<sizeof(pa_output_devicelist)<<endl;
	
	char *userdata;
	pa_devicelist_t a1{};
	pa_devicelist_t a2{};
	
	if (pa_get_devicelist(pa_input_devicelist, pa_output_devicelist, &userdata) < 0) {
		cerr<<"failed to get device list\n"<<flush;
		return 1;
	}
	
	uint32_t v4 = -1;
	uint32_t v5 = -1;

//	for(ctr = 0; ctr < 16&&!pa_output_devicelist[ctr].initialized; ctr++) {
//	}
	for(int i = 0; i < 16 && pa_input_devicelist[i].initialized; i++) {
		if (strstr(pa_input_devicelist[i].name, "monitor")) {
//			v4 = *&pa_input_devicelist[776 * i + 516];
			v4 = pa_input_devicelist[i].index;
		}
	}
	
	pa_devicelist_t *userdata1 = (pa_devicelist_t *)&userdata;
	
	for(int i = 0; i < 16 && userdata1[i].initialized; i++) {
		if (!strstr(pa_input_devicelist[i].name, "WiFiAudio")) {
			v5 = pa_input_devicelist[i].index;
		}
	}
	
	if ((v5 & 0x80000000) == 0 && (v4 & 0x80000000) == 0)
		pa_set_output(v5, v4);
	
	return 0;
}

//FIXME UNDONE!!!
//void listenerTask() {
//	sockaddr_in addr;
//	int fd, nbytes, addrlen;
//	ip_mreq mreq;
//	char msgbuf[12512];
//
//	/* create what looks like an ordinary UDP socket */
//	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
//		perror("socket");
//		exit(1);
//	}
//
//	u_int yes = 1;
//	/* allow multiple sockets to use the same PORT number */
//	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
//		perror("Reusing ADDR failed");
//		exit(1);
//	}
//
//	/* set up destination address */
//	memset(&addr, 0, sizeof(addr));
//	addr.sin_family = AF_INET;
//	addr.sin_addr.s_addr = htonl(INADDR_ANY); /* N.B.: differs from sender */
//	addr.sin_port = htons(PORT);
//
//	/* bind to receive address */
//	if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
//		perror("bind");
//		exit(1);
//	}
//
//	/* use setsockopt() to request that the kernel join a multicast group */
//	mreq.imr_multiaddr.s_addr = inet_addr(HELLO_GROUP);
//	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
//	if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
//		perror("setsockopt");
//		exit(1);
//	}
//
//	cout<<"listenerTask launched successful"<<endl;
//	/* now just enter a read-print loop */
//	while(1) {
//		addrlen = sizeof(addr);
//		if ((nbytes = (int) recvfrom(fd, msgbuf, MSGBUFSIZE,
//		                             0, (sockaddr *) &addr, (socklen_t *) (&addrlen))) < 0) {
//			perror("recvfrom");
//			exit(1);
//		}
//		cout<<"Received "<<nbytes<<" bytes: "<<msgbuf<<endl;
//	}
//}

bool running = true;
size_t raw_size;
pa_usec_t latency = 1000;

int on_delay_value_changed(uint64_t us) {
	latency = us*1000;
	clog<<"latency set to "<<latency<<endl;
}

int on_readbuffer_value_changed(int readbuffer) {
	raw_size = 960;
	switch(readbuffer) {
		case 1:
			break;
		case 2:
			raw_size *= 2;
			break;
		case 3:
			raw_size *= 4;
			break;
		case 4:
			raw_size *= 8;
			break;
		default:
			raw_size *= 12;
			break;
	}
	clog<<"read buffer size set to "<<raw_size<<endl;
}

//DONE!!!
void senderTask() {
	/* create what looks like an ordinary UDP socket */
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		fatalError("socket");
	
	/* set up destination address */
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(GROUP);
	addr.sin_port = htons(PORT);
	
	clog<<"Streaming Audio Data to ["<<GROUP<<"]. Press any key to exit!!!"<<endl;
	if (sendto(fd, "s", 2, 0, (struct sockaddr *) &addr, sizeof(addr)) < 0)
		fatalError("sendto");
	
	/* The Sample format to use */
//	clog<<"PA_SAMPLE_U8: "<<PA_SAMPLE_U8<<endl;
//	clog<<"PA_SAMPLE_ALAW: "<<PA_SAMPLE_ALAW<<endl;
//	clog<<"PA_SAMPLE_ULAW: "<<PA_SAMPLE_ULAW<<endl;
//	clog<<"PA_SAMPLE_S16LE: "<<PA_SAMPLE_S16LE<<endl;
//	clog<<"PA_SAMPLE_S16BE: "<<PA_SAMPLE_S16BE<<endl;
//	clog<<"PA_SAMPLE_FLOAT32LE: "<<PA_SAMPLE_FLOAT32LE<<endl;
//	clog<<"PA_SAMPLE_FLOAT32BE: "<<PA_SAMPLE_FLOAT32BE<<endl;
//	clog<<"PA_SAMPLE_S32LE: "<<PA_SAMPLE_S32LE<<endl;
//	clog<<"PA_SAMPLE_S32BE: "<<PA_SAMPLE_S32BE<<endl;
//	clog<<"PA_SAMPLE_S24LE: "<<PA_SAMPLE_S24LE<<endl;
//	clog<<"PA_SAMPLE_S24BE: "<<PA_SAMPLE_S24BE<<endl;
//	clog<<"PA_SAMPLE_S24_32LE: "<<PA_SAMPLE_S24_32LE<<endl;
//	clog<<"PA_SAMPLE_S24_32BE: "<<PA_SAMPLE_S24_32BE<<endl;
	
	int opus_err;
	OpusEncoder *encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &opus_err);
	if (opus_err < 0)
		fatalError("failed to create an encoder: ", opus_strerror(opus_err));
	
	opus_err = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(BITRATE));
	if (opus_err < 0)
		fatalError("failed to set bitrate: ", opus_strerror(opus_err));
	
	const char *v2 = "WiFiAudio";
	static const pa_sample_spec ss = {
			.format = PA_SAMPLE_S16LE,
			.rate = SAMPLE_RATE,
			.channels = 2
	};
	int pa_err;
	pa_simple *s = pa_simple_new(NULL, v2, PA_STREAM_RECORD, NULL, v2, &ss, NULL, NULL, &pa_err);
	if (!s)
		fatalError("pa_simple_new() failed: ", pa_strerror(pa_err));
	
	//Работает и без следующей строки:
	set_device_monitor();
	//по-сути она должна как бы переключать recorder на последний используемый
	
	while(running) {
		opus_int16 buf[raw_size];
		unsigned char data[MAX_PACKET_SIZE];
		
		if (pa_simple_read(s, buf, raw_size, &pa_err) < 0)
			fatalError("pa_simple_read() failed: ", pa_strerror(pa_err));
//		if (buf[0] || buf[(raw_size / 2)] || buf[(raw_sizee - 1)]) {
			pa_usec_t lat = pa_simple_get_latency(s, &pa_err);
			if (lat == (pa_usec_t) -1)
				fatalError("pa_simple_get_latency() failed: ", pa_strerror(pa_err));
//			clog<<"Latency: "<<(float)lat<<" us."<<endl;
			
			if (lat > latency) {
				clog<<"too big latency: "<<lat<<endl;
				pa_simple_flush(s, &pa_err); //this should not return any pa_err (i guess)
			}
//			else {
				size_t nbBytes = (size_t) opus_encode(encoder, buf, (int) raw_size/4, data, MAX_PACKET_SIZE);
				if (nbBytes < 0) {
					fatalError("encode failed: ", opus_strerror((int)(nbBytes)));
				} else if (nbBytes > 0) {
//					clog<<"sending "<<nbBytes<<" bytes."<<endl;
					if (sendto(fd, &data, nbBytes, 0, (struct sockaddr *) &addr, sizeof(addr)) < 0)
						fatalError("sendto");
				}
//			}
//		}
	}
}


int main() {
	on_delay_value_changed(1);
	on_readbuffer_value_changed(1);
//	thread listenerThread(listenerTask);
	thread senderThread(senderTask);
	senderThread.join();
}


