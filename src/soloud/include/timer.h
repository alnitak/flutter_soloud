#include <iostream>
#include <chrono>
#include <thread>
#include <functional>

class Timer {
public:
    Timer() : running(false), expired(false) {}

    ~Timer() {
        if (thread.joinable())
            thread.join();
    }

    template<typename F, typename... Args>
    void start(int duration_ms, F&& callback, Args&&... args) {
        if (!running) {
            running = true;
            expired = false;
            this->duration_ms = duration_ms;
            this->callback = std::bind(std::forward<F>(callback), std::forward<Args>(args)...);
            if (thread.joinable()) {
                thread.join();
            }
            thread = std::thread(); // Reset the thread
            thread = std::thread(&Timer::timerFunction, this);
            thread.detach();
        }
    }

    void stop() {
        if (running) {
            running = false;
            expired = false;
            if (thread.joinable()) {
                thread.join();
            }
            thread = std::thread(); // Reset the thread
        }
    }

    void restart() {
        stop();
        start(duration_ms, callback);
    }

    bool isActive() const {
        return running && !expired;
    }

private:
    std::thread thread;
    std::function<void()> callback;
    int duration_ms;
    bool running;
    bool expired;

    void timerFunction() {
        std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
        callback();
        expired = true;
        running = false;
    }
};