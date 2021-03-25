Android系统首先启动init进程，init进程会创建Zygote进程，我们都知道，Zygote进程是所有进程的鼻祖，除init进程和Zygote进程外的其他进程，源码中称为**普通进程**（unspecialized app process）都是由Zygote进程fork出来的。

当我们理解了Zygote进程，其实也就理解了其他各种进程。

### 1.Zygote如何启动？

#### 1.1 init.rc解析

init进程启动之后，会解析init.rc文件：

```c++
// system\core\rootdir\init.rc
...
import /system/etc/init/hw/init.${ro.zygote}.rc
...
```

通过上面的代码会引入加载Zygote的部分的rc文件，其中${ro.zygote}根据手机系统来决定，从Android5.0开始，Android支持64位的编译，zygote本身也就有了32位和64位的区别，在system\core\rootdir\下除了init.rc文件之外，还包括了四个关于zygote的rc文件：*init.zygote32.rc、init.zygote32_64.rc、init.zygote64.rc、init.zygote64_32.rc*，由硬件决定调用哪个文件

目前大部分手机使用的是init.zygote64_32.rc，表示支持64位并且兼容32位，因此init会启动两个zygote进程，我们来看：

```c++
// system\core\rootdir\init.zygote64_32.rc
service zygote /system/bin/app_process64 -Xzygote /system/bin --zygote --start-system-server --socket-name=zygote
    class main
    priority -20
    user root  //用户为root
    group root readproc reserved_disk
    socket zygote stream 660 root system  
    socket usap_pool_primary stream 660 root system
    onrestart write /sys/power/state on
    onrestart restart audioserver
    onrestart restart cameraserver
    onrestart restart media
    onrestart restart netd
    onrestart restart wificond
    writepid /dev/cpuset/foreground/tasks

service zygote_secondary /system/bin/app_process32 -Xzygote /system/bin --zygote --socket-name=zygote_secondary --enable-lazy-preload
    class main
    priority -20
    user root
    group root readproc reserved_disk
    socket zygote_secondary stream 660 root system
    socket usap_pool_secondary stream 660 root system
    onrestart restart zygote
    writepid /dev/cpuset/foreground/tasks

```

我们看到，在这个文件中，定义了由service指令启动两个进程，一个是zygote进程，一个是zygote_secondary进程

- zygote进程
  1. 进程通过 /system/bin/app_process64 启动
  2. 启动参数：-Xzygote /system/bin --zygote --start-system-server --socket-name=zygote
  3. 创建Socket： zygote和usap_pool_primary 可以在手机目录的/dev/socket/中看到这两个socket文件
- zygote_secondary进程
  1.  进程通过 /system/bin/app_process32 启动
  2. 启动参数：-Xzygote /system/bin --zygote --socket-name=zygote_secondary --enable-lazy-preload
  3. 创建Socket： zygote_secondary 和 usap_pool_secondary 可以在手机目录的/dev/socket/中看到这两个socket文件

我们看到，zygote进程和zygote_secondary进程都会创建两个socket，并且权限设置为660，即-rw-rw--- 表示**拥有者**和**属组用户**有读写权限，其他用户没有读写权限

zygote是通过进程文件app_process64和app_process32来启动的，它的代码实现是app_main.cpp，它位于`frameworks\base\cmds\app_process`，在该文件夹下面还有它的配置文件Android.bp，根据该配置文件，我们可以知道，app_process的确对应着app_main.cpp

```c++
//Android.bp
cc_binary {
    name: "app_process",
    srcs: ["app_main.cpp"],
	//......
}
```

### 2. 进入Native-C层

#### 2.1 app_main.cpp

