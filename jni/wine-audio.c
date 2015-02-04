/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * This is a sound server that direct pcm stream from named pipe to Android OpenSL audio player. 
 * It means to work along-side with "xserver-debian" by pelya 
 *    <https://github.com/pelya/commandergenius/tree/sdl_android/project/jni/application/xserver-debian>
 * It based on souce code of OpenAL backend for Android using the native audio APIs based on OpenSL ES 1.0.1
 *    <https://github.com/AerialX/openal-soft-android>. 
 * It is based on source code for the native-audio sample app bundled with NDK.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#ifdef __ANDROID__
#include <android/log.h>
// for native audio
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>


// DEBUG
//#define printf(...)
#define printf(...) __android_log_print(ANDROID_LOG_INFO, "XSDL", __VA_ARGS__)
#endif

/* Helper macros */
#define SLObjectItf_Realize(a,b)        ((*(a))->Realize((a),(b)))
#define SLObjectItf_GetInterface(a,b,c) ((*(a))->GetInterface((a),(b),(c)))
#define SLObjectItf_Destroy(a)          ((*(a))->Destroy((a)))

#define SLEngineItf_CreateOutputMix(a,b,c,d,e)       ((*(a))->CreateOutputMix((a),(b),(c),(d),(e)))
#define SLEngineItf_CreateAudioPlayer(a,b,c,d,e,f,g) ((*(a))->CreateAudioPlayer((a),(b),(c),(d),(e),(f),(g)))

#define SLPlayItf_SetPlayState(a,b) ((*(a))->SetPlayState((a),(b)))

static pthread_mutex_t pipe_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_pipe_mask=0,g_cache_mask=0;

typedef struct {
    /* engine interfaces */
    SLObjectItf engineObject;
    SLEngineItf engine;

    /* output mix interfaces */
    SLObjectItf outputMix;

    /* buffer queue player interfaces */
    SLObjectItf bufferQueueObject;

    void *buffer;
    unsigned int bufferSize;

    unsigned int frameSize;
    int id,fd;
    unsigned int nSamplesPerSec;
    unsigned int nChannels;
    unsigned int nBitsPerSample;
} osl_data;

static osl_data player[4];

static SLuint32 GetChannelMask(SLuint32 chans)
{
    switch(chans)
    {
        case 1: return SL_SPEAKER_FRONT_CENTER;
        case 2: return SL_SPEAKER_FRONT_LEFT|SL_SPEAKER_FRONT_RIGHT;
        case 4: return SL_SPEAKER_FRONT_LEFT|SL_SPEAKER_FRONT_RIGHT|
                                SL_SPEAKER_BACK_LEFT|SL_SPEAKER_BACK_RIGHT;
        case 6: return SL_SPEAKER_FRONT_LEFT|SL_SPEAKER_FRONT_RIGHT|
                               SL_SPEAKER_FRONT_CENTER|SL_SPEAKER_LOW_FREQUENCY|
                               SL_SPEAKER_BACK_LEFT|SL_SPEAKER_BACK_RIGHT;
        case 7: return SL_SPEAKER_FRONT_LEFT|SL_SPEAKER_FRONT_RIGHT|
                               SL_SPEAKER_FRONT_CENTER|SL_SPEAKER_LOW_FREQUENCY|
                               SL_SPEAKER_BACK_CENTER|
                               SL_SPEAKER_SIDE_LEFT|SL_SPEAKER_SIDE_RIGHT;
        case 8: return SL_SPEAKER_FRONT_LEFT|SL_SPEAKER_FRONT_RIGHT|
                               SL_SPEAKER_FRONT_CENTER|SL_SPEAKER_LOW_FREQUENCY|
                               SL_SPEAKER_BACK_LEFT|SL_SPEAKER_BACK_RIGHT|
                               SL_SPEAKER_SIDE_LEFT|SL_SPEAKER_SIDE_RIGHT;
    }
    return 0;
}

