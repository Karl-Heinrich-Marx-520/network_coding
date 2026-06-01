---

## 简介
之前的异步服务器为echo模式，但其存在安全隐患，就是在极端情况下客户端关闭导致触发写和读回调函数，二者都进入错误处理逻辑，进而造成二次析构的问题。  
下面我们介绍通过C11智能指针构造成一个伪闭包的状态延长session的生命周期。

## 智能指针管理Session
我们可以通过智能指针的方式管理Session类，将acceptor接收的链接保存在Session类型的智能指针里。由于智能指针会在引用计数为0时自动析构，所以为了防止其被自动回收，也方便Server管理Session，因为我们后期会做一些重连踢人等业务逻辑，我们在Server类中添加成员变量，该变量为一个map类型，key为Session的uid，value为该Session的智能指针。

```cpp
class CServer
{
public:
    CServer(boost::asio::io_context& io_context, short port);
    void ClearSession(std::string);
private:
    void HandleAccept(shared_ptr<CSession>, const boost::system::error_code & error);
    void StartAccept();
    boost::asio::io_context &_io_context;
    short _port;
    tcp::acceptor _acceptor;
    std::map<std::string, shared_ptr<CSession>> _sessions;
};
```

通过Server中的_sessions这个map管理链接，可以增加Session智能指针的引用计数，只有当Session从这个map中移除后，Session才会被释放。  
所以在接收连接的逻辑里将Session放入map

```cpp
void CServer::StartAccept() {
    shared_ptr<CSession> new_session = make_shared<CSession>(_io_context, this);
    _acceptor.async_accept(new_session->GetSocket(), std::bind(&CServer::HandleAccept, this, new_session, placeholders::_1));
}

void CServer::HandleAccept(shared_ptr<CSession> new_session, const boost::system::error_code& error){
    if (!error) {
        new_session->Start();
        _sessions.insert(make_pair(new_session->GetUuid(), new_session));
    }
    else {
        cout << "session accept failed, error is " << error.what() << endl;
    }

    StartAccept();
}
```

StartAccept函数中虽然new_session是一个局部变量，但是我们通过bind操作，将new_session作为数值传递给bind函数，而bind函数返回的函数对象内部引用了该new_session所以引用计数增加1，这样保证了new_session不会被释放。  
在HandleAccept函数里调用session的start函数监听对端收发数据，并将session放入map中，保证session不被自动释放。  
此外，需要封装一个释放函数，将session从map中移除，当其引用计数为0则自动释放

```cpp
void CServer::ClearSession(std::string uuid) {
    _sessions.erase(uuid);
}
```

## Session的uuid
关于session的uuid可以通过boost提供的生成唯一id的函数获得，当然你也可以自己实现雪花算法。

```cpp
CSession::CSession(boost::asio::io_context& io_context, CServer* server):
    _socket(io_context), _server(server){
    boost::uuids::uuid  a_uuid = boost::uuids::random_generator()();
    _uuid = boost::uuids::to_string(a_uuid);
}
```

另外我们修改Session中读写回调函数关于错误的处理，当读写出错的时候清除连接

```cpp
void CSession::HandleWrite(const boost::system::error_code& error) {
    if (!error) {
        std::lock_guard<std::mutex> lock(_send_lock);
        _send_que.pop();
        if (!_send_que.empty()) {
            auto &msgnode = _send_que.front();
            boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_max_len),
                std::bind(&CSession::HandleWrite, this, std::placeholders::_1));
        }
    }
    else {
        std::cout << "handle write failed, error is " << error.what() << endl;
        _server->ClearSession(_uuid);
    }
}

void CSession::HandleRead(const boost::system::error_code& error, size_t  bytes_transferred){
    if (!error) {

        cout << "read data is " << _data << endl;
        //发送数据
        Send(_data, bytes_transferred);
        memset(_data, 0, MAX_LENGTH);
        _socket.async_read_some(boost::asio::buffer(_data, MAX_LENGTH), std::bind(&CSession::HandleRead, this, std::placeholders::_1, std::placeholders::_2));
    }
    else {
        std::cout << "handle read failed, error is " << error.what() << endl;
        _server->ClearSession(_uuid);
    }
}
```

## 隐患
正常情况下上述服务器运行不会出现问题，但是当我们像上次一样模拟，在服务器要发送数据前打个断点，此时关闭客户端，在服务器就会先触发写回调函数的错误处理，再触发读回调函数的错误处理，这样session就会两次从map中移除，因为map中key唯一，所以第二次map判断没有session的key就不做移除操作了。  
但是这么做还是会有崩溃问题，因为第一次在session写回调函数中移除session，session的引用计数就为0了，调用了session的析构函数，这样在触发session读回调函数时此时session的内存已经被回收了自然会出现崩溃的问题。解决这个问题可以利用智能指针引用计数和bind的特性，实现一个伪闭包的机制延长session的生命周期。