```c++
//app_main.cpp
int main(int argc, char* const argv[])
{
    //zygote传入的argv为 -Xzygote /system/bin --zygote --start-system-server --socket-name=zygote
    //zygote_secondary传入的argv为 -Xzygote /system/bin --zygote --socket-name=zygote_secondary --enable-lazy-preload
    
    //创建一个AppRuntime对象
    AppRuntime runtime(argv[0], computeArgBlockSize(argc, argv));
	......
    //处理参数，在第一个无法识别处停止
    bool zygote = false;
    bool startSystemServer = false;
    bool application = false;
    String8 niceName;
    String8 className;

    ++i;
    while (i < argc) {
        const char* arg = argv[i++];
        if (strcmp(arg, "--zygote") == 0) {
            //传入的参数包含--zygote，设置zygote为true
            zygote = true;
            //niceName： 64位系统——》zygote64,32位系统——》zygote
            niceName = ZYGOTE_NICE_NAME;
        } else if (strcmp(arg, "--start-system-server") == 0) {
            //zygote进程为true；zygote_secondary为false
            //表示zygote进程要开启system_server进程，zygote_secondary则不需要
            startSystemServer = true;
        } else if (strcmp(arg, "--application") == 0) {
            application = true;
        } else if (strncmp(arg, "--nice-name=", 12) == 0) {
            niceName.setTo(arg + 12);
        } else if (strncmp(arg, "--", 2) != 0) {
            className.setTo(arg);
            break;
        } else {
            --i;
            break;
        }
    }
    // 定义要传入AppRuntime中的start函数的参数args
	Vector<String8> args;
    if (!className.isEmpty()) {
        //如果className不为空，说明传入的参数必定没有"--zygote"即不是zygote模式
        //这里暂不分析此种情况
        ...
    } else {
        //进入zygote模式
        //新建Dalvik的缓存目录 /data/dalvik-cache
        maybeCreateDalvikCache();
		
        if (startSystemServer) {
            //由传入的参数知，zygote进程会添加"start-system-server"，zygote_secondary则不会
            args.add(String8("start-system-server"));
        }

        char prop[PROP_VALUE_MAX];
        if (property_get(ABI_LIST_PROPERTY, prop, NULL) == 0) {
            LOG_ALWAYS_FATAL("app_process: Unable to determine ABI list from property %s.",
                ABI_LIST_PROPERTY);
            return 11;
        }
		//加入--abi-list= 参数 即cpu的指令集 armeabi x86等
        String8 abiFlag("--abi-list=");
        abiFlag.append(prop);
        args.add(abiFlag);

        //将传入app_main.cpp的main函数中的剩下的参数添加到args中
        for (; i < argc; ++i) {
            args.add(String8(argv[i]));
        }
    }
    
    if (!niceName.isEmpty()) { //zygote模式，则niceName不是空的
        //设置进程名  zygote/zygote64  默认为app_process
        runtime.setArgv0(niceName.string(), true /* setProcName */);
    }
    
    if (zygote) {
		//zygote模式，调用ZygoteInit的main方法，进入Java世界
        runtime.start("com.android.internal.os.ZygoteInit", args, zygote);
    } else if (className) {
        //application模式，加载RuntimeInit
        runtime.start("com.android.internal.os.RuntimeInit", args, zygote);
    } else {
        //没有指定类名或zygote，参数错误
        fprintf(stderr, "Error: no class name or --zygote supplied.\n");
        app_usage();
        LOG_ALWAYS_FATAL("app_process: no class name or --zygote supplied.");
    }
}
```

zygote进程/zygote_secondary进程最终会调用AppRuntime的start()方法进行虚拟机的加载并执行ZygoteInit的main方法进入Java世界

#### 2.2 AppRuntime.start()

AppRuntime类在app_main.cpp中定义，它是AndroidRuntime的派生类，它的start()方法是在AndroidRuntime中：

```c++
//frameworks\base\core\jni\AndroidRuntime.cpp
void AndroidRuntime::start(const char* className, const Vector<String8>& options, bool zygote)
{
    /**
    * 这里参数中的options即在app_main.start()中传入的args
  	* 根据之前的分析，我们可以知道参数是：
  	* zygote: start-system-server --abi-list=... zygote64 --socket-name=zygote
  	* zygote_secondary:--abi-list=... zygote --socket-name=zygote_secondary --enable-     * lazy-preload
  	**/
    ...
    /* 1. 首先启动虚拟机 */
    JniInvocation jni_invocation;
    // 1.1通过JniInvocation初始化一些本地方法函数
    jni_invocation.Init(NULL);
    JNIEnv* env;
	// 1.2启动虚拟机
    if (startVm(&mJavaVM, &env, zygote, primary_zygote) != 0) {
        return;
    }
    // 1.3虚拟机创建成功的回调函数
    onVmCreated(env);

    /*
     * 2. 在startReg(env)中，注册Android的JNI函数
     */
    if (startReg(env) < 0) {
        ALOGE("Unable to register all android natives\n");
        return;
    }

    /*
     * 3. 解析要启动的Java类的类名以及参数
     */
    jclass stringClass;
    jobjectArray strArray;
    jstring classNameStr;
	//3.1 得到一个String数组，长度为 options的长度加1
    stringClass = env->FindClass("java/lang/String");
    assert(stringClass != NULL);
    strArray = env->NewObjectArray(options.size() + 1, stringClass, NULL);
    assert(strArray != NULL);
    classNameStr = env->NewStringUTF(className);
    assert(classNameStr != NULL);
    //将className即com.android.internal.os.ZygoteInit放入String数组的第一个
    env->SetObjectArrayElement(strArray, 0, classNameStr);
	//将剩下的options放入String数组
    for (size_t i = 0; i < options.size(); ++i) {
        jstring optionsStr = env->NewStringUTF(options.itemAt(i).string());
        assert(optionsStr != NULL);
        env->SetObjectArrayElement(strArray, i + 1, optionsStr);
    }
	//3.2 找到ZygoteInit这个类，并执行类中的main()
	//3.2.1 首先将 传入的com.android.internal.os.ZygoteInit 将.替换为com/android/internal/os/ZygoteInit
    char* slashClassName = toSlashClassName(className != NULL ? className : "");
    //3.2.2找到ZygoteInit这个类
    jclass startClass = env->FindClass(slashClassName);
    if (startClass == NULL) {
        ALOGE("JavaVM unable to locate class '%s'\n", slashClassName);
    } else {
        //3.2.3 找到ZygoteInit类后，找类中的main()方法
        jmethodID startMeth = env->GetStaticMethodID(startClass, "main",
            "([Ljava/lang/String;)V");
        if (startMeth == NULL) {
            ALOGE("JavaVM unable to find main() in '%s'\n", className);
        } else {
        	//3.2.4 找到方法后，调用该方法，并传入参数strArray ，即调用ZygoteInit.main()
            env->CallStaticVoidMethod(startClass, startMeth, strArray);

#if 0
            if (env->ExceptionCheck())
                threadExitUncaughtException(env);
#endif
        }
    }
    //释放内存空间
    free(slashClassName);
    ALOGD("Shutting down VM\n");
    if (mJavaVM->DetachCurrentThread() != JNI_OK)
        ALOGW("Warning: unable to detach main thread\n");
    if (mJavaVM->DestroyJavaVM() != 0)
        ALOGW("Warning: VM did not shut down cleanly\n");
}
```

