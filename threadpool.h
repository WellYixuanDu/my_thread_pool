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

// Any类型，可以接收任意数据的类型
class Any
{
public:
    Any() = default;
    ~Any() = default;
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    Any(Any&&) = default;
    Any& operator=(Any&&) = default;

    // 接受任意其他数据
    template<typename T>
    Any(T data) : base_(std::make_unique<Derive<T>>(data))
    {}

    //把Any对象里面存储的data数据提取出来
    template<typename T>
    T cast_()
    {
        Derive<T> *pd = dynamic_cast<Derive<T>*>(base_.get());
        if (pd == nullptr)
        {
            throw "type is unmatch!";
        }
        return pd->m_data;
    }
private:
    // 基类
    class Base
    {
    public:
        virtual ~Base() = default;
    };
    // 派生类
    template<typename T>
    class Derive : public Base
    {
    public:
        Derive(T data) : m_data(data)
        {}
        T m_data;
    };

private:
    std::unique_ptr<Base> base_;
};

// 实现一个信号量类
class Semaphore
{
public:
    Semaphore(int limit = 0) 
        : m_res_limit(limit)
        , m_is_exit(false)
    {}
    ~Semaphore()
    {
        m_is_exit = true;
    }

    //获取一个信号量资源
    void wait()
    {
        if (m_is_exit)
        {
            return;
        }
        std::unique_lock<std::mutex> lock(m_mtx);
        m_cond.wait(lock, [&]()->bool { return m_res_limit > 0; });
        m_res_limit--;
    }

    // 增加一个信号量资源
    void post()
    {
        if (m_is_exit)
        {
            return;
        }
        std::unique_lock<std::mutex> lock(m_mtx);
        m_res_limit++;
        //linux下condition_variable析构函数
        m_cond.notify_all();
    }
private:
    int m_res_limit;
    std::mutex m_mtx;
    std::condition_variable m_cond;
    std::atomic_bool m_is_exit;
};

class Task;
// 实现接收提交到线程池的task任务执行完成后的返回值类型Result
class Result
{
public:
    Result(std::shared_ptr<Task> task, bool is_valid = true);
    ~Result() = default;

    // setVal 方法，获取任务执行完的返回值
    void set_val(Any any);
    // get方法，获取task的返回值
    Any get();
private:
    Any m_any; // 存储任务的返回值
    Semaphore m_sem; // 线程通信信号量
    std::shared_ptr<Task> m_task; // 指向对应获取返回值的任务对象
    std::atomic_bool m_is_valid; // 返回值是否有效
};

// 任务抽象基类
class Task
{
public:
    Task();
    ~Task() = default;
    void exec();
    void set_result(Result* res); 
    // 用户可以自定义任意任务类型，从Task继承，重写run方法，实现自定义任务处理
    virtual Any run() = 0;
private:
    Result* m_result;
};


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
    
    Thread(ThreadFunc func);
    ~Thread();
    // 启动线程
    void start();

    // 获取线程id
    int get_id() const;
private:
    ThreadFunc m_func;
    static int m_generate_id;
    int m_thread_id; //保存线程id
};

/*
example:
ThreadPool pool;
pool.start(4);

class MyTask : public Task
{
    public:
        void run() { // 线程代码}
}
pool.submit_task(std::make_shared<MyTask>());
*/

// 线程池类型
class ThreadPool
{
public:
    ThreadPool();
    ~ThreadPool();

    // 设置工作模式
    void set_mode(PoolMode mode);
    // 给线程池提交任务
    Result submit_task(std::shared_ptr<Task> sp);
    //开启线程池
    void start(size_t init_thread_size = std::thread::hardware_concurrency());
    // 设置task任务队列上线阈值
    void set_task_que_max_thresh_hold(size_t threshhold);

    // 设置线程池cached模式下线程阈值
    void set_thread_size_thresh_hold(size_t threshhold);
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool operator=(const ThreadPool&) = delete;

private:
    // 定义线程函数
    void thread_func(int threadid);

    // 检查pool运行状态

    bool check_running_state() const;
private:

    std::unordered_map<int, std::unique_ptr<Thread>> m_threads;

    //std::vector<std::unique_ptr<Thread>> m_threads; // 线程列表
    size_t m_init_thread_size; // 初始线程数量
    std::atomic_int m_cur_thread_size; //当前线程池里面线程的总数量
    size_t m_thread_size_thresh_hold;// 线程数量上限阈值    
    std::atomic_int m_idle_thread_size; // 空闲线程的数量

    std::queue<std::shared_ptr<Task>> m_task_que; // 任务队列
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