static const char *res_str(SLresult result)
{
    switch(result)
    {
        case SL_RESULT_SUCCESS: return "Success";
        case SL_RESULT_PRECONDITIONS_VIOLATED: return "Preconditions violated";
        case SL_RESULT_PARAMETER_INVALID: return "Parameter invalid";
        case SL_RESULT_MEMORY_FAILURE: return "Memory failure";
        case SL_RESULT_RESOURCE_ERROR: return "Resource error";
        case SL_RESULT_RESOURCE_LOST: return "Resource lost";
        case SL_RESULT_IO_ERROR: return "I/O error";
        case SL_RESULT_BUFFER_INSUFFICIENT: return "Buffer insufficient";
        case SL_RESULT_CONTENT_CORRUPTED: return "Content corrupted";
        case SL_RESULT_CONTENT_UNSUPPORTED: return "Content unsupported";
        case SL_RESULT_CONTENT_NOT_FOUND: return "Content not found";
        case SL_RESULT_PERMISSION_DENIED: return "Permission denied";
        case SL_RESULT_FEATURE_UNSUPPORTED: return "Feature unsupported";
        case SL_RESULT_INTERNAL_ERROR: return "Internal error";
        case SL_RESULT_UNKNOWN_ERROR: return "Unknown error";
        case SL_RESULT_OPERATION_ABORTED: return "Operation aborted";
        case SL_RESULT_CONTROL_LOST: return "Control lost";
#ifdef HAVE_OPENSL_1_1
        case SL_RESULT_READONLY: return "ReadOnly";
        case SL_RESULT_ENGINEOPTION_UNSUPPORTED: return "Engine option unsupported";
        case SL_RESULT_SOURCE_SINK_INCOMPATIBLE: return "Source/Sink incompatible";
#endif
    }
    return "Unknown error code";
}

#define PRINTERR(x, s) do {                                                      \
    if((x) != SL_RESULT_SUCCESS)                                                 \
        printf("%s: %s\n", (s), res_str((x)));                                      \
} while(0)

/* this callback handler is called every time a buffer finishes playing */
static void opensl_callback(SLAndroidSimpleBufferQueueItf bq, void *context)
{
    SLuint8 *stream;
    int len;
    //ALCdevice *Device = context;
    osl_data *data = context;//Device->ExtraData;
    int id = data->id;
    int mask = (0x01<<id);
    SLresult result;

    //aluMixData(Device, data->buffer, data->bufferSize/data->frameSize);

	//if (xsdlConnectionClosed)
	if ((g_pipe_mask & mask)==0)
		return;
	//printf("xsdl ---- %d\n",len);
	stream = data->buffer;
	len = data->bufferSize;
	while (len > 0)
	{
		int count = read(data->fd, stream, len);
		if (count <= 0)
		{
			//xsdlConnectionClosed = 1;
            pthread_mutex_lock(&pipe_mutex);
              g_pipe_mask &= ~(mask);
            pthread_mutex_unlock(&pipe_mutex);

			return;
		}
		stream += count;
		len -= count;
	}

    result = (*bq)->Enqueue(bq, data->buffer, data->bufferSize);
    PRINTERR(result, "bq->Enqueue");
}


static int opensl_open_playback(osl_data* data)
{

    SLresult result;
#if 0
    if(!deviceName)
        deviceName = opensl_device;
    else if(strcmp(deviceName, opensl_device) != 0)
        return ALC_INVALID_VALUE;

    data = calloc(1, sizeof(*data));
    if(!data)
        return 0;
#endif
    // create engine
    result = slCreateEngine(&data->engineObject, 0, NULL, 0, NULL, NULL);
    PRINTERR(result, "slCreateEngine");
    if(SL_RESULT_SUCCESS == result)
    {
        result = SLObjectItf_Realize(data->engineObject, SL_BOOLEAN_FALSE);
        PRINTERR(result, "engine->Realize");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        result = SLObjectItf_GetInterface(data->engineObject, SL_IID_ENGINE, &data->engine);
        PRINTERR(result, "engine->GetInterface");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        result = SLEngineItf_CreateOutputMix(data->engine, &data->outputMix, 0, NULL, NULL);
        PRINTERR(result, "engine->CreateOutputMix");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        result = SLObjectItf_Realize(data->outputMix, SL_BOOLEAN_FALSE);
        PRINTERR(result, "outputMix->Realize");
    }

    if(SL_RESULT_SUCCESS != result)
    {
        if(data->outputMix != NULL)
            SLObjectItf_Destroy(data->outputMix);
        data->outputMix = NULL;

        if(data->engineObject != NULL)
            SLObjectItf_Destroy(data->engineObject);
        data->engineObject = NULL;
        data->engine = NULL;

        free(data);
        return 0;
    }

    //Device->szDeviceName = strdup(deviceName);
    //Device->ExtraData = data;

    return 1;
}


static void opensl_close_playback(osl_data *data)
{

    if(data->bufferQueueObject != NULL)
        SLObjectItf_Destroy(data->bufferQueueObject);
    data->bufferQueueObject = NULL;

    SLObjectItf_Destroy(data->outputMix);
    data->outputMix = NULL;

    SLObjectItf_Destroy(data->engineObject);
    data->engineObject = NULL;
    data->engine = NULL;

    //free(data);
    //Device->ExtraData = NULL;
}