##### 2.2.1 启动虚拟机

start()函数首先通过`JniInvocation.Init()`初始化JNI:

###### 2.2.1.2 创建虚拟机

通过startVM()创建虚拟机：

```c++
int AndroidRuntime::startVm(JavaVM** pJavaVM, JNIEnv** pEnv, bool zygote, bool primary_zygote)
{
    //这里省略了一大堆代码，主要是对于虚拟机软硬件环境的配置，这些参数一般需要调整、优化，从而使系统达到最佳性能
	......
     //调用之前JniInvocation初始化的JNI_CreateJavaVM
    if (JNI_CreateJavaVM(pJavaVM, pEnv, &initArgs) < 0) {
        ALOGE("JNI_CreateJavaVM failed\n");
        return -1;
    }

    return 0;
}
```

###### 2.2.1.3 虚拟机创建成功回调

当虚拟机创建成功后，执行onVmCreated(env)，这个函数在AndroidRuntime中是一个空函数，是由AppRuntime实现，但是在zygote模式中，什么也不做，所以这里就不贴代码了

##### 2.2.2 startReg 注册Android JNI方法

```c++
/*static*/ int AndroidRuntime::startReg(JNIEnv* env)
{
    ATRACE_NAME("RegisterAndroidNatives");
    //设置Android创建线程的函数javaCreateThreadEtc 
    androidSetCreateThreadFunc((android_create_thread_fn) javaCreateThreadEtc);

    ALOGV("--- registering native functions ---\n");
	//创建一个200容量的局部引用作用域
    env->PushLocalFrame(200);
    //注册jni方法
    if (register_jni_procs(gRegJNI, NELEM(gRegJNI), env) < 0) {
        env->PopLocalFrame(NULL);
        return -1;
    }
    //释放一个200容量的局部引用作用域
    env->PopLocalFrame(NULL);
    return 0;
}
```

##### 2.2.3 反射调用ZygoteInit.main()

### 3. 进入Java层

#### 3.1 ZygoteInit.main

