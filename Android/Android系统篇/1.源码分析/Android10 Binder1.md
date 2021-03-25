Binder是Android中大量使用的IPC通信机制，当我们的程序涉及到进程之间的通信时，我们可以使用Android提供的Binder来完成。

要了解Binder，我们要先了解在Binder通信过程中的四大角色：Binder驱动、ServiceManager、Server和Client。

每个Android进程都运行在他们自己的虚拟地址空间中，其中一部分是用户空间，另一部分则是内核空间。每个进程的用户空间都是完全独立、互不相干的，而内核空间则是可以共享的。因此要进行进程间通信，不可避免要通过内核空间。

### IPC机制的对比

其他的进程间通信方式例如Pipe、Socket等需要进行两次数据的拷贝，进程一从用户空间到内核空间的拷贝和进程二从内核空间到用户空间的拷贝。而**Binder机制则只需要进行一次拷贝**，这是通过mmap()函数来实现的，mmap() 是操作系统中一种内存映射的方法。内存映射简单的讲就是将用户空间的一块内存区域映射到内核空间。映射关系建立后，用户对这块内存区域的修改可以直接反应到内核空间；反之内核空间对这段区域的修改也能直接反应到用户空间。共享内存方式虽然无需拷贝，但是控制复杂，难以使用。

![Binder机制原理图](https://upload-images.jianshu.io/upload_images/7403980-b5bc67d93e646def.jpg?imageMogr2/auto-orient/strip|imageView2/2/w/720/format/webp)

当然，Binder机制相较于其他IPC机制，还有基于C/S架构，职责明确，架构清晰的优点，以及可以为每个进程分配UID，鉴别进程的身份，安全性较好的优点

### Binder机制的组成部分

1. Binder驱动

   Binder驱动位于内核空间中，负责进程之间Binder通信的建立，Binder在进程之间的传递，Binder引用计数管理，数据包在进程之间的传递和交互等一系列底层支持

2. ServiceManager

   类似网络通信中的DNS服务器，负责将Client请求的Binder描述符转化为具体的Server地址，以便Binder驱动能够转发给具体的Server。Server如需提供Binder服务，需要向ServiceManager注册。

3. Server

   负责提供服务的进程

4. Client

   需要使用服务的进程

### Binder机制原理









































