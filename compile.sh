#!/bin/bash

set -e

PA_INC_DIR="/opt/homebrew/Cellar/portaudio/19.7.0/include"
PA_LIB_DIR="/opt/homebrew/Cellar/portaudio/19.7.0/lib"

g++ -o a.out main.cpp -I$PA_INC_DIR -L$PA_LIB_DIR -lportaudio -std=c++20
./a.out
