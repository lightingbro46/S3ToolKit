# A lightweight network programming framework based on C++11

![](https://github.com/S3MediaKit/S3ToolKit/actions/workflows/linux.yml/badge.svg)
![](https://github.com/S3MediaKit/S3ToolKit/actions/workflows/macos.yml/badge.svg)
![](https://github.com/S3MediaKit/S3ToolKit/actions/workflows/windows.yml/badge.svg)

## Project Features

- Based on C++11 development, avoiding the use of naked pointers, the code is stable and reliable; at the same time, cross-platform porting is simple and convenient, and the code is clear and concise.
- Use epoll + thread pool + asynchronous network IO mode to develop, with superior concurrency performance.
- The code has undergone a lot of stability and performance testing to meet commercial server projects.
- Support linux, macos, ios, android, and windows platforms
- Learn more: [S3MediaKit](https://github.com/S3MediaKit/S3MediaKit)

## Features

- Network Library
  - tcp/udp client, the interface is simple and easy to use and thread-safe, and users do not have to care about specific socket API operations.
  - tcp/udp server is very simple to use. As long as you implement the specific tcp/udp session (Session class) logic, you can quickly build a high-performance server by using templates.
  - Encapsulation of sockets for multiple operations.
- Thread library
  - Simple and easy-to-use timer implemented using threads.
  - Semaphore.
  - Thread group.
  - Simple and easy-to-use thread pool, can perform tasks asynchronously or synchronously, and supports functional and lambad expressions.
- Tool library
  - File operation.
  - std::cout style log library, supports color highlighting, code positioning, and asynchronous printing.
  - Read and write INI configuration files.
  - Message broadcaster in listener mode.
  - A circular pool based on smart pointer, does not require explicit manual release.
  - Ring buffering, supporting two modes of active read and read event.
  - MySQL link pool, generates SQL statements using placeholder (?) method, and supports synchronous asynchronous operations.
  - Simple and easy-to-use SSL encryption and decryption black box, supporting multi-threading.
  - Some other useful tools.
  - Command line parsing tool, which can easily implement configurable applications

## Network IO adaptation

|              |  Linux (Android)  |       Windows       | MacOS (iOS/Unix) |
| :----------: | :---------------: | :-----------------: | :--------------: |
| Multiplexing |   epoll/select    | wepoll(iocp)/select |  kqueue/select   |
|     udp      | recvmmsg/sendmmsg |  recvfrom/WSASend   | recvfrom/sendto  |
|     tcp      | recvfrom/sendmsg  |  recvfrom/WSASend   | recvfrom/sendmsg |

## Compile (Linux)

- My compilation environment
  - Ubuntu16.04 64 bit + gcc5.4 (minimum gcc4.7)
  - cmake 3.5.1
- Compilation

  ```
  cd S3MediaKit
  ./build_for_linux.sh
  ```

## Compile (macOS)

- My compilation environment
  - macOS Sierra(10.12.1) + xcode8.3.1
  - Homebrew 1.1.3
  - cmake 3.8.0
- Compilation

  ```
  cd S3MediaKit
  ./build_for_mac.sh
  ```

## Compile (iOS)

- Compilation environment: `Please refer to the compilation guidance of macOS. `
- Compilation

  ```
  cd S3MediaKit
  ./build_for_ios.sh
  ```

- You can also generate Xcode projects and then compile:

  ```
  cd S3MediaKit
  mkdir -p build
  cd build
  # Generate Xcode project, the project file is in the build directory
  cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/iOS.cmake -DIOS_PLATFORM=SIMULATOR64 -G "Xcode"
  ```

## Compile (Android)

- My compilation environment
  - macOS Sierra(10.12.1) + xcode8.3.1
  - Homebrew 1.1.3
  - cmake 3.8.0
  - [android-ndk-r14b](https://dl.google.com/android/repository/android-ndk-r14b-darwin-x86_64.zip)
- Compilation

  ```
  cd S3MediaKit
  export ANDROID_NDK_ROOT=/path/to/ndk
  ./build_for_android.sh
  ```

## Compile (Windows)

- My compilation environment
  - windows 10
  - visual studio 2017
  - [openssl](http://slproweb.com/download/Win32OpenSSL-1_1_0f.exe)
  - [mysqlclient](https://dev.mysql.com/downloads/file/?id=472430)
  - [cmake-gui](https://cmake.org/files/v3.10/cmake-3.10.0-rc1-win32-x86.msi)
- Compilation

```
   1 Use cmake-gui to open the project and generate the vs project file.
   2 Find the project file (S3MediaKit.sln), and double-click to open it with vs2017.
   3 Select to compile the Release version.
   4 Compile S3MediaKit_static, S3MediaKit_shared, ALL_BUILD, INSTALL in turn.
   5 Find the target file and run the test case.
   6 Find the installed header file and library file (in the root directory of the partition where the source code is located).
```

## Authorization Agreement

The own code of this project uses a loose MIT protocol and can be freely applied to its respective commercial and non-commercial projects without retaining copyright information.
However, this project also uses some other open source code in fragments. Please replace or remove it yourself when it is commercially available;
Commercial disputes or infringements arising from the use of this project have nothing to do with this project and the developer. Please bear legal risks at your own discretion.

## QA

- How is the performance of this library?
  Based on S3MediaKit, I implemented a streaming server [S3MediaKit](https://github.com/S3MediaKit/S3MediaKit); the author has performed performance tests on it, and you can check [benchmark.md](https://github.com/S3MediaKit/S3MediaKit/blob/master/benchmark.md) for details.

- How is the stability of this library?

The library has been strictly tested by the author and tested with long-term and heavy loads; the author has also used the library to develop multiple online projects. Practice has proved that the library is very stable; it can be run continuously for several months without watchdog scripts.

- Many errors in compiling under windows?

Since the main code of this project is developed under macOS/linux, some source codes use UTF-8 encoding without bom headers; since Windows is not very friendly to UTF-8 support, if you find a compilation error, please try adding bom headers before compiling.

## Contact information

-Email: <1213642868@qq.com> (Please follow the issue process for questions related to this project or network programming, otherwise you will not reply by email)
-QQ Group: 542509000
