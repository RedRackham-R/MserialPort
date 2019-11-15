#pragma once
#include "IBackgroundService.h"
#include <future>
#include <functional>
#include <thread>
#include <chrono>
#include <iostream>

class PFBackgroundService : public IBackgroundService
{
private:

    //thread-safe wrapper class for a promise/future pair
    template <typename T>
    struct PromiseAndFuture
    {
        std::unique_ptr<std::promise<T>> m_promise;
        std::unique_ptr<std::future<T>> m_future;
        std::mutex m_mutex;

        //constructor
        PromiseAndFuture()
        {
            reset();
        }

        //reset the managed promise and future objects to new ones
        void reset()
        {
            m_promise.reset(nullptr);
            m_future.reset(nullptr);
            m_promise = std::make_unique<std::promise<T>>();
            m_future = std::make_unique<std::future<T>>(m_promise->get_future());
        }

        //non-blocking function that returns whether or not the
        //managed future object is ready, i.e., has a valid value
        bool ready()
        {
            std::future_status status = m_future->wait_for(std::chrono::milliseconds(0));
            return (status == std::future_status::ready);
        }

        //blocking function that retrieves and returns value in
        //managed future object
        T get()
        {
            std::lock_guard<std::mutex> lockGuard(m_mutex);
            T ret = m_future->get();
            reset();
            return ret;
        }

        //set the value on the managed promise object to val,
        //thereby making the future "ready" and contain val
        bool set(T val)
        {
            std::lock_guard<std::mutex> lockGuard(m_mutex);
            //don't
            if (ready())
            {
                return false;
            }
            m_promise->set_value(val);
            return true;
        }
    };

    //managed background thread
    std::unique_ptr<std::thread> m_thread;

    //instance of promise/future pair that is used for messaging
    PromiseAndFuture<std::string> m_PF;


public:
    //stop message
    const static std::string STOP;
    //destroy message
    const static std::string DESTROY;
    //constructor (takes in fptr to a "reactor" function that "reacts"
    //to a string message)
    PFBackgroundService(const std::function<void(std::string)>& reactor);

    //interface method
    void processMessage(std::string msg);

    //destructor (stops and waits for managed thread to exit)
    ~PFBackgroundService();
};