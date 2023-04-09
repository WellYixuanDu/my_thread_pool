#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <stddef.h>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>
#include <future>
#include <iostream>

const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60; //单位：秒


// 线程池支持的模式
enum class PoolMode
{
    MODE_FIXED, // 固定数量的线程
    MODE_CACHED, // 线程数量可动态增长
};


class Thread
{
public:
    using ThreadFunc = std::function<void(int)>;
    
    Thread(ThreadFunc func)
    :m_func(func)
    // 冲突？
    , m_thread_id(m_generate_id++)
    {

    }
    ~Thread() = default;
    // 启动线程
    void start()
    {
        // 创建一个线程来执行一个线程函数
        std::thread t(m_func, m_thread_id);
        t.detach(); // 设置分离线程
    }

    // 获取线程id
    int get_id() const
    {
        return m_thread_id;
    }
private:
    ThreadFunc m_func;
    static int m_generate_id;
    int m_thread_id; //保存线程id
};

int Thread::m_generate_id = 0;
// 线程池类型
class ThreadPool
{
public:
    ThreadPool()
        : m_init_thread_size(0)
        , m_task_size(0)
        , m_idle_thread_size(0)
        , m_cur_thread_size(0)
        , m_task_que_max_thresh_hold(TASK_MAX_THRESHHOLD)
        , m_thread_size_thresh_hold(THREAD_MAX_THRESHHOLD)
        , m_pool_mode(PoolMode::MODE_FIXED) 
        , m_is_pool_running(false)
    {}

    ~ThreadPool()
    {
        m_is_pool_running = false;
        std::unique_lock<std::mutex> lock(m_task_que_mtx);
        m_not_empty.notify_all();
        m_exit_cond.wait(lock, [&]()->bool{return m_threads.size() == 0;});
    }

    // 设置工作模式
    void set_mode(PoolMode mode)
    {
        if (check_running_state())
        {
            return;
        }
        m_pool_mode = mode;
    }
    // 给线程池提交任务
    //Result submit_task(std::shared_ptr<Task> sp);
    // 使用可变参数模版，让submittask接收任意任务函数和任意数量的参数
    // 函数值类型通过auto + decltype进行类型推导
    template<typename Func, typename... Args>
    auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
    {
        // 打包任务，放入任务队列中
        using RType = decltype(func(args...));
        auto task = std::make_shared<std::packaged_task<RType()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        std::future<RType> result = task->get_future();
        // 获取锁
        std::unique_lock<std::mutex> lock(m_task_que_mtx);
        // 线程的通信，等待任务队列有空余
        // 用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回
        if (!m_not_full.wait_for(lock, 
            std::chrono::seconds(1),
            [&]()->bool{return m_task_que.size() < m_task_que_max_thresh_hold;}))
        {
            std::cerr << "task queue is full, submit task fail." << std::endl;
            //return task->get_result(); // 不可以这样封装，由于task任务在掉用完成后便会进行析构，那么get_result方法中的result也就没用了，生命周期问题
            auto task = std::make_shared<std::packaged_task<RType()>>(
                []()->RType { return RType(); });
            (*task)();
            return task->get_future();
        }

        // 如果有空余，把任务让乳任务队列中,通过增加中间层来进行返回值类型的去除
        m_task_que.emplace([task](){
            // 去执行下面的任务
            (*task)();
        });
        m_task_size++;
        m_not_empty.notify_all();
        
        if (m_pool_mode == PoolMode::MODE_CACHED
            && m_task_size > m_idle_thread_size
            && m_cur_thread_size < m_thread_size_thresh_hold)
        {
            std::cout << "create new thread..." << std::endl;
            // 创建新线程
            auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::thread_func, this, std::placeholders::_1));
            int threadId = ptr->get_id();
            m_threads.emplace(threadId, std::move(ptr));
            m_threads[threadId]->start();
            m_cur_thread_size++;
            m_idle_thread_size++;
        }
        //cached 任务处理比较紧急，场景：小而快的任务 需要根据任务数量和空闲线程的数量，判断是否需要
        return result;        
    }
    //开启线程池
    void start(size_t init_thread_size = std::thread::hardware_concurrency())
    {
        m_is_pool_running = true;
        m_init_thread_size = init_thread_size;

        // 创建线程对象
        for (int i = 0; i < m_init_thread_size; ++i)
        {
            auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::thread_func, this, std::placeholders::_1));
            int threadId = ptr->get_id();
            m_threads.emplace(threadId, std::move(ptr));
        }

        // 启动所有线程
        for (int i = 0; i < m_init_thread_size; ++i)
        {
            m_threads[i]->start(); // 需要去执行一个线程函数bug
            m_idle_thread_size++;
            m_cur_thread_size++;
        }
    }
    // 设置task任务队列上线阈值
    void set_task_que_max_thresh_hold(size_t threshhold)
    {
        if (check_running_state())
        {
            return;
        }
        m_task_que_max_thresh_hold = threshhold;
    }

    // 设置线程池cached模式下线程阈值
    void set_thread_size_thresh_hold(size_t threshhold)
    {
        if (check_running_state())
        {
            return;
        }
        if (m_pool_mode == PoolMode::MODE_CACHED)
        {
            m_thread_size_thresh_hold = threshhold;
        }        
    }
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool operator=(const ThreadPool&) = delete;

