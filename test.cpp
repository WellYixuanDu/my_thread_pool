#include <iostream>
#include <chrono>
#include <thread>
#include "threadpool.h"

using uLong = unsigned long long;

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

int main()
{
    {
        ThreadPool pool;
        pool.set_mode(PoolMode::MODE_CACHED);
        pool.start(2);

        Result res1 = pool.submit_task(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submit_task(std::make_shared<MyTask>(100000001, 200000000));
        Result res3 = pool.submit_task(std::make_shared<MyTask>(200000001, 300000000));
        pool.submit_task(std::make_shared<MyTask>(200000001, 300000000));
        pool.submit_task(std::make_shared<MyTask>(200000001, 300000000));
        pool.submit_task(std::make_shared<MyTask>(200000001, 300000000));

        //std::cout << "hello w orld" << std::endl;
        uLong sum1 = res1.get().cast_<uLong>();
        uLong sum2 = res2.get().cast_<uLong>();
        uLong sum3 = res3.get().cast_<uLong>();


        std::cout << (sum1 + sum2 + sum3) << std::endl;
    }
    // pool.submit_task(std::make_shared<MyTask>());
    // pool.submit_task(std::make_shared<MyTask>());
    // pool.submit_task(std::make_shared<MyTask>());
    // pool.submit_task(std::make_shared<MyTask>());
    // pool.submit_task(std::make_shared<MyTask>());
    // pool.submit_task(std::make_shared<MyTask>());
    // pool.submit_task(std::make_shared<MyTask>());
    // pool.submit_task(std::make_shared<MyTask>());
    // pool.submit_task(std::make_shared<MyTask>());
    // pool.submit_task(std::make_shared<MyTask>());
    // pool.submit_task(std::make_shared<MyTask>());
    getchar();
    //std::this_thread::sleep_for(std::chrono::seconds(5));
}