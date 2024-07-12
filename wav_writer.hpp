#ifndef WAV_WRITER_HPP
#define WAV_WRITER_HPP

#include <fstream>
#include <vector>
#include <cstring>
#include <optional>

class wav_writer{
public:
    wav_writer(){}; //constructor

    bool writeToWavPCMMonoCD(const std::string& fileName, const std::vector<float>& data){
        wav_header_t wav_header;
        chunk_t wav_chunk;
        
        std::memcpy(wav_header.fileTypeID, "RIFF", sizeof(wav_header.fileTypeID));
        std::memcpy(wav_header.format, "WAVE", sizeof(wav_header.format));
        std::memcpy(wav_header.formatBlockID, "fmt ",sizeof(wav_header.formatBlockID));
        wav_header.audioFormat=1;
        wav_header.nbrChannels=1;
        wav_header.sampleRate=44100;
        wav_header.bitsPerSample=1*16;
        wav_header.bytesPerBlock=1*(16/8);
        wav_header.bytesPerSec=(44100 * 1 * 16) / 8;

        std::memcpy(wav_chunk.dataBlockID, "data", sizeof(wav_chunk.dataBlockID));
        wav_header.fmtBlockSize = 16;
        wav_chunk.dataSize = data.size()*16/8;
        wav_header.fileSize = 44-8 + data.size()*16/8;


        unsigned short shortMax = -1;

        std::ofstream fs (fileName,std::ios::binary); 
        if(!fs.is_open()) return false;

        fs.write(reinterpret_cast<const char*>(&wav_header), sizeof(wav_header));
        fs.write(reinterpret_cast<const char*>(&wav_chunk), sizeof(wav_chunk));
        for (size_t i = 0; i < data.size(); i++){
            float normalizedFloat = (data[i] + 1.0f)/2.0f;
            unsigned short p = normalizedFloat * shortMax;
            fs.write(reinterpret_cast<const char*>(&p), sizeof(p));
        }
        fs.close();

        return true;
    }

    std::vector<float> readWavPCMMonoCDToFloat(const std::string& fileName){
        // Very dirty, but should work good enough for my needs
        wav_header_t wav_header;
        chunk_t chunk_header;


        std::ifstream fs (fileName, std::ios::binary);
        if(!fs.is_open()) return std::vector<float>();
        
        fs.read(reinterpret_cast<char*>(&wav_header), sizeof(wav_header));
        fs.read(reinterpret_cast<char*>(&chunk_header), sizeof(chunk_header));
        // this implementation is very disgusting

        std::vector<float> data(chunk_header.dataSize/2);
        unsigned short shortMax = -1;
        for (size_t i = 0; i < chunk_header.dataSize/2; i++){
            unsigned short p;
            fs.read(reinterpret_cast<char*>(&p), sizeof(p));
            float sample=p/(shortMax/2.0f);
            sample = sample - 1;
            data[i] = sample;
        }

        fs.close();



        return data;
    }

private:
    #pragma pack(push, 1) // do not pack structs, so that binary data can be read and written. Probably bad practice
    struct wav_header_t{
        char fileTypeID[4];
        unsigned int fileSize; //overall file size minus 8 bytes
        char format[4];

        char formatBlockID[4];
        unsigned int fmtBlockSize; // chunk size minus 8 bytes: 16 [+ sizeof(wExtraFormatBytes) + wExtraFormatBytes]
        unsigned short audioFormat; //1: PCM integer, 3: IEEE 754 float
        unsigned short nbrChannels;
        unsigned int sampleRate;
        unsigned int bytesPerSec;
        unsigned short bytesPerBlock;
        unsigned short bitsPerSample;
        // extra format bytes
    };

    struct chunk_t {
        char dataBlockID[4]; //"data"
        unsigned int dataSize; // size of remaining data
    };
    #pragma pack(pop)

};
#endif
