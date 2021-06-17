servicemanager是Binder IPC通信过程中的守护进程。它用于查询和注册服务。

### 1.servicemanager启动

servicemanager进程由init进程通过解析init.rc文件启动的，我们可以查看servicemanager.rc来查看它的一些设置，它的执行程序为/system/bin/servicemanager 对应的文件为ServiceManager.cpp，它的进程名为servicemanager。

```c++
service servicemanager /system/bin/servicemanager
    class core animation
    user system
    group system readproc
    critical
    onrestart restart healthd
    onrestart restart zygote
    onrestart restart audioserver
    onrestart restart media
    onrestart restart surfaceflinger
    onrestart restart inputflinger
    onrestart restart drm
    onrestart restart cameraserver
    onrestart restart keystore
    onrestart restart gatekeeperd
    onrestart restart thermalservice
    writepid /dev/cpuset/system-background/tasks
    shutdown critical
```

#### 1.1 ServiceManager入口main()

servicemanager.rc位于`frameworks\native\cmds\servicemanager`路径下，在该路径下还存在ServiceManager.cpp的入口main.cpp

```c++
int main(int argc, char** argv) {
    
    if (argc > 2) {
        LOG(FATAL) << "usage: " << argv[0] << " [binder driver]";
    }
    //1.如果没有参数，则默认是/dev/binder
    const char* driver = argc == 2 ? argv[1] : "/dev/binder";
    //2.开启Binder驱动
    sp<ProcessState> ps = ProcessState::initWithDriver(driver);
    ps->setThreadPoolMaxThreadCount(0);
    ps->setCallRestriction(ProcessState::CallRestriction::FATAL_IF_NOT_ONEWAY);

    sp<ServiceManager> manager = new ServiceManager(std::make_unique<Access>());
    if (!manager->addService("manager", manager, false /*allowIsolated*/, IServiceManager::DUMP_FLAG_PRIORITY_DEFAULT).isOk()) {
        LOG(ERROR) << "Could not self register servicemanager";
    }
    
    IPCThreadState::self()->setTheContextObject(manager);
    ps->becomeContextManager(nullptr, nullptr);

    sp<Looper> looper = Looper::prepare(false /*allowNonCallbacks*/);

    BinderCallback::setupTo(looper);
    ClientCallbackCallback::setupTo(looper, manager);

    while(true) {
        looper->pollAll(-1);
    }

    // should not be reached
    return EXIT_FAILURE;
}
```

我们看到，在servicemanager.rc中，`service servicemanager /system/bin/servicemanager`后面并没有跟着参数，即传入main()中的参数是没有的，可是在main()中首先验证了参数的个数，无参的情况下，默认为/dev/binder。

那么什么时候会有参数呢？在Android8.0后，谷歌为方便设备供应商接入代码，引入Treble机制，binder机制增加了hwbinder和vndbinder，其中vndbinder的守护进程为vndservicemanager。

vndservicemanager和service共用同一份代码，只是传入的参数和宏控制的流程有部分差异。

```c++
service vndservicemanager /vendor/bin/vndservicemanager /dev/vndbinder
    class core
    user system
    group system readproc
    writepid /dev/cpuset/system-background/tasks
    shutdown critical
```

当开启的是vndbinder时，会传入参数/dev/vndbinder ，那么ServiceManager开启的驱动就是vndbinder。他们的区别如下，binder是Android Framework层与应用层之间的IPC，而vndbinder是供应商与供应商进程之间的IPC，当然还有hwbinder是Framework与供应商进程之间的IPC

![](https://img-blog.csdnimg.cn/20200329112935401.jpg)

#### 1.2 ProcessState::initWithDriver开启binder驱动

```c++
sp<ProcessState> ProcessState::initWithDriver(const char* driver)
{
    Mutex::Autolock _l(gProcessMutex);
    //gProcess就是ProcessState的单例对象
    if (gProcess != nullptr) {
        //存在gProcess直接返回
        if (!strcmp(gProcess->getDriverName().c_str(), driver)) {
            return gProcess;
        }
        LOG_ALWAYS_FATAL("ProcessState was already initialized.");
    }

    if (access(driver, R_OK) == -1) {
        ALOGE("Binder driver %s is unavailable. Using /dev/binder instead.", driver);
        driver = "/dev/binder";
    }
	//创建ProcessState单例对象
    gProcess = new ProcessState(driver);
    return gProcess;
}
```

ProcessState::initWithDriver()创建了ProcessState的单例对象，在ProcessState的构造函数中，会开启Binder驱动。实际上，ProcessState就是负责打开Binder节点和进行mmap映射，我们可以在它的构造函数中看到：

```c++
ProcessState::ProcessState(const char *driver)
    : mDriverName(String8(driver))
    , mDriverFD(open_driver(driver))//打开驱动
    , mVMStart(MAP_FAILED)
    , mThreadCountLock(PTHREAD_MUTEX_INITIALIZER)
    , mThreadCountDecrement(PTHREAD_COND_INITIALIZER)
    , mExecutingThreadsCount(0)
    , mMaxThreads(DEFAULT_MAX_BINDER_THREADS)
    , mStarvationStartTimeMs(0)
    , mBinderContextCheckFunc(nullptr)
    , mBinderContextUserData(nullptr)
    , mThreadPoolStarted(false)
    , mThreadPoolSeq(1)
    , mCallRestriction(CallRestriction::NONE)
{
    LOG_ALWAYS_FATAL("Cannot use libbinder in APEX (only system.img libbinder) since it is not stable.");
#endif

    if (mDriverFD >= 0) {
		//mmap映射内存
        //nullptr:表示映射区的起始位置由系统决定
        //BINDER_VM_SIZE: 映射区的大小
        //PROT_READ:表示映射区可读
        //MAP_PRIVATE：表示映射区的写入会有一个映射的复制 即copy-on-write 
        //mDriverFD：即要映射的文件，即Binder文件
        //0：从文件头开始映射
        mVMStart = mmap(nullptr, BINDER_VM_SIZE, PROT_READ, MAP_PRIVATE | MAP_NORESERVE, mDriverFD, 0);
        ......
    }
}
```

ProcessState会在构造时，通过函数open_driver()打开传入的driver，即就是Binder文件，并将返回的值设置为mDriverFD。然后通过mmap函数开启映射