#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60; //单位：秒

ThreadPool::ThreadPool()
    : m_init_thread_size(0)
    , m_task_size(0)
    , m_idle_thread_size(0)
    , m_cur_thread_size(0)
    , m_task_que_max_thresh_hold(TASK_MAX_THRESHHOLD)
    , m_thread_size_thresh_hold(THREAD_MAX_THRESHHOLD)
    , m_pool_mode(PoolMode::MODE_FIXED) 
    , m_is_pool_running(false)
{}

ThreadPool::~ThreadPool()
{
    m_is_pool_running = false;
    std::unique_lock<std::mutex> lock(m_task_que_mtx);
    m_not_empty.notify_all();
    m_exit_cond.wait(lock, [&]()->bool{return m_threads.size() == 0;});
}

// 设置工作模式
void ThreadPool::set_mode(PoolMode mode)
{
    if (check_running_state())
    {
        return;
    }
    m_pool_mode = mode;
}
// 给线程池提交任务
Result ThreadPool::submit_task(std::shared_ptr<Task> sp)
{
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
        return Result(sp, false);
    }

     // 如果有空余，把任务让乳任务队列中
     m_task_que.emplace(sp);
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
     return Result(sp);
}
//开启线程池
void ThreadPool::start(size_t init_thread_size)
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
        m_threads[i]->start(); // 需要去执行一个线程函数
        m_idle_thread_size++;
        m_cur_thread_size++;
    }
}
// 设置task任务队列上线阈值
void ThreadPool::set_task_que_max_thresh_hold(size_t threshhold)
{
    if (check_running_state())
    {
        return;
    }
    m_task_que_max_thresh_hold = threshhold;
}

// 设置线程池cached任务队列上限阈值
void ThreadPool::set_thread_size_thresh_hold(size_t threshhold)
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

// 定义线程函数 线程池的所有线程从任务队列里面消费任务
void ThreadPool::thread_func(int threadid)
{
    auto last_time = std::chrono::high_resolution_clock().now();
    while (m_is_pool_running)
    {
        std::shared_ptr<Task> task;
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
            task->exec();
        }
        m_idle_thread_size++;
        // 更新时间
        last_time = std::chrono::high_resolution_clock().now();
       
        
    }
    m_threads.erase(threadid);
    std::cout << "threadid: " << std::this_thread::get_id() << "exit!" << std::endl;    
    m_exit_cond.notify_all();
}

 bool ThreadPool::check_running_state() const
 {
    return m_is_pool_running;
 }
// ---------------------------------------Thread 实现---------------------------------------

int Thread::m_generate_id = 0;

Thread::Thread(ThreadFunc func)
    :m_func(func)
    // 冲突？
    , m_thread_id(m_generate_id++)
{

}
Thread::~Thread()
{

}
// 启动线程
void Thread::start()
{
     // 创建一个线程来执行一个线程函数
    std::thread t(m_func, m_thread_id);
    t.detach(); // 设置分离线程
}

int Thread::get_id() const
{
    return m_thread_id;
}

// ------------------------------------------- Task 实现 ------------------------------------------
Task::Task()
    : m_result(nullptr)
{}

void Task::exec()
{
    if (m_result != nullptr)
    {
        m_result->set_val(run()); //发生多态掉用
    }
}

void Task::set_result(Result *res)
{
    m_result = res;
}

//----------------------------------- Result 实现 -------------------------------------------------
Result::Result(std::shared_ptr<Task> task, bool is_valid)
    : m_task(task)
    , m_is_valid(is_valid)
{
    m_task->set_result(this);
}


Any Result::get()
{
    if (!m_is_valid)
    {
        return "";
    }
    m_sem.wait();
    return std::move(m_any);
}


void Result::set_val(Any any)
{
    this->m_any = std::move(any);
    m_sem.post();
}