```java
//frameworks\base\core\java\com\android\internal\os\ZygoteInit.java
public static void main(String argv[]) {
        //声明一个ZygoteServer,用来管理和子进程通信的服务端
        ZygoteServer zygoteServer = null;
        //设置标志位，开启zygote创建，zygote进程不需要开启线程
        ZygoteHooks.startZygoteNoThreadCreation();

        //设置当前进程id为0（第一个0），设置当前进程加入一个进程组，并成为主进程（第二个0）
        try {
            Os.setpgid(0, 0);
        } catch (ErrnoException ex) {
            throw new RuntimeException("Failed to setpgid(0,0)", ex);
        }

        Runnable caller;
        try {
            //获取系统属性，判断系统重启完成
            if (!"1".equals(SystemProperties.get("sys.boot_completed"))) {
                MetricsLogger.histogram(null, "boot_zygote_init",
                        (int) SystemClock.elapsedRealtime());
            }
		    //判断当前进程是64位还是32位，并设置标记
            String bootTimeTag = Process.is64Bit() ? "Zygote64Timing" : "Zygote32Timing";
            TimingsTraceLog bootTimingsTraceLog = new TimingsTraceLog(bootTimeTag,
                    Trace.TRACE_TAG_DALVIK);
            bootTimingsTraceLog.traceBegin("ZygoteInit");
            //1.fork之前的初始化工作
            RuntimeInit.preForkInit();
  
            /**
             * 解析参数
             *zygote: start-system-server --abi-list=... zygote64 --socket-name=zygote
  			 *zygote_secondary:--abi-list=... zygote --socket-name=zygote_secondary --enable-lazy-preload
  			 */
            boolean startSystemServer = false;
            String zygoteSocketName = "zygote";
            String abiList = null;
            boolean enableLazyPreload = false;
            for (int i = 1; i < argv.length; i++) {
                if ("start-system-server".equals(argv[i])) {
                    //zygote进程为true，zygote_secondary进程为false
                    startSystemServer = true;
                } else if ("--enable-lazy-preload".equals(argv[i])) {
                     //zygote进程为flase，zygote_secondary进程为true
                    enableLazyPreload = true;
                } else if (argv[i].startsWith(ABI_LIST_ARG)) {
                    abiList = argv[i].substring(ABI_LIST_ARG.length());
                } else if (argv[i].startsWith(SOCKET_NAME_ARG)) {
                    //zygote进程为zygote，zygote_secondary进程为zygote_secondary
                    zygoteSocketName = argv[i].substring(SOCKET_NAME_ARG.length());
                } else {
                    throw new RuntimeException("Unknown command line argument: " + argv[i]);
                }
            }
            //进程名为socket名称为zygote的是第一个Zygote
            final boolean isPrimaryZygote = zygoteSocketName.equals(Zygote.PRIMARY_SOCKET_NAME);

            if (abiList == null) {
                throw new RuntimeException("No ABI list supplied.");
            }

			//zygote进程enableLazyPreload为false，会进入进行资源预加载
            if (!enableLazyPreload) {
                ......
                //3.加载进程的资源和类
                preload(bootTimingsTraceLog);
        		......
            }
            bootTimingsTraceLog.traceBegin("PostZygoteInitGC");
            gcAndFinalize();
            bootTimingsTraceLog.traceEnd(); // PostZygoteInitGC

            bootTimingsTraceLog.traceEnd(); // ZygoteInit
			//初始化zygote的状态
            Zygote.initNativeState(isPrimaryZygote);
			//结束zygote创建
            ZygoteHooks.stopZygoteNoThreadCreation();
            //4.创建ZygoteServer，并创建ServerSocket用于接收消息
            zygoteServer = new ZygoteServer(isPrimaryZygote);

            if (startSystemServer) {
                //5. fork SystemServer进程 zygote进程才会进入，zygote_secondary则不进入
                Runnable r = forkSystemServer(abiList, zygoteSocketName, zygoteServer);
                //这里会返回两次，当r!=null时，表示在新进程system_server中
                if (r != null) {
                    //在system_server进程中启动SystemServer
                    r.run();
                    return;
                }
            }
			//当上面的r==null,表示在zygote中，或者是在zygote_secondary中
            //进入无限循环，处理请求
            caller = zygoteServer.runSelectLoop(abiList);
        } catch (Throwable ex) {
            Log.e(TAG, "System zygote died with exception", ex);
            throw ex;
        } finally {
            if (zygoteServer != null) {
                zygoteServer.closeServerSocket();
            }
        }
        //runSelectLoop中如果收到请求，fork出了子进程，就会在子进程返回caller，
        //那么在子进程中就要执行这个Runnable
        if (caller != null) {
            caller.run();
        }
    }
```

##### 3.1.1 preForkInit

zygote进程在preForkInit中开启了ddms

```java
    public static void preForkInit() {
        RuntimeHooks.setThreadPrioritySetter(new RuntimeThreadPrioritySetter());
        //开启ddms
        RuntimeInit.enableDdms();
        MimeMap.setDefaultSupplier(DefaultMimeMapFactory::create);
    }
```

##### 3.1.2 preload

zygote进程会进行预加载资源，这样系统只在zygote执行一次加载操作，所有APP用到该资源不需要再重新加载，减少资源加载时间，加快了应用启动速度，一般情况下，系统中App共享的资源会被列为预加载资源。

```java
//ZygoteInit.java
static void preload(TimingsTraceLog bootTimingsTraceLog) {
	 beginPreload();
	 //预加载/system/etc/preloaded-classes中的类
	 preloadClasses();
	 //预加载一些应用需要使用但是不能放入bootclasspath中的jar包
	 cacheNonBootClasspathClassLoaders();
	 //预加载资源，包含drawable和color资源
	 preloadResources();
	 nativePreloadAppProcessHALs();
	 //预加载OpenGL或者Vulkan Vulkan也是一个2D、3D绘图接口
	 maybePreloadGraphicsDriver();
	 //通过System.loadLibrary 加载Jni库
	 preloadSharedLibraries();
	 //预加载文本连接符资源
	 preloadTextResources();
	 //预先在Zygote中做好加载WebView的准备
	 WebViewFactory.prepareWebViewInZygote();
	 endPreload();
	 sPreloadComplete = true;
}
```

##### 3.1.3 ZygoteServer()