private:
    // 定义线程函数
    void thread_func(int threadid)
    {
        auto last_time = std::chrono::high_resolution_clock().now();
        while (m_is_pool_running)
        {
            Task task;
            {
                std::unique_lock<std::mutex> lock(m_task_que_mtx);

                std::cout << "tid:" << std::this_thread::get_id()
                    << "尝试获取任务..." << std::endl;

                // cache模式下，超过数量的线程需要进行回收
                

                // 每一秒中返回一次
                // 锁 + 双重判断
                while (m_is_pool_running && m_task_que.size() == 0)
                {
                    if (m_pool_mode == PoolMode::MODE_CACHED)
                    {
                        // 条件变量，超时返回
                        if (std::cv_status::timeout == m_not_empty.wait_for(lock, std::chrono::seconds(1)))
                        {
                            auto now = std::chrono::high_resolution_clock().now();
                            auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - last_time);
                            if (dur.count() >= THREAD_MAX_IDLE_TIME
                                && m_cur_thread_size > m_init_thread_size)
                            {
                                // 开始回收当前线程
                            
                                m_cur_thread_size--;
                                m_idle_thread_size--;

                                std::cout << "threadid: " << std::this_thread::get_id() << "exit!" << std::endl;
                                return;
                            }
                        }
                    }
                    else
                    {
                        m_not_empty.wait(lock);
                    }

                }
                if (!m_is_pool_running)
                {
                    break;
                }
                m_idle_thread_size--;
                std::cout << "tid:" << std::this_thread::get_id()
                    << "获取任务成功..." << std::endl;
                task = m_task_que.front();
                m_task_que.pop();
                m_task_size--;

                if (m_task_que.size() > 0)
                {
                    m_not_empty.notify_all();
                }
                m_not_full.notify_all(); 
            }
            if (task != nullptr)
            {
                task();
            }
            m_idle_thread_size++;
            // 更新时间
            last_time = std::chrono::high_resolution_clock().now();
        
            
        }
        m_threads.erase(threadid);
        std::cout << "threadid: " << std::this_thread::get_id() << "exit!" << std::endl;    
        m_exit_cond.notify_all();        
    }

    // 检查pool运行状态

    bool check_running_state() const
    {
        return m_is_pool_running;
    }
private:

    std::unordered_map<int, std::unique_ptr<Thread>> m_threads;

    //std::vector<std::unique_ptr<Thread>> m_threads; // 线程列表
    size_t m_init_thread_size; // 初始线程数量
    std::atomic_int m_cur_thread_size; //当前线程池里面线程的总数量
    size_t m_thread_size_thresh_hold;// 线程数量上限阈值    
    std::atomic_int m_idle_thread_size; // 空闲线程的数量
    using Task = std::function<void()>;
    std::queue<Task> m_task_que; // 任务队列
    std::atomic_uint m_task_size; // 任务的数量
    size_t m_task_que_max_thresh_hold; // 任务队列的上限阈值

    std::mutex m_task_que_mtx; // 保证任务队列的线程安全
    std::condition_variable m_not_full; // 任务队列不满
    std::condition_variable m_not_empty; // 任务队列不空
    std::condition_variable m_exit_cond; // 等待线程资源全部回收
    PoolMode m_pool_mode; //当前线程池的工作模式

    std::atomic_bool m_is_pool_running;// 表示当前线程池的启动状态
    
};

#endif