#include <iostream>
#include <fstream>
#include <math.h>
#include <portaudio.h>
#include "circular_buffer.hpp"
#include <optional>
#include <string>
#include <vector>
#include <sstream>
// uncomment to disable assert()
// #define NDEBUG
#include <cassert>
// #include "fir.hpp"

#include <thread>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif

#include <GLFW/glfw3.h> // Will drag system OpenGL headers

static void glfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}


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

struct frame_t {
    std::vector<float> coefficients;
    float gain = 0;
};

class synth {
public:
    synth(size_t coefficientSize)
        : delayLine(coefficientSize, 0)
    {}


    float getOutputSample(const frame_t& frame, float breath = 0.1, float buzz = 0.8, float pitch = 150){
        
        // @Bisqwit@youtube.com
        // 5 years ago (edited)
        // This is LPC (Linear Predictive Coding): yₙ = eₙ − ∑(ₖ₌₁..ₚ) (bₖ yₙ₋ₖ)  where
        // ‣ y[] = output signal, e[] = excitation signal (buzz, also called predictor error signal), b[] = the coefficients for the given frame
        // ‣ p = number of coefficients per frame, k = coefficient index, n = output index

        size_t cSize = frame.coefficients.size();

        float output = genBuzz(buzzI, breath, buzz, pitch);
        for(size_t i=0; i<cSize;i++){
            output -= frame.coefficients[i] * delayLine[(cSize - i + count) % cSize];
        }
        if (++count >= cSize) count = 0;
        delayLine[count] = output;

        
        buzzI++;

        return(std::max(-1.0f,std::min(output * sqrt(frame.gain),1.0f)));
    }

    void reset(){
        count = 0;
        std::fill(delayLine.begin(), delayLine.end(), 0.0f);
    }

private:
    // std::vector<float> coefficients;
    std::vector<float> delayLine;
    // size_t coefficientSize;
    size_t count = 0;
    // float gain = 0;
    int buzzI = 0;
};


void synthThread(int* returnErr, frame_t* lpcFrame, float* breath, float* buzz, float* pitch, std::atomic_bool* stopStream) {
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
        synth mySynth((*lpcFrame).coefficients.size());

        //size_t samplesPerFrame = lpcFrameLength*SAMPLE_RATE;
        while (!((*stopStream).load())){
            sharedBuffer.wait_put(mySynth.getOutputSample(*lpcFrame, *breath, *buzz, *pitch));
        }
    }

    err = Pa_StopStream( stream );
    if( err != paNoError ) goto error;
    err = Pa_CloseStream( stream );
    if( err != paNoError ) goto error;
    Pa_Terminate();
    printf("Test finished.\n");
    *returnErr = err;
    return;

error:
    Pa_Terminate();
    std::cerr << "An error occured while using the portaudio stream\n" << std::endl;
    std::cerr << "Error number: " << err << std::endl;
    std::cerr << "Error message: " << Pa_GetErrorText(err) << std::endl;
    *returnErr =  err;
    return;
}

int main(){
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

    //------------------------------
    // IMGUI SETUP
    //------------------------------


    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit())
        return 1;


    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    const char* glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Dear ImGui GLFW+OpenGL3 example", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    ImVec4 clear_color = ImVec4(0.45f, 0.60, 0.55f, 1.00f);



    int st1err{-1};
    frame_t lpcFrame{lpcFrames[0]}; // This requires a mutex, but does not have one
                                    // Add mutex later!!!
    int lpcFrameI{0}; // All those require one mutex
    int maxFrameI = lpcFrames.size(); // size_t to int
    std::atomic_bool stopStream{false}; // No mutex for this one, but must be atomic
    float breath {0.1};
    float buzz {0.8};
    float pitch {150};
    // NOT THREAD SAFE!!!!!!!!!
    std::thread st1(synthThread, &st1err, &lpcFrame, &breath, &buzz, &pitch, &stopStream);

    //------------------------------
    // MAIN LOOP
    //------------------------------

    
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        {

            ImGui::Begin("LPC browser");                          // Create a window called "Hello, world!" and append into it.
            ImGui::Text("Click and drag to edit value.\n"
                "Hold SHIFT/ALT for faster/slower edit.\n"
                "Double-click or CTRL+click to input value.");
            ImGui::DragInt("Frame #", &lpcFrameI, 1, 0, maxFrameI, "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SameLine();ImGui::Text("max: %d", maxFrameI);
            ImGui::SliderFloat("Buzz", &buzz, 0, 1);
            ImGui::SliderFloat("Breath", &breath, 0, 1);
            ImGui::SliderFloat("Pitch", &pitch, 1, 1500);
            if (ImGui::Button("Normalize Buzz-Breath Ratio")) {
                float total = buzz + breath;
                buzz /= total;
                breath /= total;
            }
            lpcFrame = lpcFrames[lpcFrameI];
            if (ImGui::Button("check error to cout")) {                          // Buttons return true when clicked (most widgets return true when edited/activated)
                std::cout << st1err << std::endl;
            }
            ImGui::End();
        }

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }



    // Tell thread to stop the stream cleanly
    stopStream.store(true);
    st1.join();




    //------------------------------
    // IMGUI CLEANUP
    //------------------------------
    //

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