```java
   ZygoteServer(boolean isPrimaryZygote) {
        mUsapPoolEventFD = Zygote.getUsapPoolEventFD();
		//根据是zygote还是zygote_secondary创建不同的Socket
        if (isPrimaryZygote) {
            //zygote进程创建两个Socket 名称分别为 zygote 和 usap_pool_primary
            mZygoteSocket = Zygote.createManagedSocketFromInitSocket(Zygote.PRIMARY_SOCKET_NAME);
            mUsapPoolSocket =
                    Zygote.createManagedSocketFromInitSocket(
                            Zygote.USAP_POOL_PRIMARY_SOCKET_NAME);
        } else {
             //zygote_secondary进程创建两个Socket 名称分别为 zygote_secondary 和 usap_pool_secondary
            mZygoteSocket = Zygote.createManagedSocketFromInitSocket(Zygote.SECONDARY_SOCKET_NAME);
            mUsapPoolSocket =
                    Zygote.createManagedSocketFromInitSocket(
                            Zygote.USAP_POOL_SECONDARY_SOCKET_NAME);
        }

        mUsapPoolSupported = true;
        fetchUsapPoolPolicyProps();
    }
    
    static LocalServerSocket createManagedSocketFromInitSocket(String socketName) {
        int fileDesc;
        final String fullSocketName = ANDROID_SOCKET_PREFIX + socketName;

        try {
            //init.zygote64_32.rc启动时，指定了4个socket：
            //分别是zygote、usap_pool_primary、zygote_secondary、usap_pool_secondary
            // 在进程被创建时，就会创建对应的文件描述符，并加入到环境变量中
            // 这里取出对应的环境变量
            String env = System.getenv(fullSocketName);
            fileDesc = Integer.parseInt(env);
        } catch (RuntimeException ex) {
            throw new RuntimeException("Socket unset or invalid: " + fullSocketName, ex);
        }

        try {
            FileDescriptor fd = new FileDescriptor();
            fd.setInt$(fileDesc);//设置文件描述符
            return new LocalServerSocket(fd);//创建Socket的本地服务端
        } catch (IOException ex) {
            ...
        }
    }
```

##### 3.1.4 forkSystemServer

```java
   private static Runnable forkSystemServer(String abiList, String socketName,
            ZygoteServer zygoteServer) {
       //......
       //用args数组保存SystemServer的启动参数
        String args[] = {
                "--setuid=1000",
                "--setgid=1000",
                "--setgroups=1001,1002,1003,1004,1005,1006,1007,1008,1009,1010,1018,1021,1023,"
                        + "1024,1032,1065,3001,3002,3003,3006,3007,3009,3010,3011",
                "--capabilities=" + capabilities + "," + capabilities,
                "--nice-name=system_server",
                "--runtime-args",
                "--target-sdk-version=" + VMRuntime.SDK_VERSION_CUR_DEVELOPMENT,
                "com.android.server.SystemServer",
        };
        ZygoteArguments parsedArgs = null;

        int pid;

        try {
            //解析参数
            parsedArgs = new ZygoteArguments(args);
            Zygote.applyDebuggerSystemProperty(parsedArgs);
            Zygote.applyInvokeWithSystemProperty(parsedArgs);
			//fork 子进程，用于运行system_server
            pid = Zygote.forkSystemServer(
                    parsedArgs.mUid, parsedArgs.mGid,
                    parsedArgs.mGids,
                    parsedArgs.mRuntimeFlags,
                    null,
                    parsedArgs.mPermittedCapabilities,
                    parsedArgs.mEffectiveCapabilities);
        } catch (IllegalArgumentException ex) {
            throw new RuntimeException(ex);
        }
        if (pid == 0) {
            //当pic==0时，表示在子进程system_server中
            if (hasSecondZygote(abiList)) {
                waitForSecondaryZygote(socketName);
            }
            //子进程不需要开启ServerSocket
            zygoteServer.closeServerSocket();
            //完成system_server进程中的工作
            return handleSystemServerProcess(parsedArgs);
        }
        return null;
    }
```

zygote进程中会fork出system_server进程并执行SystemServer，在forkSystemServer()方法的末尾处会返回handleSystemServerProcess(parsedArgs)，该函数最终会得到一个Runnable，Runnable中的run()方法则会调用SystemServer的main()函数，具体我们在SystemServer一文中进行讲解。

#### 3.2 ZygoteServer.runSelectLoop

当ZygoteInit fork了一个子进程system_server后，子进程执行SystemServer的代码，而父进程zygote则进入无限循环，开启消息轮询