static int opensl_reset_playback(osl_data* data)
{
    //osl_data *data = Device->ExtraData;
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq;
    SLDataLocator_OutputMix loc_outmix;
    SLDataFormat_PCM format_pcm;
    SLDataSource audioSrc;
    SLDataSink audioSnk;
    SLInterfaceID id;
    SLboolean req;
    SLresult result;

#if 0
    Device->UpdateSize = (unsigned int64)Device->UpdateSize * 44100 / Device->Frequency;
    Device->UpdateSize = Device->UpdateSize * Device->NumUpdates / 2;
    Device->NumUpdates = 2;

    Device->Frequency = 44100;
    Device->FmtChans = DevFmtStereo;
    Device->FmtType = DevFmtShort;

    SetDefaultWFXChannelOrder(Device);
#endif

    id  = SL_IID_ANDROIDSIMPLEBUFFERQUEUE;
    req = SL_BOOLEAN_TRUE;

    loc_bufq.locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
    loc_bufq.numBuffers = 2;//Device->NumUpdates;

    format_pcm.formatType = SL_DATAFORMAT_PCM;
    format_pcm.numChannels = data->nChannels;//ChannelsFromDevFmt(Device->FmtChans);
    format_pcm.samplesPerSec = data->nSamplesPerSec * 1000;//SL_SAMPLINGRATE_44_1;// Device->Frequency * 1000;
    format_pcm.bitsPerSample = data->nBitsPerSample;//BytesFromDevFmt(Device->FmtType) * 8;
    format_pcm.containerSize = format_pcm.bitsPerSample;
    format_pcm.channelMask = GetChannelMask(data->nChannels);//SL_SPEAKER_FRONT_LEFT|SL_SPEAKER_FRONT_RIGHT;
#ifdef HAVE_OPENSL_1_1
    format_pcm.endianness = SL_BYTEORDER_NATIVE;
#else
	union { unsigned short num; char buf[sizeof(unsigned short)]; } endianness;
	endianness.num = 1;
	format_pcm.endianness = endianness.buf[0] ? SL_BYTEORDER_LITTLEENDIAN : SL_BYTEORDER_BIGENDIAN;
#endif

    audioSrc.pLocator = &loc_bufq;
    audioSrc.pFormat = &format_pcm;

    loc_outmix.locatorType = SL_DATALOCATOR_OUTPUTMIX;
    loc_outmix.outputMix = data->outputMix;
    audioSnk.pLocator = &loc_outmix;
    audioSnk.pFormat = NULL;


    if(data->bufferQueueObject != NULL)
        SLObjectItf_Destroy(data->bufferQueueObject);
    data->bufferQueueObject = NULL;

    result = SLEngineItf_CreateAudioPlayer(data->engine, &data->bufferQueueObject, &audioSrc, &audioSnk, 1, &id, &req);
    PRINTERR(result, "engine->CreateAudioPlayer");
    if(SL_RESULT_SUCCESS == result)
    {
        result = SLObjectItf_Realize(data->bufferQueueObject, SL_BOOLEAN_FALSE);
        PRINTERR(result, "bufferQueue->Realize");
    }

    if(SL_RESULT_SUCCESS != result)
    {
        if(data->bufferQueueObject != NULL)
            SLObjectItf_Destroy(data->bufferQueueObject);
        data->bufferQueueObject = NULL;

        return 0;
    }

    return 1;
}

static int opensl_start_playback(osl_data* data)
{
    SLAndroidSimpleBufferQueueItf bufferQueue;
    SLPlayItf player;
    SLresult result;
    unsigned int i;

    result = SLObjectItf_GetInterface(data->bufferQueueObject, SL_IID_BUFFERQUEUE, &bufferQueue);
    PRINTERR(result, "bufferQueue->GetInterface");
    if(SL_RESULT_SUCCESS == result)
    {
        result = (*bufferQueue)->RegisterCallback(bufferQueue, opensl_callback, data);
        PRINTERR(result, "bufferQueue->RegisterCallback");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        data->frameSize = 4;//FrameSizeFromDevFmt(Device->FmtChans, Device->FmtType);
        data->bufferSize = data->frameSize* 1024;//Device->UpdateSize * data->frameSize;
        data->buffer = calloc(1, data->bufferSize);
        if(!data->buffer)
        {
            result = SL_RESULT_MEMORY_FAILURE;
            PRINTERR(result, "calloc");
        }
    }
    /* enqueue the first buffer to kick off the callbacks */
    for(i = 0;i < 2;i++)
    {
        if(SL_RESULT_SUCCESS == result)
        {
            result = (*bufferQueue)->Enqueue(bufferQueue, data->buffer, data->bufferSize);
            PRINTERR(result, "bufferQueue->Enqueue");
        }
    }
    if(SL_RESULT_SUCCESS == result)
    {
        result = SLObjectItf_GetInterface(data->bufferQueueObject, SL_IID_PLAY, &player);
        PRINTERR(result, "bufferQueue->GetInterface");
    }
    if(SL_RESULT_SUCCESS == result)
    {
        result = SLPlayItf_SetPlayState(player, SL_PLAYSTATE_PLAYING);
        PRINTERR(result, "player->SetPlayState");
    }

    if(SL_RESULT_SUCCESS != result)
    {
        if(data->bufferQueueObject != NULL)
            SLObjectItf_Destroy(data->bufferQueueObject);
        data->bufferQueueObject = NULL;

        free(data->buffer);
        data->buffer = NULL;
        data->bufferSize = 0;

        return 0;
    }

    return 1;
}


