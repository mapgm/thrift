/*
 * 本main.cpp文件储存匹配机制的服务器段和储存信息的客户端
 * 即 match_server 和 save_client 
 */

#include "match_server/Match.h" // 导入本地头文件，用""标注
#include "save_client/Save.h"
#include <thrift/protocol/TBinaryProtocol.h> // thrift库相关头文件
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/transport/TSocket.h>
#include <thrift/concurrency/ThreadManager.h>
#include <thrift/concurrency/ThreadFactory.h>
#include <thrift/TToString.h>
#include <thrift/server/TThreadedServer.h>

#include <iostream> // cout
#include <thread> // 多线程
#include <mutex> // 锁
#include <condition_variable> // 条件变量
#include <queue> // 存储task任务：add 或 remove
#include <vector> // 存储users：user1，user2...
#include <unistd.h> // sleep()

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using namespace ::match_service;
using namespace ::save_service;
using namespace std; // 合作开发项目时尽量别写，独立开发就无所谓了

// 定义一个任务结构体Task， type: add 或 remove
struct Task
{
    User user;
    string type;
};

// 定义一个消息队列，是生产者与消费者通信媒介
// 队列是互斥的，同时只能有一个线程访问队列
struct MessageQueue
{
    queue<Task> q;
    mutex m;
    condition_variable cv;
}message_queue;

// 定义类 -> 匹配池
class Pool 
{
    public:
        void save_result(int a, int b) // 储存匹配信息
        {
            printf("Match Result: %d %d\n", a, b); // 输出 a 和 b 匹配成功的结果

            std::shared_ptr<TTransport> socket(new TSocket("123.57.47.211", 9090)); // 储存匹配结果的服务器的IP 和 监视端口
            std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
            std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
            SaveClient client(protocol);

            try {
                transport->open();

                int res = client.save_data("acs_3247", "f8745d5e", a, b); // 储存结果的服务器名称和md5sum加密的密码

                if (!res) cout << "Successfully uploaded to the server!" << endl; // 若res为0，则证明结果成功储存在服务器中，即成功储存在 save_server 中
                else cout << "Failed to upload to the server!" << endl; // 反之，若res为1，则证明储存失败

                transport->close();
            } catch (TException& tx) {
                cout << "ERROR: " << tx.what() << endl;
            }
        }
    
        // 返回 a 和 b 的分值是否在范围内，即 a 和 b 是否能匹配上
        bool check_match(uint32_t i, uint32_t j) 
        {
            auto a = users[i], b = users[j]; // 取出 a 和 b 两个用户

            int dt = abs(a.score - b.score); // dt 为 a 和 b 的分差
            int a_max_dif = waiting_time[i] * 50; // a 能匹配的用户的分差范围
            int b_max_dif = waiting_time[j] * 50; // b 能匹配的用户的分差范围

            return dt <= a_max_dif && dt <= b_max_dif; // 返回 a 和 b 是否彼此都在能匹配的分值范围内，即 a 和 b 是否能匹配上
        }

        void match()
        {
            for (uint32_t i = 0;i < waiting_time.size();i ++)
                waiting_time[i] ++; // 等待时间 + 1

            while (users.size() > 1)
            {
                bool flag = true;
                for (uint32_t i = 0;i < users.size();i ++)
                {
                    for (uint32_t j = i + 1;j < users.size();j ++)
                    {
                        if (check_match(i, j)) // 不断尝试 users[i] 和 users[j] 是否能匹配上
                        {
                            auto a = users[i], b = users[j]; // 说明 a 和 b 已经匹配上了
                            users.erase(users.begin() + j); // 删除用户 a 和 b
                            users.erase(users.begin() + i);
                            waiting_time.erase(waiting_time.begin() + j); // 删除 a 和 b 的等待时间
                            waiting_time.erase(waiting_time.begin() + i);
                            save_result(a.id, b.id); // 储存 a 和 b 的匹配信息
                            flag = false;
                            break;
                        }
                    }

                    if (!flag) break;
                }

                if (flag) break;
            }
        }
        
