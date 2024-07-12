#include "wav_writer.hpp"
#include <vector>
#include <iostream>

int main(){
    wav_writer ww;
    /*std::vector<float> data(22050);
    float x{0};
    for(size_t i=0;i<22050;i++){
        data[i] = x;
        x += 0.01;
        if (x > 1) x -= 2;
    }

    ww.writeToWavPCMMonoCD("saw.wav",data);
    */
    std::vector<float> data{ww.readWavPCMMonoCDToFloat("freesound_IR_cathedral_mono.wav")};
    ww.writeToWavPCMMonoCD("test.wav",data);
    std::vector<float> data2{ww.readWavPCMMonoCDToFloat("test.wav")};
    std::cout << data.size() << std::endl;
    std::cout << data2.size() << std::endl;
    float dif{0};
    for(size_t i = 0; i < data2.size(); i++){
        float difn{std::abs(data[i] - data2[i])};
        if (difn > dif) dif = difn;
    }
    std::cout << dif << std::endl;
}
