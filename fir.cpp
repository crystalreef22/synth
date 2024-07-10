#include <iostream>
#include <vector>
//adapted from https://ptolemy.berkeley.edu/eecs20/week12/implementation.html

// use vector, performance is not THAT important
class FIRFilter {
public:
	FIRFilter(const std::vector<float>& impulseResponse)
		: impulseResponse(impulseResponse),
		delayLine(impulseResponse.size(), 0) // initialize delay line(circular buffer) to size of impulse response
	{/* empty constructor */}

	float filterSize(){
		return impulseResponse.size();
	}

	float getOutputSample(float inputSample) {
		delayLine[count] = inputSample;
		float result = 0.0f;
		size_t index = count;
		for (size_t i=0; i<filterSize(); i++) {
			result += impulseResponse[i] * delayLine[index--];
			if (index < 0) index = filterSize()-1; //wrap around circular buffer
		}
		if (++count >= filterSize()) count = 0;
		return result;

	}
private:
	const std::vector<float> impulseResponse;
	std::vector<float> delayLine;
	size_t count = 0;
};

int main() {
	std::cout << std::endl;
}