        // 添加一个用户
        void add(User user) 
        {
            users.push_back(user); // 在users中加入新的用户
            waiting_time.push_back(0); // 添加他的等待时间
        }
        
        // 删除一个用户
        void remove(User user)
        {
            for (uint32_t i = 0;i < users.size();i ++) // 遍历users寻找被删除的用户
            {
                if (users[i].id == user.id) // 如果找到了
                {
                    users.erase(users.begin() + i); // 就从users中删除他的信息
                    waiting_time.erase(waiting_time.begin() + i); // 并删除他的的等待时间
                    break; // 跳出循环
                }
            }
        }
    private:
        vector<User> users; // 用户列表
        vector<int> waiting_time; // 等待时间

}pool; // 定义匹配池

class MatchHandler : virtual public MatchIf {
    public:
        MatchHandler() {
            // Your initialization goes here
        }

        // 实现了 add_user 接口
        int32_t add_user(const User& user, const std::string& info) {
            // Your implementation goes here
            printf("add_user\n"); // 成功在匹配池中加入新数据时输出 add_user

            unique_lock<mutex> lck(message_queue.m); // 在操作队列前，加锁（执行完自动解锁）
            message_queue.q.push({user, "add"}); // 添加任务到队列q
            message_queue.cv.notify_all(); // 当有操作时，唤醒线程

            return 0;
        }
    
        // 实现了 remove_user 接口
        int32_t remove_user(const User& user, const std::string& info) {
            // Your implementation goes here
            printf("remove_user\n"); // 成功在匹配池中删除新数据时输出 remove_user

            unique_lock<mutex> lck(message_queue.m); // 在操作队列前，加锁（执行完自动解锁）
            message_queue.q.push({user, "remove"}); // 添加任务到队列q
            message_queue.cv.notify_all(); // 当有操作时，唤醒线程

            return 0;
        }

};

class MatchCloneFactory : virtual public MatchIfFactory {
    public:
        ~MatchCloneFactory() override = default;
        MatchIf* getHandler(const ::apache::thrift::TConnectionInfo& connInfo) override
        {
            std::shared_ptr<TSocket> sock = std::dynamic_pointer_cast<TSocket>(connInfo.transport);
            /*
            cout << "Incoming connection\n";
            cout << "\tSocketInfo: "  << sock->getSocketInfo() << "\n";
            cout << "\tPeerHost: "    << sock->getPeerHost() << "\n";
            cout << "\tPeerAddress: " << sock->getPeerAddress() << "\n";
            cout << "\tPeerPort: "    << sock->getPeerPort() << "\n";
            */
            return new MatchHandler;
        }
        void releaseHandler(MatchIf* handler) override {
            delete handler;
        }
};

// 线程操作的函数，匹配消耗函数
void comsume_task()
{
    while(true)
    {
        unique_lock<mutex> lck(message_queue.m); // 涉及队列的读写，加锁
        if (message_queue.q.empty())
        {
            // message_queue.cv.wait(lck);
            lck.unlock();
            pool.match();
            sleep(1);
        }
        else
        {
            auto task = message_queue.q.front();
            message_queue.q.pop();
            lck.unlock(); // 在执行task时还可以添加和删除用户，若没有解锁，add_user remove_user线程无法进行

            // 具体任务
            if (task.type == "add") pool.add(task.user);
            else if (task.type == "remove") pool.remove(task.user);
        }
    }
}

int main(int argc, char **argv) {
    TThreadedServer server(
            std::make_shared<MatchProcessorFactory>(std::make_shared<MatchCloneFactory>()),
            std::make_shared<TServerSocket>(9090), //port
            std::make_shared<TBufferedTransportFactory>(),
            std::make_shared<TBinaryProtocolFactory>());


    cout << "Start Match Server!" << endl; // 提示开始匹配

    thread matching_thread(comsume_task);

    server.serve();
    return 0;
}

