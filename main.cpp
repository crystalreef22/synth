#include <iostream>
#include <math.h>
#include <portaudio.h>
#include "circular_buffer.hpp"
#include <optional>
#include <vector>
// #include "fir.hpp"

#define SAMPLE_RATE   (44100)
#define RINGBUFFER_SIZE    (1024)




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

float genSaw(float x, float amplitude, float period) {
    return (std::fmod(2*(x*(period/SAMPLE_RATE)), 2.0f)-1.0f) * amplitude * 0.5; // this WILL be aliasing. Not much we can do
}

float genBuzz(float x, float noiseAmplitude, float sawAmplitude, float period) {
    return noiseAmplitude * (drand48() - 0.5) + genSaw(x, sawAmplitude, period);
}

class synth {
public:
    synth(const std::vector<float>& coefficients, float gain)
        : coefficients(coefficients),
        delayLine(coefficients.size(), 0),
        gain{gain}
    {}

    void setCoefficients(std::vector<float> coeff, float g){
        coefficients = coeff;
        gain = g;
    }


    float getOutputSample(size_t buzzI){
        
        // @Bisqwit@youtube.com
        // 5 years ago (edited)
        // This is LPC (Linear Predictive Coding): yₙ = eₙ − ∑(ₖ₌₁..ₚ) (bₖ yₙ₋ₖ)  where
        // ‣ y[] = output signal, e[] = excitation signal (buzz, also called predictor error signal), b[] = the coefficients for the given frame
        // ‣ p = number of coefficients per frame, k = coefficient index, n = output index

        float output = genBuzz(buzzI, 0.1, 0.4, 150);
        size_t index = count + 1 ;// + 1;
        if (index == 0) index = coefficients.size();
        if (++count >= coefficients.size()) count = 0;
        for(size_t i=0; i<coefficients.size();i++){
            output -= coefficients[i] * delayLine[--index];
            std::cout << index << std::endl;
            if (index == 0) index = coefficients.size();
        }
        delayLine[count] = output ;
        std::cout << "                 " <<  count << std::endl;
        
        
        
        return(std::max(-1.0f,std::min(output * sqrt(gain),1.0f)));
    }

private:
    std::vector<float> coefficients;
    std::vector<float> delayLine;
    size_t count = 0;
    float gain;
};


int main(){
    // Read LPC








    // Init sound

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
        synth mySynth({-0.8579094422019475,-0.9277631143195522,0.804326386102418,0.26789408269619314,-0.39655431995016505,0.40583070034300345,-0.04803689569700615,-0.7224031368285665,0.3706332655071031,0.5274388251363206,-0.5919288151596237,-0.2548778925565867,0.5521411691507471,0.1196527490841037,-0.26917557222963295,0.07046663444708721},0.00034088200960689826);

        for(size_t i=0;i<SAMPLE_RATE*2;i++){
            sharedBuffer.wait_put(mySynth.getOutputSample(i));
        }
        mySynth.setCoefficients({0}, 1);
        for(size_t i=0;i<SAMPLE_RATE*2;i++){
            sharedBuffer.wait_put(mySynth.getOutputSample(i));
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
