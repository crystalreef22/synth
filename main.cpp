#include <iostream>
#include <fstream>
#include <math.h>
#include <portaudio.h>
#include "circular_buffer.hpp"
#include <optional>
#include <vector>
#include <sstream>
// uncomment to disable assert()
// #define NDEBUG
#include <cassert>
// #include "fir.hpp"

// #include <GLFW/glfw3.h>

//#include "imgui.h"
//#include "imgui_impl_glfw.h"
//#include "imgui_impl_opengl3.h"

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
    synth(size_t coefficientSize)
        : coefficients(coefficientSize, 0),
        delayLine(coefficientSize, 0),
        coefficientSize(coefficientSize)
    {}


    void setCoefficients(const std::vector<float>& coeff, float g){
        if (coeff.size() == coefficientSize){
            coefficients = coeff;
            gain = g;
        }
    }


    float getOutputSample(size_t buzzI, float breath = 0.15, float pitch = 150, float buzzGain = 0.9){
        
        // @Bisqwit@youtube.com
        // 5 years ago (edited)
        // This is LPC (Linear Predictive Coding): yₙ = eₙ − ∑(ₖ₌₁..ₚ) (bₖ yₙ₋ₖ)  where
        // ‣ y[] = output signal, e[] = excitation signal (buzz, also called predictor error signal), b[] = the coefficients for the given frame
        // ‣ p = number of coefficients per frame, k = coefficient index, n = output index

        size_t cSize = coefficients.size();

        float output = genBuzz(buzzI, breath * buzzGain, (1-breath) * buzzGain, pitch);
        for(size_t i=0; i<cSize;i++){
            output -= coefficients[i] * delayLine[(cSize - i + count) % cSize];
        }
        if (++count >= cSize) count = 0;
        delayLine[count] = output;
        
        

        return(std::max(-1.0f,std::min(output * sqrt(gain),1.0f)));
    }

private:
    std::vector<float> coefficients;
    std::vector<float> delayLine;
    size_t coefficientSize;
    size_t count = 0;
    float gain = 0;
};


int main(){


    {
    // Read LPC


    std::ifstream lpcFile("batchaduz-48-burg.LPC");
    if (!lpcFile.is_open()){
        std::cerr << "Error opening lpc file" << std::endl;
        return 1;
    }
    std::string line;

    std::getline(lpcFile, line);
    if (line != "File type = \"ooTextFile\"") {
        std::cerr << "Does not seem to be a LPC file" << std::endl;
        return 1;
    }

    std::getline(lpcFile, line);
    if (line != "Object class = \"LPC 1\"") {
        std::cerr << "Does not seem to be a LPC file" << std::endl;
        return 1;
    }

    float lpcFrameLength{0.02f}; // if not specified
    
    struct frame_t {
        std::vector<float> coefficients;
        float gain = 0;
    };

    std::vector<frame_t> lpcFrames;


    while (true) {
        // std::replace(line.begin(), line.end(), '=', ' ');
        std::getline(lpcFile, line);
        std::istringstream iss(line);
        std::string varName;
        iss >> varName;

        if (varName == "frames"){
            break;
        } else if (varName == "dx"){
            iss.ignore(128,'='); // 128 can be any arbitrary number

            iss >> lpcFrameLength;
        }
    }

    // assert(line == "frames []: ");
    std::getline(lpcFile, line);
    // assert(line == "    frames [1]:");
    

    bool keepReading = true;
    while (keepReading) {
        frame_t frame;
        while (true) {
            if (!std::getline(lpcFile, line)) {
                keepReading = false;
                break;
            }
            std::istringstream iss(line);
            std::string varName;
            iss >> varName;
            

            if (varName == "a"){
                std::string token;
                iss >> token;
                if (token != "[]:") {
                    iss.ignore(128,'=');
                    float sample;
                    iss >> sample;
                    frame.coefficients.push_back(sample);
                }
                
            } else if (varName == "gain") {
                iss.ignore(128,'=');
                iss >> frame.gain;
            } else if (varName == "frames"){
                break;
            }
        }
        //std::cout << "fc" << std::endl;
        //std::cout << frame.coefficients[0] << std::endl;
        lpcFrames.push_back(frame);
        //std::cin.ignore();
    }

    







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
        // synth mySynth({-0.8579094422019475,-0.9277631143195522,0.804326386102418,0.26789408269619314,-0.39655431995016505,0.40583070034300345,-0.04803689569700615,-0.7224031368285665,0.3706332655071031,0.5274388251363206,-0.5919288151596237,-0.2548778925565867,0.5521411691507471,0.1196527490841037,-0.26917557222963295,0.07046663444708721},0.00034088200960689826);
        synth mySynth(lpcFrames[0].coefficients.size());

        size_t samplesPerFrame = lpcFrameLength*SAMPLE_RATE;
        size_t sampleCount = lpcFrames.size()*samplesPerFrame;
        for(size_t i=0;i<sampleCount;i++){
            frame_t frame = lpcFrames[i/samplesPerFrame];
            // std::cout << frame.gain << std::endl;
            mySynth.setCoefficients(frame.coefficients, frame.gain);
            //std::cout << frame.coefficients[0] << std::endl;
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
}
