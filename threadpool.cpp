#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <time.h>
#include <utility>
#include <vector>
using namespace std;

template<typename T>
struct safe_queue {
    queue<T> q;
    shared_mutex _m;
    auto empty() {  // AOP(aspect-oriented-programming), decorator, solidity modifier
        shared_lock<shared_mutex> lc(_m);
        return q.empty();
    }
    auto size() {
        shared_lock<shared_mutex> lc(_m);
        return q.size();
    }
    auto push(T& t) {
        unique_lock<shared_mutex> lc(_m);  // unique_lock vs shared_mutex
        q.push(t);
    }
    auto pop(T& t) {
        unique_lock<shared_mutex> lc(_m);
        if (q.empty())
            return false;
        t = move(q.front());
        q.pop();
        return true;
    }
};

class ThreadPool {
private:
    class worker {  // 工作线程一旦被创建,会不断地去wait线程池的任务队列是否为空,如果不空会取出队头的任务执行;执行完继续wait
    public:
        ThreadPool* pool;
        worker(ThreadPool* _pool) : pool{ _pool } {}
        void operator ()() {  // callable ( like lambda / function() )
            while (!pool->is_shut_down) {
                {
                    unique_lock<mutex> lock(pool->_m);  // <- 其他线程阻塞在这里
                    pool->cv.wait(lock, [this]() {  // <- 只有一个线程阻塞在这里
                        return (this->pool->is_shut_down) || !this->pool->q.empty();
                        //     (pool开启的时候,这一项可忽略)
                    });
                }
                function<void()> func;
                bool flag = pool->q.pop(func);  // 队列中有元素
                if (flag)
                    func();
            }
        }
    };
public:
    bool is_shut_down;
    safe_queue<std::function<void()>> q;
    vector<std::thread> threads;
    mutex _m;
    condition_variable cv;
    ThreadPool(int n): threads(n, thread(worker(this))), is_shut_down{false} {}  // 创建n个thread, 回调函数为 worker()
    
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&...args) -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));
        function<return_type()> func = [&f, args...]() {  // bind: f(args...) -> func()  使得函数变成一个无参函数
            return f(args...);  // 此处未使用完美转发,导致如果传入右值的args会产生拷贝构造的消耗
        };
        // 用一个智能指针来管理这个任务(函数)??
        auto task_ptr = std::make_shared< std::packaged_task<return_type()> >(func);
        std::function<void()> wrapper_func = [task_ptr]() {  // 使得函数变成一个无返回值函数,这也是一种多态
            (*task_ptr)();
        };
        q.push(wrapper_func);
        cv.notify_one();
        // https://zh.cppreference.com/w/cpp/thread/future
        // => "future<return_type> = packaged_task<return_type()>.get_future()"
        // 让packaged_task和future类型的返回值绑定,可以通过 返回值.get() 即 future.get() 来获得线程执行的返回值
        return task_ptr->get_future();
    }
    ~ThreadPool() {
        auto f = submit([]() {});  // 哨兵
        f.get();  // 说明:最后一个任务被取出,可知任务队列里的所有任务都被取出执行了(是否执行完成不知道,这个由下面的join来保证)
        is_shut_down = true;  // 关闭线程池(不再有worker进入while循环)
        cv.notify_all();
        for (auto& t : threads) {
            if (t.joinable())
                t.join();  // 保证每一个worker执行的任务都执行完毕,才结束析构函数,结束程序
        }
    }
};

mutex _m;
int main() {
    ThreadPool pool(8);
    int n = 20;
    for (int i = 1; i <= n; i++) {
        pool.submit(
            [](int id) {
                if (id % 2 == 1)
                    this_thread::sleep_for(0.2s);
                unique_lock<mutex> lc(_m);
                cout << "id : " << id << endl;
            }, i
        );
    }
}

/*
    (1) 封装成了一个无返回值的函数,才能放入任务列表,那如果希望接收返回值,如何操作?
    (2) 如何加强线程池的功能? 
        1. 设计一个挂起功能,使某个线程暂停. 
        2. 设计一个查看线程状态的功能 
        3. 设置任务队列的上限(相当于生产者消费者中的信号量)
    (3) 哨兵的作用,加入最后一个任务,保证"当任务队列里还有任务没有被执行的时候,如果直接shut_down=true,
    这些任务将无法被执行,如何实现任务队列为空的时候才可以执行shut_down=true呢?
    (4) 完美转发
    (5) 条件变量
    (6) 线程池可以看成一个生产者消费者模型,submit/q.push生产,worker()消费
    (7) 涉及C++17的特性: 
        1. shared_mutex,unique_lock (unique_lock可以主动释放锁,lock_guard不可以)
        2. condition_variable.wait()
        3. packaged_task,future
    (8) 用智能指针来管理这个任务,智能指针是否线程安全? https://www.zhihu.com/question/56836057/answer/2158966805
    (9) 用哪些方法减少加锁的代价:
        1. 读写锁
        2. 减小锁的粒度
   (10) 死锁四个条件: 互斥,不可剥夺,请求保持,循环等待
   (11) 预防死锁:
        1. 破坏不可剥夺: 申请新资源前,要释放旧资源 / 按进程优先级允许抢占资源
        2. 破坏请求保持: 上处理机之前,一次性申请并分配所有资源,运行时不再请求资源
        3. 破坏循环等待: 按顺序申请资源
   (12) 编程中预防死锁:
        1. RAII
        2. 按顺序申请
        3. 减少锁的粒度
        4. 嵌套锁
*/