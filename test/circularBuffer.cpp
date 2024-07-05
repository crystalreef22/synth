
// Thanks to Embedded Artistry for the circular buffer tutorial
// Find the tutorial here: https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/

#include <memory>
#include <condition_variable>
#include <mutex>
#include <optional>

template <class T>
class circular_buffer {
public:
	explicit circular_buffer(size_t size) :
		buf_(std::unique_ptr<T[]>(new T[size])),
		max_size_(size)
	{
		//empty constructor
	}

	void put(T item){
		std::lock_guard<std::mutex> lock(mutex_);

		buf_[head_] = item;

		if (full_) {
			// Overwrite oldest value implementation
			tail_ = (tail_ + 1) % max_size_;
		}

		head_ = (head_ + 1) % max_size_;

		full_ = head_ == tail_;
	}

	void wait_put(T item){ // Not in tutorial. Got idea from ChatGPT and Stack Overflow
		std::unique_lock<std::mutex> lock(mutex_);

		not_full.wait(lock, [this] {return !full_;}); // Wait until not full

		buf_[head_] = item;

		head_ = (head_ + 1) % max_size_;

		full_ = head_ == tail_;
	}

	std::optional<T> get(){
		std::lock_guard<std::mutex> lock(mutex_);

		if(empty()){
			return std::nullopt;
		}
		
		// Read data and advance the tail
		auto val = buf_[tail_];
		full_ = false;
		tail_ = (tail_+1) % max_size_;

		return val;
	}

	void reset() {
		std::lock_guard<std::mutex> lock(mutex_);
		head_ = tail_;
		full_ = false;
	}

	bool empty() const {
		return (!full_ && (head_ == tail_) );
	}
	bool full() const {
		return full_;
	}

	size_t capacity() const {
		return max_size_;
	}
	size_t size() const {
		if (!full_) {
			if (head_ >= tail_) {
				return head_ - tail_;
			}
			// else go around the circle once
			return max_size_ + head_ - tail_;
		} // else it is full, so return size

		return max_size_;
	}



private:
	std::mutex mutex_;
	std::condition_variable not_full;
	std::unique_ptr<T[]> buf_;
	size_t head_ = 0;
	size_t tail_ = 0;
	const size_t max_size_;
	bool full_ = 0;
};
