#include "threadpool.h"
/*
1.  如何让线程池提交任务更加方便
    submittask可变参模版编程
2. 我们自己造了一个Result以及相关的类型，代码挺多
    使用future代替result
*/

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