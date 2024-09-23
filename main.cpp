#include <iostream>
#include <fstream>
#include <map>
// #include <cmath>
#include <mutex>
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
#include "misc/cpp/imgui_stdlib.h"
#include "misc/cpp/imgui_stdlib.cpp"

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


const std::vector<std::string> phonemeNames = {
    "AA","AE","AH","AO","AW","AY","B","CH","D","DH","EH","ER","EY","F","G","HH","IH","IY","JH","K","L","M","N","NG","OW","OY","P","R","S","SH","T","TH","UH","UW","V","W","Y","Z","ZH"
};

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

class lpc_synth {
public:
    lpc_synth(size_t coefficientSize)
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

enum class phoneme_Playback {ONESHOT, RANDOMLOOP};

struct phoneme_t {
    bool voiced;
    phoneme_Playback playback;
    std::vector<frame_t> frames;
};

enum class synth_thread_Status{
    PAUSED,
    LPC_BROWSER,
    LPC_PLAYER
};

class synth_thread {
public:
    synth_thread(shared_circular_buffer<float, RINGBUFFER_SIZE>* sharedBuffer, size_t maxFrameLength)
        : sharedBuffer{sharedBuffer},
        maxFrameLength{maxFrameLength}
    {
        thread_ = std::thread(&synth_thread::threadFunction, this);
    };
    

