#include <iostream>
#include <math.h>
#include <portaudio.h>
#include "circular_buffer.hpp"
#include <optional>

#define SAMPLE_RATE   (44100)
#define RINGBUFFER_SIZE	(2048)




/* This routine will be called by the PortAudio engine when audio is needed.
 * It may called at interrupt level on some machines so don't do anything
 * that could mess up the system like calling malloc() or free().
*/ 
static int patestCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
    circular_buffer<float,RINGBUFFER_SIZE> *sharedBuffer = static_cast<circular_buffer<float, RINGBUFFER_SIZE>*>(userData); 
    float *out = static_cast<float*>(outputBuffer);
    (void) inputBuffer; /* Prevent unused variable warning. */
    
    for(size_t i=0; i<framesPerBuffer; i++ )
    {
		float v = sharedBuffer->get().value_or(0.0f);
        *out++ = v;
        *out++ = v;
    }
    return 0;
} //literally copied and pasted



int main(){
	PaStream *stream;
	PaError err;

	circular_buffer<float, RINGBUFFER_SIZE> sharedBuffer;

	err = Pa_Initialize();
	if( err != paNoError ) goto error;

	    /* Open an audio I/O stream. */
    err = Pa_OpenDefaultStream( &stream,
                                0,          /* no input channels */
                                2,          /* stereo output */
                                paFloat32,  /* 32 bit floating point output */
                                SAMPLE_RATE,
                                paFramesPerBufferUnspecified,
												/* frames per buffer default:256, i.e. the number
                                                   of sample frames that PortAudio will
                                                   request from the callback. Many apps
                                                   may want to use
                                                   paFramesPerBufferUnspecified, which
                                                   tells PortAudio to pick the best,
                                                   possibly changing, buffer size.*/
                                patestCallback, /* this is your callback function */
                                &sharedBuffer ); /*This is a pointer that will be passed to
                                                   your callback*/
    if( err != paNoError ) goto error;

	err = Pa_StartStream( stream );
	if( err != paNoError ) goto error;


	{
		float thing = 0.0f;
		for(int i=0;i<SAMPLE_RATE*6;i++){
			sharedBuffer.wait_put(thing);
			thing += (float)i / (SAMPLE_RATE*6)*0.3;
			if(thing >= 1.0f) thing-=2.0f;

			if((i%1000)==0)
				std::cout << i << "  |  " << sharedBuffer.size() << std::endl;
		}
	}

    err = Pa_StopStream( stream );
    if( err != paNoError ) goto error;
    err = Pa_CloseStream( stream );
    if( err != paNoError ) goto error;
    Pa_Terminate();
    printf("Test finished.\n");
    return err;

error:
	Pa_Terminate();
	std::cerr << "An error occured while using the portaudio stream\n" << std::endl;
	std::cerr << "Error number: " << err << std::endl;
    std::cerr << "Error message: " << Pa_GetErrorText(err) << std::endl;
    return err;
}
