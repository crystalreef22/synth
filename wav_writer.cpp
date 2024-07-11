#include "wav_writer.hpp"
#include <vector>

int main(){
    wav_writer ww;
    std::vector<float> data(22050);
    float x{0};
    for(size_t i=0;i<22050;i++){
        data[i] = x;
        x += 0.01;
        if (x > 1) x -= 2;
    }
    ww.writeToWavPCMMonoCD("saw.wav",data);
}