```java
  Runnable runSelectLoop(String abiList) {
        //创建Socket的文件描述符的集合
        ArrayList<FileDescriptor> socketFDs = new ArrayList<>();
        //创建与Zygote进程建立的连接的集合
        ArrayList<ZygoteConnection> peers = new ArrayList<>();
		//将ZygoteSocket的文件描述符先加入集合，mZygoteSocket就是
        //ZygoteServer的构造方法创建的那个LocalServerSocket
        socketFDs.add(mZygoteSocket.getFileDescriptor());
        peers.add(null);

        mUsapPoolRefillTriggerTimestamp = INVALID_TIMESTAMP;
		
        //进入无限循环
        while (true) {
            fetchUsapPoolPolicyPropsWithMinInterval();
            mUsapPoolRefillAction = UsapPoolRefillAction.NONE;

            int[] usapPipeFDs = null;
            //创建一个数组，表示要监听的文件描述符
            StructPollfd[] pollFDs;

            if (mUsapPoolEnabled) {
                //默认开启usapPool
                //这里的数量就是（ ZygoteSocket + eventFD + usap的Pipe） 他们的文件描述符
                usapPipeFDs = Zygote.getUsapPipeFDs();
                pollFDs = new StructPollfd[socketFDs.size() + 1 + usapPipeFDs.length];
            } else {
                pollFDs = new StructPollfd[socketFDs.size()];
            }
            int pollIndex = 0;
            for (FileDescriptor socketFD : socketFDs) {
                pollFDs[pollIndex] = new StructPollfd();
                pollFDs[pollIndex].fd = socketFD;
                pollFDs[pollIndex].events = (short) POLLIN;
                ++pollIndex;
            }
			//上面的循环将socketFDs中的FD设置进pollFDs中
            //那么第一次执行到这里时，socketFDs中应该只有一个ZygoteSocket的FD
            //（下面的注释不理解可以先阅读下面的代码再回头看）
            //当之后循环来到这里时，socketFDs中，第一个是ZygoteSocket的FD，
            //剩下的是与ZygoteSocket建立的连接的FD
            
            //此时，usapPoolEventFDIndex就是一个分割点，
            //[0,usapPoolEventFDIndex)的就是Socket的消息，之后的就是
            //[usapPoolEventFDIndex,end]的是eventFD和usap的Pipe的消息（我们这里不关系这两个消息）
            final int usapPoolEventFDIndex = pollIndex;

            if (mUsapPoolEnabled) {
                //先加入eventFD
                pollFDs[pollIndex] = new StructPollfd();
                pollFDs[pollIndex].fd = mUsapPoolEventFD;
                pollFDs[pollIndex].events = (short) POLLIN;
                ++pollIndex;
                //再循环加入usap的pipe
                assert usapPipeFDs != null;
                for (int usapPipeFD : usapPipeFDs) {
                    FileDescriptor managedFd = new FileDescriptor();
                    managedFd.setInt$(usapPipeFD);

                    pollFDs[pollIndex] = new StructPollfd();
                    pollFDs[pollIndex].fd = managedFd;
                    pollFDs[pollIndex].events = (short) POLLIN;
                    ++pollIndex;
                }
            }

            int pollTimeoutMs;

            if (mUsapPoolRefillTriggerTimestamp == INVALID_TIMESTAMP) {
                pollTimeoutMs = -1;
            } else {
                long elapsedTimeMs = System.currentTimeMillis() - mUsapPoolRefillTriggerTimestamp;
                if (elapsedTimeMs >= mUsapPoolRefillDelayMs) {
                    pollTimeoutMs = -1;
                } else if (elapsedTimeMs <= 0) {
                    pollTimeoutMs = mUsapPoolRefillDelayMs;
                } else {
                    pollTimeoutMs = (int) (mUsapPoolRefillDelayMs - elapsedTimeMs);
                }
            }

            int pollReturnValue;
            try {
                //轮询pollFDs是否有变化，返回值表示有变化的FD的个数
                pollReturnValue = Os.poll(pollFDs, pollTimeoutMs);
            } catch (ErrnoException ex) {
                throw new RuntimeException("poll failed", ex);
            }
		
            if (pollReturnValue == 0) {
                //等于0表示超时了也没有变化
                mUsapPoolRefillTriggerTimestamp = INVALID_TIMESTAMP;
                mUsapPoolRefillAction = UsapPoolRefillAction.DELAYED;
            } else {
                //设置一个标志位，表示是否处理了usap的消息
                boolean usapPoolFDRead = false;
                //遍历FD数组
                while (--pollIndex >= 0) {
                    //如果当前FD没有变化，continue
                    if ((pollFDs[pollIndex].revents & POLLIN) == 0) {
                        continue;
                    }
                    if (pollIndex == 0) {
						//由于pollFDs先加入的时sockedFDs，socketFDs只有一个FD，
                        //即ZygoteSocket的FD，所以当pollIndex==0时，
                        //表示ZygoteSocket收到了建立连接的请求
                        ZygoteConnection newPeer = acceptCommandPeer(abiList);
                        //将简历好的连接加入到peers中
                        peers.add(newPeer);
                        //将连接的FD加入到socketFDs中，那么while的下次循环
                        //就会在加入ZygoteSocket的FD之后，加入这个FD 
                        //（这里看懂了就可以理解usapPoolEventFDIndex那里了）
                        socketFDs.add(newPeer.getFileDescriptor());

                    } else if (pollIndex < usapPoolEventFDIndex) {
                        //当pollIndex大于0但是又小于usapPoolEventFDIndex
                        //那么表示的就是已经建立的连接的消息，表示有数据请求
                        try {
                            ZygoteConnection connection = peers.get(pollIndex);
                            //processOneCommand会fork出子进程
                            final Runnable command = connection.processOneCommand(this);
                            //由于fork了进程，会返回两次
                            if (mIsForkChild) {
                                //处于子进程时，command不为空
                                if (command == null) {
                                    throw new IllegalStateException("command == null");
                                }
                                //这里直接返回command
                                //交给ZygoteInit.main()方法的末尾，就会执行这个Runnable
                                return command;
                            } else {
                                //处于父进程，command应该要是null
                                if (command != null) {
                                    throw new IllegalStateException("command != null");
                                }
                                //处理完请求之后，要从集合中移除这个连接
                                if (connection.isClosedByPeer()) {
                                    connection.closeSocket();
                                    peers.remove(pollIndex);
                                    socketFDs.remove(pollIndex);
                                }
                            }
                        } catch (Exception e) {
                            ......
                        } finally {
                            ......
                        }
                    } else {
						//进入这个分支，表示pollIndex >= usapPoolEventFDIndex
                        //那么消息是eventFD 或者 是 pipe的FD ..这里就不分析啦
                        long messagePayload;
                        try {
                            byte[] buffer = new byte[Zygote.USAP_MANAGEMENT_MESSAGE_BYTES];
                            int readBytes =
                                    Os.read(pollFDs[pollIndex].fd, buffer, 0, buffer.length);
                            if (readBytes == Zygote.USAP_MANAGEMENT_MESSAGE_BYTES) {
                                DataInputStream inputStream =
                                        new DataInputStream(new ByteArrayInputStream(buffer));
                                messagePayload = inputStream.readLong();
                            } else {
                                Log.e(TAG, "Incomplete read from USAP management FD of size "
                                        + readBytes);
                                continue;
                            }
                        } catch (Exception ex) {
                            ...
                            continue;
                        }

                        if (pollIndex > usapPoolEventFDIndex) {
                            Zygote.removeUsapTableEntry((int) messagePayload);
                        }
                        usapPoolFDRead = true;
                    }
                }

                if (usapPoolFDRead) {
                    int usapPoolCount = Zygote.getUsapPoolCount();

                    if (usapPoolCount < mUsapPoolSizeMin) {
                        // Immediate refill
                        mUsapPoolRefillAction = UsapPoolRefillAction.IMMEDIATE;
                    } else if (mUsapPoolSizeMax - usapPoolCount >= mUsapPoolRefillThreshold) {
                        // Delayed refill
                        mUsapPoolRefillTriggerTimestamp = System.currentTimeMillis();
                    }
                }
            }

            if (mUsapPoolRefillAction != UsapPoolRefillAction.NONE) {
                int[] sessionSocketRawFDs =
                        socketFDs.subList(1, socketFDs.size())
                                .stream()
                                .mapToInt(FileDescriptor::getInt$)
                                .toArray();

                final boolean isPriorityRefill =
                        mUsapPoolRefillAction == UsapPoolRefillAction.IMMEDIATE;

                final Runnable command =
                        fillUsapPool(sessionSocketRawFDs, isPriorityRefill);

                if (command != null) {
                    return command;
                } else if (isPriorityRefill) {
                    // Schedule a delayed refill to finish refilling the pool.
                    mUsapPoolRefillTriggerTimestamp = System.currentTimeMillis();
                }
            }
        }
    }
```

