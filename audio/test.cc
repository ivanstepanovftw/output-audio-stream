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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <pulse/simple.h>
#include <pulse/error.h>
#include <iostream>

#define BUFSIZE 1024

int main(int argc, char*argv[]) {

    /* The Sample format to use */
    static const pa_sample_spec ss = {
            .format = PA_SAMPLE_S16LE,
            .rate = 44100,
            .channels = 2
    };

    pa_simple *s = NULL;
    int ret = 1;
    int error;

    /* replace STDIN with the specified file if needed */
    if (argc > 1) {
        int fd;

        if ((fd = open(argv[1], O_RDONLY)) < 0) {
            fprintf(stderr, __FILE__": open() failed: %s\n", strerror(errno));
            goto finish;
        }

        if (dup2(fd, STDIN_FILENO) < 0) {
            fprintf(stderr, __FILE__": dup2() failed: %s\n", strerror(errno));
            goto finish;
        }

        close(fd);
    }

    /* Create a new playback stream */
    if (!(s = pa_simple_new(NULL, argv[0], PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error))) {
        fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
        goto finish;
    }

    for (;;) {
        uint8_t buf[BUFSIZE];
        ssize_t r;

#if 0
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
        std::cout<<+buf[0]<<std::endl;;
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

///*
// * $Id$
// *
// * This program uses the PortAudio Portable Audio Library.
// * For more information see: http://www.portaudio.com
// * Copyright (c) 1999-2000 Ross Bencina and Phil Burk
// *
// * Permission is hereby granted, free of charge, to any person obtaining
// * a copy of this software and associated documentation files
// * (the "Software"), to deal in the Software without restriction,
// * including without limitation the rights to use, copy, modify, merge,
// * publish, distribute, sublicense, and/or sell copies of the Software,
// * and to permit persons to whom the Software is furnished to do so,
// * subject to the following conditions:
// *
// * The above copyright notice and this permission notice shall be
// * included in all copies or substantial portions of the Software.
// *
// * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
// * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// */
//
///*
// * The text above constitutes the entire PortAudio license; however,
// * the PortAudio community also makes the following non-binding requests:
// *
// * Any person wishing to distribute modifications to the Software is
// * requested to send the modifications to the original developer so that
// * they can be incorporated into the canonical version. It is also
// * requested that these non-binding requests be included along with the
// * license above.
// */
//
//#include <stdio.h>
//#include <math.h>
//#include "portaudio.h"
//
//#ifdef WIN32
//#include <windows.h>
//
//#if PA_USE_ASIO
//#include "pa_asio.h"
//#endif
//#endif
//
///*******************************************************************/
//static void PrintSupportedStandardSampleRates(
//        const PaStreamParameters *inputParameters,
//        const PaStreamParameters *outputParameters )
//{
//    static double standardSampleRates[] = {
//        8000.0, 9600.0, 11025.0, 12000.0, 16000.0, 22050.0, 24000.0, 32000.0,
//        44100.0, 48000.0, 88200.0, 96000.0, 192000.0, -1 /* negative terminated  list */
//    };
//    int     i, printCount;
//    PaError err;
//
//    printCount = 0;
//    for( i=0; standardSampleRates[i] > 0; i++ )
//    {
//        err = Pa_IsFormatSupported( inputParameters, outputParameters, standardSampleRates[i] );
//        if( err == paFormatIsSupported )
//        {
//            if( printCount == 0 )
//            {
//                printf( "\t%8.2f", standardSampleRates[i] );
//                printCount = 1;
//            }
//            else if( printCount == 4 )
//            {
//                printf( ",\n\t%8.2f", standardSampleRates[i] );
//                printCount = 1;
//            }
//            else
//            {
//                printf( ", %8.2f", standardSampleRates[i] );
//                ++printCount;
//            }
//        }
//    }
//    if( !printCount )
//        printf( "None\n" );
//    else
//        printf( "\n" );
//}
//
///*******************************************************************/
//int main(void);
//int main(void)
//{
//    int     i, numDevices, defaultDisplayed;
//    const   PaDeviceInfo *deviceInfo;
//    PaStreamParameters inputParameters, outputParameters;
//    PaError err;
//
//
//    err = Pa_Initialize();
//    if( err != paNoError )
//    {
//        printf( "ERROR: Pa_Initialize returned 0x%x\n", err );
//        goto error;
//    }
//
//    printf( "PortAudio version: 0x%08X\n", Pa_GetVersion());
//    printf( "Version text: '%s'\n", Pa_GetVersionInfo()->versionText );
//
//    numDevices = Pa_GetDeviceCount();
//    if( numDevices < 0 )
//    {
//        printf( "ERROR: Pa_GetDeviceCount returned 0x%x\n", numDevices );
//        err = numDevices;
//        goto error;
//    }
//
//    printf( "Number of devices = %d\n", numDevices );
//    for( i=0; i<numDevices; i++ )
//    {
//        deviceInfo = Pa_GetDeviceInfo( i );
//        printf( "--------------------------------------- device #%d\n", i );
//
//    /* Mark global and API specific default devices */
//        defaultDisplayed = 0;
//        if( i == Pa_GetDefaultInputDevice() )
//        {
//            printf( "[ Default Input" );
//            defaultDisplayed = 1;
//        }
//        else if( i == Pa_GetHostApiInfo( deviceInfo->hostApi )->defaultInputDevice )
//        {
//            const PaHostApiInfo *hostInfo = Pa_GetHostApiInfo( deviceInfo->hostApi );
//            printf( "[ Default %s Input", hostInfo->name );
//            defaultDisplayed = 1;
//        }
//
//        if( i == Pa_GetDefaultOutputDevice() )
//        {
//            printf( (defaultDisplayed ? "," : "[") );
//            printf( " Default Output" );
//            defaultDisplayed = 1;
//        }
//        else if( i == Pa_GetHostApiInfo( deviceInfo->hostApi )->defaultOutputDevice )
//        {
//            const PaHostApiInfo *hostInfo = Pa_GetHostApiInfo( deviceInfo->hostApi );
//            printf( (defaultDisplayed ? "," : "[") );
//            printf( " Default %s Output", hostInfo->name );
//            defaultDisplayed = 1;
//        }
//
//        if( defaultDisplayed )
//            printf( " ]\n" );
//
//    /* print device info fields */
//#ifdef WIN32
//        {   /* Use wide char on windows, so we can show UTF-8 encoded device names */
//            wchar_t wideName[MAX_PATH];
//            MultiByteToWideChar(CP_UTF8, 0, deviceInfo->name, -1, wideName, MAX_PATH-1);
//            wprintf( L"Name                        = %s\n", wideName );
//        }
//#else
//        printf( "Name                        = %s\n", deviceInfo->name );
//#endif
//        printf( "Host API                    = %s\n",  Pa_GetHostApiInfo( deviceInfo->hostApi )->name );
//        printf( "Max inputs = %d", deviceInfo->maxInputChannels  );
//        printf( ", Max outputs = %d\n", deviceInfo->maxOutputChannels  );
//
//        printf( "Default low input latency   = %8.4f\n", deviceInfo->defaultLowInputLatency  );
//        printf( "Default low output latency  = %8.4f\n", deviceInfo->defaultLowOutputLatency  );
//        printf( "Default high input latency  = %8.4f\n", deviceInfo->defaultHighInputLatency  );
//        printf( "Default high output latency = %8.4f\n", deviceInfo->defaultHighOutputLatency  );
//
//#ifdef WIN32
//#if PA_USE_ASIO
///* ASIO specific latency information */
//        if( Pa_GetHostApiInfo( deviceInfo->hostApi )->type == paASIO ){
//            long minLatency, maxLatency, preferredLatency, granularity;
//
//            err = PaAsio_GetAvailableLatencyValues( i,
//                            &minLatency, &maxLatency, &preferredLatency, &granularity );
//
//            printf( "ASIO minimum buffer size    = %ld\n", minLatency  );
//            printf( "ASIO maximum buffer size    = %ld\n", maxLatency  );
//            printf( "ASIO preferred buffer size  = %ld\n", preferredLatency  );
//
//            if( granularity == -1 )
//                printf( "ASIO buffer granularity     = power of 2\n" );
//            else
//                printf( "ASIO buffer granularity     = %ld\n", granularity  );
//        }
//#endif /* PA_USE_ASIO */
//#endif /* WIN32 */
//
//        printf( "Default sample rate         = %8.2f\n", deviceInfo->defaultSampleRate );
//
//    /* poll for standard sample rates */
//        inputParameters.device = i;
//        inputParameters.channelCount = deviceInfo->maxInputChannels;
//        inputParameters.sampleFormat = paInt16;
//        inputParameters.suggestedLatency = 0; /* ignored by Pa_IsFormatSupported() */
//        inputParameters.hostApiSpecificStreamInfo = NULL;
//
//        outputParameters.device = i;
//        outputParameters.channelCount = deviceInfo->maxOutputChannels;
//        outputParameters.sampleFormat = paInt16;
//        outputParameters.suggestedLatency = 0; /* ignored by Pa_IsFormatSupported() */
//        outputParameters.hostApiSpecificStreamInfo = NULL;
//
//        if( inputParameters.channelCount > 0 )
//        {
//            printf("Supported standard sample rates\n for half-duplex 16 bit %d channel input = \n",
//                    inputParameters.channelCount );
//            PrintSupportedStandardSampleRates( &inputParameters, NULL );
//        }
//
//        if( outputParameters.channelCount > 0 )
//        {
//            printf("Supported standard sample rates\n for half-duplex 16 bit %d channel output = \n",
//                    outputParameters.channelCount );
//            PrintSupportedStandardSampleRates( NULL, &outputParameters );
//        }
//
//        if( inputParameters.channelCount > 0 && outputParameters.channelCount > 0 )
//        {
//            printf("Supported standard sample rates\n for full-duplex 16 bit %d channel input, %d channel output = \n",
//                    inputParameters.channelCount, outputParameters.channelCount );
//            PrintSupportedStandardSampleRates( &inputParameters, &outputParameters );
//        }
//    }
//
//    Pa_Terminate();
//
//    printf("----------------------------------------------\n");
//    return 0;
//
//error:
//    Pa_Terminate();
//    fprintf( stderr, "Error number: %d\n", err );
//    fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
//    return err;
//}