The source code is placed in the `src` folder, and there are several modules:

```
src
|
|--NetWork # Network Module
| |--Socket.cpp # Socket abstract encapsulation, including TCP server/client, UDP sockets
| |--Socket.h
| |--sockutil.cpp # Unified encapsulation of system network-related APIs
| |--sockutil.h
| |--TcpClient.cpp # TCP client encapsulation, derive this class to easily implement client programs
| |--TcpClient.h
| |--TcpServer.h # TCP server template class, it can easily implement a high-performance private protocol server
| |--Session.h # TCP/UDP service private protocol implements the session base class, used to process TCP/UDP long connection data and responses
|
|--Poller # Main thread event polling module
| |--EventPoller.cpp # Main thread, all network events are polled and triggered by this thread
| |--EventPoller.h
| |--Pipe.cpp # Object encapsulation of pipelines
| |--Pipe.h
| |--PipeWrap.cpp # PipeWrap, simulated by socket under Windows
| |--SelectWrap.cpp # simple packaging of the select model
| |--SelectWrap.h
| |--Timer.cpp # Timer triggered in the main thread
| |--Timer.h
|
|--Thread # Thread Module
| |--AsyncTaskThread.cpp # Background asynchronous task thread, you can submit a timed repeatable task background execution
| |--AsyncTaskThread.h
| |--rwmutex.h # Read and write lock, experimental nature
| |--semaphore.h # semaphore, implemented by conditional variables
| |--spin_mutex.h # Spin lock, suitable for low-latency critical zone, be careful when using single-core/low-performance equipment
| |--TaskQueue.h # functional task queue
| |--threadgroup.h # thread group, ported from boost
| |--ThreadPool.h # ThreadPool.h # ThreadPool, you can enter functional tasks to the background thread to execute
| |--WorkThreadPool.cpp # Get an available thread pool (thread load balancing allocation algorithm can be added)
| |--WorkThreadPool.h
|
|--Util # Tool Module
	|--File.cpp # File/Directory Operation Module
	|--File.h
	|--function_traits.h # function, lambda to functional
	|--logger.h # log module
	|--MD5.cpp # md5 encryption module
	|--MD5.h
	|--mini.h # ini configuration file read and write module, supports carriage return characters in unix/windows format
	|--NoticeCenter.h # Message broadcaster, can broadcast and pass any number of parameters of any type
	|--onceToken.h # Implemented using RAII mode, a piece of code can be executed during object construction and destruction
	|--ResourcePool.h # A loop pool based on smart pointers, no need to manually recycle objects
	|--RingBuffer.h # ring buffer, adaptable to size, suitable for GOP cache, etc.
|--SqlConnection.cpp # mysql client
	|--SqlConnection.h
	|--SqlPool.h # mysql connection pool, and simple and easy-to-use SQL statement generation tool
	|--SSLBox.cpp # openssl black box encapsulation, blocks SSL handshake details, supports multi-threading
	|--SSLBox.h
	|--TimeTicker.h # timer, can be used to count function execution time
	|--util.cpp # Some other tool codes are adapted to multiple systems
	|--util.h
	|--uv_errno.cpp # The error code system extracted from libuv, mainly for compatibility with windows
	|--uv_errno.h
	
```