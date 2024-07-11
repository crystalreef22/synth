#include <iostream>
#include <math.h>
#include <portaudio.h>
#include "circular_buffer.hpp"
#include <optional>
#include "fir.hpp"

#define SAMPLE_RATE   (44100)
#define RINGBUFFER_SIZE	(1024)




/* This routine will be called by the PortAudio engine when audio is needed.
 * It may called at interrupt level on some machines so don't do anything
 * that could mess up the system like calling malloc() or free().
 *
 * Read from circular buffer when more data is needed
*/ 
static int paCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
    shared_circular_buffer<float,RINGBUFFER_SIZE> *sharedBuffer = static_cast<shared_circular_buffer<float, RINGBUFFER_SIZE>*>(userData); 
    float *out = static_cast<float*>(outputBuffer);
    (void) inputBuffer; /* Prevent unused variable warning. */
    
    for(size_t i=0; i<framesPerBuffer; i++ )
    {
		float v = sharedBuffer->get().value_or(0.0f);
        *out++ = v;
        *out++ = v;
    }
    return 0;
}


// Generate LPC for to read

float saw(float x, float period) {
	return std::fmod(2*(x*(period/SAMPLE_RATE)), 2.0f)-1.0f;
}


int main(){
	PaStream *stream;
	PaError err;

	shared_circular_buffer<float, RINGBUFFER_SIZE> sharedBuffer;

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
                                paCallback, /* this is your callback function */
                                &sharedBuffer ); /*This is a pointer that will be passed to
                                                   your callback*/
    if( err != paNoError ) goto error;

	err = Pa_StartStream( stream );
	if( err != paNoError ) goto error;


	{
		FIRFilter reverb = FIRFilter({0.04,0.06,0.08,0.12,0.2,0.2,0.12,0.08,0.06,0.04});
		for(int i=0;i<SAMPLE_RATE*2;i++){
			float s = saw(i,440)*0.3;
			sharedBuffer.wait_put(std::max(-1.0f,std::min(s,1.0f)));
		}
		for(int i=0;i<SAMPLE_RATE*2;i++){
			float s = reverb.getOutputSample(saw(i,440)*0.3);
			sharedBuffer.wait_put(std::max(-1.0f,std::min(s,1.0f)));
		}
	}

	//synth();

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
