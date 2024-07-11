#ifndef CIRCULAR_BUFFER_HPP
#define CIRCULAR_BUFFER_HPP

// Thanks to Embedded Artistry for the circular buffer tutorial, in addition to StackOverflow and ChatGPT for help
// Find the tutorial here: https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/

#include <array>
#include <condition_variable>
#include <mutex>
#include <optional>

template <class T, size_t TElemCount>
class shared_circular_buffer {
public:
    explicit shared_circular_buffer() = default;

    /**
    // ^ place a slash here to enable put()
    
    // We want to wait until there is space in the buffer, rather than overwrite,
    // since this is an audio application. See wait_put
    void put(T item){
        std::lock_guard<std::mutex> lock(mutex_);

        buf_[head_] = item;

        if (full_) {
            // Overwrite oldest value implementation
            tail_ = (tail_ + 1) % TElemCount;
        }

        head_ = (head_ + 1) % TElemCount;

        full_ = head_ == tail_;
    }
    // */
    

    void wait_put(T item){ // Not in tutorial. Got idea from ChatGPT and Stack Overflow
        std::unique_lock<std::mutex> lock(mutex_);

        not_full.wait(lock, [this] {return !full_;}); // Wait until not full

        buf_[head_] = item;

        head_ = (head_ + 1) % TElemCount;

        full_ = head_ == tail_;
    }

    std::optional<T> get(){
        std::unique_lock<std::mutex> lock(mutex_);

        if(empty()){
            return std::nullopt;
        }
        
        // Read data and advance the tail
        auto val = buf_[tail_];
        full_ = false;
        tail_ = (tail_+1) % TElemCount;
        
        lock.unlock();
        not_full.notify_one();

        return val;
    }

    /*
    //Not implemented, not in tutorial, not needed.
    std::optional<T> peek(size_t look_ahead_counter){
        // Thread safe if 1 producer/consumer
        // Adapted from github of C version
        // Do not add a lock since it is not recursive and size() is called

        if(empty() || look_ahead_counter > size()){
            return std::nullopt;
        }
        
        // Read data and advance the tail
        size_t pos = buf
        auto val = buf_[tail_];
        full_ = false;
        tail_ = (tail_+1) % TElemCount;

        return val;
    }
    */


    void reset() {
        std::unique_lock<std::mutex> lock(mutex_);
        head_ = tail_;
        full_ = false;
        lock.unlock();
        not_full.notify_one();
    }

    bool empty() const {
        return (!full_ && (head_ == tail_) );
    }
    bool full() const {
        return full_;
    }

    size_t capacity() const {
        return TElemCount;
    }
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!full_) {
            if (head_ >= tail_) {
                return head_ - tail_;
            }
            // else go around the circle once
            return TElemCount + head_ - tail_;
        } // else it is full, so return size

        return TElemCount;
    }



private:
    mutable std::mutex mutex_;
    std::condition_variable not_full;
    std::array<T, TElemCount> buf_;
    size_t head_ = 0;
    size_t tail_ = 0;
    bool full_ = false;
};

#endif
