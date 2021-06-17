## Thread线程

###  创建

java中通过new Thread创建线程，当调用Thread.start方式时，会调用Thread.naticeCreateJni方法。

在ART虚拟机中，该jni方法会先在native层先创建一个thread对象，然后创建一个JniEnvExt对象于之绑定，再通过pthread_create向内核申请一个线程执行线程的初始化和java-thread的run方法。

Native线程初始化时，会设置栈底溢出保护，初始化EntryPoint，分配线程内部tid，添加到thread_list统一管理等。

### 线程运行，栈分配

每个ArtMethod方法执行时，会先在栈上创建MamagerStack对象，然后将thread.tls_ptr.top_manager_stack更新会当前对象，将之前的绑定加到当前对象的link单向链表中

ArtMethod执行时，会根据codeItem等信息，创建在栈上创建ShadowFrame方法，并将参数添加到vreg数组中。

ShadowFrame还存储了当前指令计数器dex_pc.

ArtMethod方法会在interpreter.cc方法中被解释执行，每个指令执行后会判断有没有异常产生，有的话会在catchBolck中查询是否能处理该异常的代码块，有的话将dex_pc指向该代码块起点。

### 异常触发

当执行指令时有异常产生时，会设置将异常设置到thread.exception中，当当前执行被解释执行完时，虚拟机会检查是否有异常产生，有的话就会处理异常

### interrupt打断线程

主要用来唤醒被Monitor对象锁住的情况，调用时会调用wait_cond.NotifyLock方法，唤醒锁，并在后续处理抛出打断异常

### join等待线程结束

调用join方法会执行Thread.lock对象的wait方法。在线程执行结束要销毁时，会调用Thread.lock对象的NotifyAll()方法。

