# my_thread_pool
## threadpool.h
该类文件中声明了 Any类，Semaphore类，Result类，Task类，Thread类，以及主体ThreadPool类。
### Any类
Any类实现了任意数据类型的接收存储，核心思想是利用派生类与基类互转的性质，通过一个基类指针来接收通过模版定义而来的派生类，在进行数据的获取时，再通过类型转换至派生类，获取到存储的值。
### Semaphore类
Semaphore类主要是通过互斥锁以及条件变量来共同维护一个资源数量，提供post与wait方法，来进行资源的使用与增加。
### Result类
Result类实现接收提交到线程池的task任务执行完成后的返回值类型，用来通过get方法获取任务完成时的数据，若任务未完成则会进入阻塞状态。其中提供了set_val的接口，用于任务完成时，任务返回值的赋值，然后通过信号量进行get的唤醒赋值。
### Task类
Task类是任务基类，主要进行任务的执行并实现了Result与task的相关绑定工作。
### Thread类
Thread类实现了线程函数的执行。
### ThreadPool类
ThreadPool类提供了参数设置的一些接口，并提供了start与submit_task的方法,start函数用于创建线程，并进行线程的启动，submit_task则将task放入任务队列当中，其中涉及一些临界区的访问问题。线程函数主要是通过内置函数thread_func进行的，主要进行线程队列中任务的获取，其中需要注意锁的争用问题。
## 运行示例
```c++
class MyTask : public Task
{
public:
    MyTask(int begin, int end)
        : m_begin(begin)
        , m_end(end)
    {}
    // C++17 Any类型
    Any run()
    {
        std::cout << "tid:" << std::this_thread::get_id()
            << "begin!" << std::endl;
        uLong sum = 0;
        for (uLong i = m_begin; i <= m_end; ++i)
        {
            sum += i;
        }
        //std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cout << "tid:" << std::this_thread::get_id()
            << "end!" << std::endl;
        return sum;
    }
private:
    int m_begin;
    int m_end;
};

ThreadPool pool;
pool.set_mode(PoolMode::MODE_CACHED);
pool.start(2);

Result res1 = pool.submit_task(std::make_shared<MyTask>(1, 100000000));
uLong sum1 = res1.get().cast_<uLong>();
```
***
## threadpool_final
threadpool_final 中的线程池采用了C++14，17新标准中提供的future类简化了开发，future类即位上一版本threadpool中实现的Result类，Task类则被function进行替代，并且通过可变参模版编程实现了submit_task接口的通用性。核心思想保持不变。
### 代码示例
```c++
int sum1(int a, int b)
{
    return a + b;
}

int main()
{
    ThreadPool pool;
    pool.start(4);
    pool.submitTask(sum1, 1, 2);
}
```
