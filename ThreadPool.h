#ifndef THREAD_P00L_H
#define THREAD_P00L_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

using std::vector;
using std::thread;
using std::function;
using std::queue;
using std::future;
using std::result_of;
using std::mutex;
using std::condition_variable;
using std::unique_lock;
using std::forward;
using std::packaged_task;
using std::make_shared;
using std::bind;

class ThreadPool {
public:
    ThreadPool(size_t);
    template<typename F, typename... Args>//Args是一个模版参数包
    auto enqueue(F&& f, Args&&... args)//args是一个函数参数包，通过在模式右边放'...'来扩展包
        -> future<typename result_of<F(Args...)>::type>;
    
    ~ThreadPool();

private:
    vector<thread> workers;
    queue<function<void()>> tasks;

    mutex queue_mutex;
    condition_variable condition;
    bool stop;
};

inline ThreadPool::ThreadPool(size_t threads) : stop(false)
{
    for (size_t i = 0; i < threads; i++)
    {
        workers.emplace_back(
            [this]
            {
                for (;;)
                {
                    function<void()> task;

                    {
                        unique_lock<mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, 
                                [this]{return this->stop || !this->tasks.empty();});
                                //只有当 pred 条件为 false 时调用 wait() 才会阻塞当前线程，并且在收到其他线程的通知后只有当 pred 为 true 时才会被解除阻塞
                        if (this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    task();
                }
            }
        );
    }
}

template<typename F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    ->future<typename result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;
    auto task = make_shared<packaged_task<return_type()>>(
        bind(forward<F>(f), forward<Args>(args)...)
    );

    future<return_type> res = task->get_future();

    {
        unique_lock<mutex> lock(queue_mutex);

        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task](){(*task)();});
    }
    condition.notify_one();
    return res;
}

ThreadPool::~ThreadPool()
{
    {
        unique_lock<mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for (thread &worker : workers)
        worker.join();
}

#endif