## 如何构造伪闭包
思路：  
1   利用智能指针被复制或使用引用计数加一的原理保证内存不被回收  
2   bind操作可以将值绑定在一个函数对象上生成新的函数对象，如果将智能指针作为参数绑定给函数对象，那么智能指针就以值的方式被新函数对象使用，那么智能指针的生命周期将和新生成的函数对象一致，从而达到延长生命的效果。  
我们按照上面的思路改写我们的回调函数

```cpp
void HandleRead(const boost::system::error_code& error, 
size_t  bytes_transferred, shared_ptr<CSession> _self_shared);
void HandleWrite(const boost::system::error_code& error, shared_ptr<CSession> _self_shared);
```

以HandleWrite举例,在bind时传递_self_shared指针增加其引用计数，这样_self_shared的生命周期就和async_write的第二个参数(也就是asio要求的回调函数对象)生命周期一致了。

```cpp
void CSession::HandleWrite(const boost::system::error_code& error, shared_ptr<CSession> _self_shared) {
    if (!error) {
        std::lock_guard<std::mutex> lock(_send_lock);
        _send_que.pop();
        if (!_send_que.empty()) {
            auto &msgnode = _send_que.front();
            boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_max_len),
                std::bind(&CSession::HandleWrite, this, std::placeholders::_1, _self_shared));
        }
    }
    else {
        std::cout << "handle write failed, error is " << error.what() << endl;
        _server->ClearSession(_uuid);
    }
}
```

同样道理HandleRead内部也实现了类似的绑定

```cpp
void CSession::HandleRead(const boost::system::error_code& error, size_t  bytes_transferred, shared_ptr<CSession> _self_shared){
    if (!error) {

        cout << "read data is " << _data << endl;
        //发送数据
        Send(_data, bytes_transferred);
        memset(_data, 0, MAX_LENGTH);
        _socket.async_read_some(boost::asio::buffer(_data, MAX_LENGTH), std::bind(&CSession::HandleRead, this, 
            std::placeholders::_1, std::placeholders::_2, _self_shared));
    }
    else {
        std::cout << "handle read failed, error is " << error.what() << endl;
        _server->ClearSession(_uuid);
    }
}
```

除此之外，我们也要在第一次绑定读写回调函数的时候传入智能指针的值,但是要注意传入的方式，不能用两个智能指针管理同一块内存，如下用法是错误的。

```cpp
void CSession::Start(){
    memset(_data, 0, MAX_LENGTH);
    _socket.async_read_some(boost::asio::buffer(_data, MAX_LENGTH), std::bind(&CSession::HandleRead, this, 
        std::placeholders::_1, std::placeholders::_2, shared_ptr<CSession>(this)));
}
```

shared_ptr(this)生成的新智能指针和this之前绑定的智能指针并不共享引用计数，所以要通过shared_from_this()函数返回智能指针，该智能指针和其他管理这块内存的智能指针共享引用计数。

```cpp
void CSession::Start(){
    memset(_data, 0, MAX_LENGTH);
    _socket.async_read_some(boost::asio::buffer(_data, MAX_LENGTH), std::bind(&CSession::HandleRead, this, 
        std::placeholders::_1, std::placeholders::_2, shared_from_this()));
}
```

shared_from_this()函数并不是session的成员函数，要使用这个函数需要继承std::enable_shared_from_this

```cpp
class CSession:public std::enable_shared_from_this<CSession>
{
public:
    CSession(boost::asio::io_context& io_context, CServer* server);
    tcp::socket& GetSocket();
    std::string& GetUuid();
    void Start();
    void Send(char* msg,  int max_length);
private:
    void HandleRead(const boost::system::error_code& error, size_t  bytes_transferred, shared_ptr<CSession> _self_shared);
    void HandleWrite(const boost::system::error_code& error, shared_ptr<CSession> _self_shared);
    tcp::socket _socket;
    std::string _uuid;
    char _data[MAX_LENGTH];
    CServer* _server;
    std::queue<shared_ptr<MsgNode> > _send_que;
    std::mutex _send_lock;
};
```

同样的道理，我们在发送的时候也要绑定智能指针作为参数, 这里不做赘述。  
再次测试，链接可以安全释放，并不存在二次释放的问题。可以在析构函数内打印析构的信息，发现只析构一次  
<img src="https://cdn.llfc.club/20230410154807.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_14%2Ctext_5oGL5oGL6aOO6L6wemFjaw%3D%3D%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="496" title="null" crop="0,0,1,1" id="fQcUb" class="ne-image">

## 总结
我们通过C11的bind和智能指针实现了类似于go，js等语言的闭包功能，保证在回调函数触发之前Session都是存活的。  
源码链接  
[https://gitee.com/secondtonone1/boostasio-learn](https://gitee.com/secondtonone1/boostasio-learn)
