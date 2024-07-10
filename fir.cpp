#include <iostream>
#include <vector>
#include "circular_buffer.hpp"
//adapted from https://ptolemy.berkeley.edu/eecs20/week12/implementation.html

// use vector, performance is not THAT important
class FIRFilter {
public:
	FIRFilter(const std::vector<float>& impulseResponse)
		: impulseResponse(impulseResponse) {}

	float getOutputSample(float inputSample) {
		delayLine[count] = inputSample;
		float result = 0.0f;
		int index = count;
		//stub

		return result;
	}
private:
	std::vector<float> impulseResponse;
	std::vector<float> delayLine;
	size_t count = 0;
};

int main() {
	std::cout << std::endl;
}