Zygote进程在runSelectLoop中进行消息的处理，我们可以看到，Zygote会接收三种类型的消息传递，一种是通过Socket传递的，在注释中分析的应该比较清楚了。还有两种是eventFD传递的消息以及Pipe传递的消息。

我们这里只分析一下Socket的消息，通过Os.poll()轮询要监听的文件描述符数组pollFDs，当有消息时，遍历文件描述符数组。当是第0个文件描述符有消息时，表示接收到Socket的**连接请求**，那么就会通过` acceptCommandPeer(abiList)`建立连接，得到ZygoteConnection对象，并加入集合`peers`中以及将ZygoteConnection的文件描述符加入到集合socketFDs中。socketFDs的第0个位置是ZygoteSocket的文件描述符，之后的都是ZygoteConnection的文件描述符。

那么当下一次循环时，会重新创建一个文件描述符数组pollFDs，交给Os.poll()轮询，并将socketFDs中记录的文件描述符传递给pollFDs。那么，当之前建立的连接，有**数据请求**时，就会调用`connection.processOneCommand(this）`处理这个请求。

##### 3.2.1 acceptCommandPeer

```java
    private ZygoteConnection acceptCommandPeer(String abiList) {
        try {                          //调用ServerSocket.accept()得到Socket
            return createNewConnection(mZygoteSocket.accept(), abiList);
        } catch (IOException ex) {
            throw new RuntimeException(
                    "IOException during accept()", ex);
        }
    }
    
    protected ZygoteConnection createNewConnection(LocalSocket socket, String abiList)
            throws IOException {
        return new ZygoteConnection(socket, abiList);
    }

```