    ~synth_thread() {
        stopStream.store(true);
        //Join thread
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void updateBrowser(const frame_t &lpcFrame, float breath, float buzz, float pitch) {
        std::lock_guard<std::mutex> lock(mutex_);
        this->lpcFrame = lpcFrame;
        this->breath = breath;
        this->buzz = buzz;
        this->pitch = pitch;
        newDataAvailable.store(true);
    }

    void updatePlayer(const std::string phonemeName, float pitch) {
        std::lock_guard<std::mutex> lock(mutex_);
        this->pitch = pitch;
        newDataAvailable.store(true);
    }
    
    void pause() {
        status.store(synth_thread_Status::PAUSED);
    };
    void startLPCBrowser() {
        status.store(synth_thread_Status::LPC_BROWSER);
    }
    void startLPCPlayer() {
        status.store(synth_thread_Status::LPC_PLAYER);
    }
    void toggleLPCBrowser() {
        toggleStatus(synth_thread_Status::LPC_BROWSER);
    }
    void toggleLPCPlayer() {
        toggleStatus(synth_thread_Status::LPC_PLAYER);
    }
    void toggleStatus(synth_thread_Status status) {
        if (this->status.load() == status) {
            this->status.store(synth_thread_Status::PAUSED);
        } else {
            this->status.store(status);
        }
    }

    synth_thread_Status getStatus() {
        return status.load();
    }

private:
    void threadFunction(){
        lpc_synth mySynth(maxFrameLength);

        frame_t localLpcFrame{lpcFrame};
        float localBreath{breath};
        float localBuzz{buzz};
        float localPitch{pitch};
        while (!(stopStream.load())) {
            switch (status.load()) {
                case synth_thread_Status::PAUSED:
                    sharedBuffer -> wait_put(0.0f);
                    break;
                case synth_thread_Status::LPC_BROWSER:
                    if (newDataAvailable.load()) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        localLpcFrame = lpcFrame;
                        localBreath = breath;
                        localBuzz = buzz;
                        localPitch = pitch;
                        newDataAvailable.store(false);
                    }
                    sharedBuffer -> wait_put(mySynth.getOutputSample(localLpcFrame, localBreath, localBuzz, localPitch));
                    break;
                case synth_thread_Status::LPC_PLAYER:
                    sharedBuffer -> wait_put(genSaw(tmpCounter, 0.5, 440));
                    tmpCounter++;
                    // not implemented
                    break;
            }
        }
    }
private:
    std::mutex mutex_;
    // std::atomic_bool paused{false};
    std::atomic<synth_thread_Status> status{synth_thread_Status::PAUSED};
    std::thread thread_;
    shared_circular_buffer<float, RINGBUFFER_SIZE>* sharedBuffer;
    std::atomic_bool stopStream{false};
    std::atomic_bool newDataAvailable{false};
    frame_t lpcFrame;
    float breath{0};float buzz{0};float pitch{0};
    size_t maxFrameLength;
    int tmpCounter{0};
};

bool writeVoicebank(const std::map<std::string, phoneme_t>& voicebank) {
    std::ofstream out("out.frv");

    out << "Forestria Synthesizer Voicebank v0.1" << "\n";

    for (const auto& phonemePair : voicebank) {
        out << "\"" << phonemePair.first << "\n"; // ARPABET
        auto& phone = phonemePair.second;

        out << (phone.voiced ? "vvoiced" : "vunvoiced") << "\n";

        switch (phone.playback) {
            case phoneme_Playback::ONESHOT:
                out << "poneshot" << "\n";
                break;
            case phoneme_Playback::RANDOMLOOP:
                out << "prandomloop" << "\n";
                break;
            default:
                std::cerr << "E: phone has unknown playback" << std::endl;
                out.close();
                return false;
        }

        out << "f" << "\n";
        for (const auto& frame : phone.frames) {
            out << "g" << frame.gain << "\nc";
            for (const auto& coefficients : frame.coefficients) {
                out << coefficients << " ";
            }
            out << "\n";
        }
        out << "end\n";
    }

    out << "end file\n";

    out.close();
    return true;
}
bool readFrvLine(std::ifstream& in, std::string& line, char expectedChar) {
    if (!std::getline(in, line)) return false;
    if (line.size() == 0) return false; // line will be "" then
    if (line[0] != expectedChar) return false;
    line = line.erase(0,1);
    return true;
}
std::optional<std::map<std::string, phoneme_t>> readVoicebank(const std::string& filename) {
    std::map<std::string, phoneme_t> result;

    std::ifstream in(filename);

    std::string line;

    if (!std::getline(in, line)) return std::nullopt;
    if (line != "Forestria Synthesizer Voicebank v0.1") return std::nullopt;

    // std::cout << "Yes, this is a frv file" << std::endl;

    while (true) {
        if (!readFrvLine(in, line, '"')) {
            if (line == "end file") {
                break;
            }
            return std::nullopt;
        }
        const std::string arpabetName{line};

        if (!readFrvLine(in, line, 'v')) return std::nullopt;
        const bool voiced = line == "voiced";
        if (!voiced && line != "unvoiced") {
            return std::nullopt;
        }

        if (!readFrvLine(in, line, 'p')) return std::nullopt;
        phoneme_Playback playback;
        if (line == "oneshot") {
            playback = phoneme_Playback::ONESHOT;
        } else if (line == "randomloop") {
            playback = phoneme_Playback::RANDOMLOOP;
        } else return std::nullopt;

        // std::cout << "read \", v, p" << std::endl;

        if (!readFrvLine(in, line, 'f')) return std::nullopt;

        std::vector<frame_t> frames;

        while (true) {
            frame_t frame;
            if (readFrvLine(in, line, 'g')) {
                try {
                    frame.gain = std::stof(line);
                } catch (std::invalid_argument) {
                    return std::nullopt;
                } catch (std::out_of_range) {
                    return std::nullopt;
                }
            } else if (line == "end") {
                break;
            } else {
                // if end of file reached for some reason?
                return std::nullopt;
            }
            // std::cout << "Got gain" << std::endl;
            if (! readFrvLine(in, line, 'c')) return std::nullopt;
            std::istringstream iss(line);
            std::string s;
            while (getline( iss, s, ' ' )) {
                // std::cout << s << std::endl;
                try {
                    frame.coefficients.push_back(std::stof(s));
                } catch (std::invalid_argument) {
                    return std::nullopt;
                } catch (std::out_of_range) {
                    return std::nullopt;
                }
            }
            //std::cout << "Got coefficients" << std::endl;
        }

        //std::cout << "Got frames" << std::endl;

        result.insert_or_assign(arpabetName, phoneme_t{voiced, playback, frames});
    }
    


    in.close();
    return result;
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
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Forestria Synthesizer", nullptr, nullptr);
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



    
    //------------------------------
    // START PORTAUDIO?
    //------------------------------
    PaStream *stream;
    PaError err;

    shared_circular_buffer<float, RINGBUFFER_SIZE> sharedBuffer;

    err = Pa_Initialize();
    if( err != paNoError ) goto portaudioError;

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
    if( err != paNoError ) goto portaudioError;

    err = Pa_StartStream( stream );
    if( err != paNoError ) goto portaudioError;

    { // END SETUP

        int maxFrameI = lpcFrames.size(); // size_t to int
        // NOT THREAD SAFE!!!!!!!!!
        synth_thread synthThread{&sharedBuffer, lpcFrames.size()};

        std::map<std::string, phoneme_t> voicebank;

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

                ImGui::Begin("File reader");
                if (ImGui::Button("open out.frv")) {
                    voicebank = readVoicebank("out.frv").value();
                }
                ImGui::End();
                ImGui::Begin("LPC browser");                          // Create a window called "Hello, world!" and append into it.
                ImGui::Text("Click and drag to edit value.\n"
                    "Hold SHIFT/ALT for faster/slower edit.\n"
                    "Double-click or CTRL+click to input value.");


                if (ImGui::Button(synthThread.getStatus() == synth_thread_Status::LPC_BROWSER ? "Stop" : "Start")) {
                    synthThread.toggleLPCBrowser();
                }

                static int lpcFrameI;
                ImGui::DragInt("Frame #", &lpcFrameI, 1, 0, maxFrameI, "%d", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SameLine();ImGui::Text("max: %d", maxFrameI);

                static float breath{0.1}; static float buzz{0.8}; static float pitch{150};

                ImGui::SliderFloat("Buzz", &buzz, 0, 1);
                ImGui::SliderFloat("Breath", &breath, 0, 1);
                ImGui::SliderFloat("Pitch", &pitch, 1, 1500);
                if (ImGui::Button("Normalize Buzz-Breath Ratio")) {
                    float total = buzz + breath;
                    buzz /= total;
                    breath /= total;
                }
                synthThread.updateBrowser(lpcFrames[lpcFrameI], breath, buzz, pitch);


                ImGui::Separator();

                static size_t segmentStart{0};
                static size_t segmentEnd{0};

                if (ImGui::Button("Set segment start")) {
                    segmentStart = lpcFrameI;
                }
                ImGui::SameLine(); ImGui::Text("%i", (int)segmentStart);
                
                ImGui::SameLine();
                if (ImGui::Button("Set segment end")) {
                    segmentEnd = lpcFrameI;
                }
                ImGui::SameLine(); ImGui::Text("%i", (int)segmentEnd);

                static int sampleTypeIdx = 0;
                ImGui::Combo("Sample type", &sampleTypeIdx, "oneshot\0randomloop\0\0");

                static bool voiced = true;
                ImGui::Checkbox("voiced", &voiced);

                // arpabet selector combobox
                static int phonemeArpabetIdx;
                const char* combo_preview_value = (phonemeNames[phonemeArpabetIdx]).c_str();

                if (ImGui::BeginCombo("Phoneme representation", combo_preview_value)) {
                    for (int n = 0; n < phonemeNames.size(); n++)
                    {
                        const bool is_selected = (phonemeArpabetIdx == n);
                        std::string phone = phonemeNames[n];
                        if (voicebank.contains(phone)) {phone = "* " + phone;}
                        if (ImGui::Selectable(phone.c_str(), is_selected))
                            phonemeArpabetIdx = n;

                        // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                const bool voicebankContainsComboItem = voicebank.contains(phonemeNames[phonemeArpabetIdx]);

                
                // Save phoneme button
                ImGui::BeginDisabled(segmentStart > segmentEnd); // Disable if segment is illegal

                if (ImGui::Button(voicebankContainsComboItem ? "Overwrite" : "Save")) {
                    phoneme_Playback playback;
                    if (sampleTypeIdx == 0) playback = phoneme_Playback::ONESHOT;
                    if (sampleTypeIdx == 1) playback = phoneme_Playback::RANDOMLOOP;
                    
                    std::vector<frame_t> slicedFrames;
                    slicedFrames.reserve(segmentEnd - segmentStart + 1);

                    for (size_t i = segmentStart; i <= segmentEnd; i++) {
                        slicedFrames.push_back(lpcFrames[i]);
                    }


                    voicebank.insert_or_assign(phonemeNames[phonemeArpabetIdx], phoneme_t{voiced, playback, slicedFrames});

                    ++phonemeArpabetIdx %= phonemeNames.size();
                }
                ImGui::EndDisabled();

                // Remove button
                ImGui::SameLine();
                ImGui::BeginDisabled(! voicebankContainsComboItem);
                if (ImGui::Button("Remove")) {
                    voicebank.erase(phonemeNames[phonemeArpabetIdx]);
                }
                ImGui::EndDisabled();
                
                if (ImGui::Button("Write to file"))
                {
                    writeVoicebank(voicebank);
                }

                ImGui::End();

                ImGui::Begin("Arpabet player");
                ImGui::Text("Play a thing");
                static std::string arpabetInputTest;
                ImGui::InputText("One arpabet tone", &arpabetInputTest, ImGuiInputTextFlags_CharsUppercase);
                if (ImGui::Button(synthThread.getStatus() == synth_thread_Status::LPC_PLAYER ? "Stop" : "Start")) {
                    synthThread.toggleLPCPlayer();
                }
                ImGui::Text("I have not implemented this yet so");
                ImGui::Text("it is just playing a sawtooth wave");
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

        // synth thread goes out of scope, deconstructor called
        
    } // BEGIN SHUTDOWN

    //------------------------------
    // STOP PORTAUDIO
    //------------------------------
    err = Pa_StopStream( stream );
    if( err != paNoError ) goto portaudioError;
    err = Pa_CloseStream( stream );
    if( err != paNoError ) goto portaudioError;
    Pa_Terminate();
    goto imguiCleanup;

portaudioError:
    Pa_Terminate();
    std::cerr << "An error occured while using the portaudio stream\n" << std::endl;
    std::cerr << "Error number: " << err << std::endl;
    std::cerr << "Error message: " << Pa_GetErrorText(err) << std::endl;
    goto imguiCleanup;





imguiCleanup:


    //------------------------------
    // IMGUI CLEANUP
    //------------------------------
    //

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return err;
}