static void opensl_stop_playback(osl_data *data)
{
    //osl_data *data = Device->ExtraData;

    free(data->buffer);
    data->buffer = NULL;
    data->bufferSize = 0;
}


static char pipe_fn[4][255];

typedef struct
{
    SLuint32 nSamplesPerSec;
    SLuint32 nChannels;
    SLuint32 nBitsPerSample;
} pcm_pipe_hdr;

void HandleWineAudio()
{
    int i,fd;
    int pipe_mask;
    int change;
    pcm_pipe_hdr pcm_hdr;

    pthread_mutex_lock(&pipe_mutex);

    change = g_pipe_mask ^ g_cache_mask;
    g_cache_mask=g_pipe_mask;

    pthread_mutex_unlock(&pipe_mutex);

       if (change) // some audio pipe is done;
       {
           int mask;

               for ( i = 0; i < 4; i++)  // find which pipe is done
               {
                    mask = (0x01 << i);
                    if (change & mask)
                    {
    			        opensl_stop_playback(&player[i]);
	            		opensl_close_playback(&player[i]);
			            close(player[i].fd);
			            printf(" Audio pipe %d closed\n",i);
                    }
               }
       }

       pipe_mask = 0;
       for ( i = 0; i < 4; i++)
       {
            int mask;
            mask = (0x01 << i);


            if ((g_cache_mask & mask) ==0)
            {   // check for new pipe
                //printf(" checking pipe %s\n",pipe_fn[i]);
                if  ((fd = open(pipe_fn[i], O_RDONLY)) > -1)
                {
                    read(fd,&pcm_hdr,sizeof(pcm_hdr));
                    player[i].id = i;
                    player[i].fd = fd;
                    player[i].nSamplesPerSec = pcm_hdr.nSamplesPerSec;
                    player[i].nChannels = pcm_hdr.nChannels;
                    player[i].nBitsPerSample = pcm_hdr.nBitsPerSample;

                    pthread_mutex_lock(&pipe_mutex);
                    g_pipe_mask |= mask;
                    pthread_mutex_unlock(&pipe_mutex);
                    g_cache_mask |= mask;

                    if (!opensl_open_playback(&player[i]))
                        goto error;

                    if (!opensl_reset_playback(&player[i])) {
	            		opensl_close_playback(&player[i]);
                        goto error;
                    }

                    if (!opensl_start_playback(&player[i])) {
	            		opensl_close_playback(&player[i]);
                        goto error;
                    }
                    printf(" Audio pipe %d connected, freq %u\n",i,pcm_hdr.nSamplesPerSec);
                    continue;

                 error:
                    close(fd);
                    pthread_mutex_lock(&pipe_mutex);
                    g_pipe_mask &= ~mask;
                    pthread_mutex_unlock(&pipe_mutex);
                    g_cache_mask &= ~mask;

                }
            }
       }
}

int main( int argc, char *argv[])
{
	char infile[PATH_MAX];
	int i;
	char *p;
	p = getenv("SECURE_STORAGE_DIR");
	if (!p) {
		printf("Server aborted. env SECURE_STORAGE_DIR not found. Please set it to full path of Debian installation 'files' directory.\n");
		return 1;
	}
		
	strcpy(infile, p);
	strcat(infile, "/img/tmp/audio-out");
	for (i=0 ; i < 4; i++)
	{
	    sprintf(pipe_fn[i],"%s%d",infile,i);
	}

	while (1)
	{
	    HandleWineAudio();
	    sleep(1);
	}
	return 0;
}