acceptCommandPeer方法中其实就是调用了LocalServerSocket.accept()方法得到一个LocalSocket并传入ZygoteConnection的构造方法中。

在ZygoteConnection的构造方方法中，会记录这个Socket以及得到它的输入输出流

```java
    ZygoteConnection(LocalSocket socket, String abiList) throws IOException {
        mSocket = socket;
        this.abiList = abiList;
        
        mSocketOutStream = new DataOutputStream(socket.getOutputStream());
        mSocketReader =
                new BufferedReader(
                        new InputStreamReader(socket.getInputStream()), Zygote.SOCKET_BUFFER_SIZE);
        mSocket.setSoTimeout(CONNECTION_TIMEOUT_MILLIS);
        try {
            peer = mSocket.getPeerCredentials();
        } catch (IOException ex) {
            Log.e(TAG, "Cannot read peer credentials", ex);
            throw ex;
        }

        isEof = false;
    }
```



##### 3.2.2 processOneCommand

```java
    Runnable processOneCommand(ZygoteServer zygoteServer) {
        String[] args;

        try {
            //解析参数
            args = Zygote.readArgumentList(mSocketReader);
        } catch (IOException ex) {
            throw new IllegalStateException("IOException on command socket", ex);
        }

        if (args == null) {
            isEof = true;
            return null;
        }

        int pid;
        FileDescriptor childPipeFd = null;
        FileDescriptor serverPipeFd = null;

        ZygoteArguments parsedArgs = new ZygoteArguments(args);
		
		...
		
        int [] fdsToClose = { -1, -1 };
        FileDescriptor fd = mSocket.getFileDescriptor();

        if (fd != null) {
            fdsToClose[0] = fd.getInt$();
        }
        fd = zygoteServer.getZygoteSocketFileDescriptor();
        if (fd != null) {
            fdsToClose[1] = fd.getInt$();
        }
		//fork子进程
        pid = Zygote.forkAndSpecialize(parsedArgs.mUid, parsedArgs.mGid, parsedArgs.mGids,
                parsedArgs.mRuntimeFlags, rlimits, parsedArgs.mMountExternal, parsedArgs.mSeInfo,
                parsedArgs.mNiceName, fdsToClose, fdsToIgnore, parsedArgs.mStartChildZygote,
                parsedArgs.mInstructionSet, parsedArgs.mAppDataDir, parsedArgs.mIsTopApp);

        try {
            if (pid == 0) {
                //在子线程中
                //设置zygoteServer中的mIsForkChild为true
                zygoteServer.setForkChild();
				//子进程关闭ZygoteServerSocket
                zygoteServer.closeServerSocket();
                IoUtils.closeQuietly(serverPipeFd);
                serverPipeFd = null;
				
                return handleChildProc(parsedArgs, childPipeFd, parsedArgs.mStartChildZygote);
            } else {
                //在父进程中
                IoUtils.closeQuietly(childPipeFd);
                childPipeFd = null;
                handleParentProc(pid, serverPipeFd);
                return null;
            }
        } finally {
            IoUtils.closeQuietly(childPipeFd);
            IoUtils.closeQuietly(serverPipeFd);
        }
    }
```

##### 3.2.3 handleChildProc

```java
    private Runnable handleChildProc(ZygoteArguments parsedArgs,
            FileDescriptor pipeFd, boolean isZygote) {
        closeSocket();

        Zygote.setAppProcessName(parsedArgs, TAG);

        // End of the postFork event.
        Trace.traceEnd(Trace.TRACE_TAG_ACTIVITY_MANAGER);
        if (parsedArgs.mInvokeWith != null) {
            WrapperInit.execApplication(parsedArgs.mInvokeWith,
                    parsedArgs.mNiceName, parsedArgs.mTargetSdkVersion,
                    VMRuntime.getCurrentInstructionSet(),
                    pipeFd, parsedArgs.mRemainingArgs);

            // Should not get here.
            throw new IllegalStateException("WrapperInit.execApplication unexpectedly returned");
        } else {
            if (!isZygote) {
                //子进程会执行到这里，通过反射执行目标类的main()方法
                return ZygoteInit.zygoteInit(parsedArgs.mTargetSdkVersion,
                        parsedArgs.mDisabledCompatChanges,
                        parsedArgs.mRemainingArgs, null /* classLoader */);
            } else {
                return ZygoteInit.childZygoteInit(parsedArgs.mTargetSdkVersion,
                        parsedArgs.mRemainingArgs, null /* classLoader */);
            }
        }
    }
```

