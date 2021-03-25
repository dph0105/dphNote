SystemServer承载着Android framework的核心服务，它运行在system_server的进程中。在Android系统启动流程中讲到Zygote的启动过程中会通过fork()开启system_server进程。

##### 1. ZygoteInit.main():

```java
   public static void main(String argv[]) {
   	   ...
            if (startSystemServer) {
                Runnable r = forkSystemServer(abiList, zygoteSocketName, zygoteServer);
                if (r != null) {
                    r.run();
                    return;
                }
            }
       ...
    }
```

ZygoteInit的main方法中，如果传入的参数包含"start-system-server"，则会调用

`forkSystemServer`函数：

##### 2.forkSystemServer

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
			//fork 子进程，该进程是system_server
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
            //子进程不需要开启ServerSocket，关闭从zygote进程中复制来的Socket
            zygoteServer.closeServerSocket();
            //完成system_server进程中的工作
            return handleSystemServerProcess(parsedArgs);
        }
        return null;
    }
```

##### 3. handleSystemServerProcess

函数handleSystemServerProcess()以及运行在system_server进程中，它用于完成剩余的system_server中的工作：

```java
    private static Runnable handleSystemServerProcess(ZygoteArguments parsedArgs) {
        Os.umask(S_IRWXG | S_IRWXO);
		//设置当前进程名为system_server
        if (parsedArgs.mNiceName != null) {
            Process.setArgV0(parsedArgs.mNiceName);
        }

        final String systemServerClasspath = Os.getenv("SYSTEMSERVERCLASSPATH");
        if (systemServerClasspath != null) {
            performSystemServerDexOpt(systemServerClasspath);
            if (shouldProfileSystemServer() && (Build.IS_USERDEBUG || Build.IS_ENG)) {
                try {
                    Log.d(TAG, "Preparing system server profile");
                    prepareSystemServerProfile(systemServerClasspath);
                } catch (Exception e) {
                    Log.wtf(TAG, "Failed to set up system server profile", e);
                }
            }
        }

        if (parsedArgs.mInvokeWith != null) {
            String[] args = parsedArgs.mRemainingArgs;
            if (systemServerClasspath != null) {
                String[] amendedArgs = new String[args.length + 2];
                amendedArgs[0] = "-cp";
                amendedArgs[1] = systemServerClasspath;
                System.arraycopy(args, 0, amendedArgs, 2, args.length);
                args = amendedArgs;
            }
			
            WrapperInit.execApplication(parsedArgs.mInvokeWith,
                    parsedArgs.mNiceName, parsedArgs.mTargetSdkVersion,
                    VMRuntime.getCurrentInstructionSet(), null, args);

            throw new IllegalStateException("Unexpected return from WrapperInit.execApplication");
        } else {
            //根据上面之前方法的参数，mInvokeWith==null，所以进入else
            ClassLoader cl = null;
            if (systemServerClasspath != null) {
                //创建类加载器，并赋予当前线程，Java中每个线程都有自己的类加载器
                cl = createPathClassLoader(systemServerClasspath, parsedArgs.mTargetSdkVersion);
                Thread.currentThread().setContextClassLoader(cl);
            }
            return ZygoteInit.zygoteInit(parsedArgs.mTargetSdkVersion,
                    parsedArgs.mDisabledCompatChanges,
                    parsedArgs.mRemainingArgs, cl);
        }

        /* should never reach here */
    }
```

##### 4. zygoteInit

```java
    public static final Runnable zygoteInit(int targetSdkVersion, long[] disabledCompatChanges,
            String[] argv, ClassLoader classLoader) {
        if (RuntimeInit.DEBUG) {
            Slog.d(RuntimeInit.TAG, "RuntimeInit: Starting application from zygote");
        }

        Trace.traceBegin(Trace.TRACE_TAG_ACTIVITY_MANAGER, "ZygoteInit");
        RuntimeInit.redirectLogStreams();
        //4.1 通用的一些初始化
        RuntimeInit.commonInit();
        //4.2 zygote初始化
        ZygoteInit.nativeZygoteInit();
        //4.3 应用初始化，主要就是通过传入的类名，找到类，通过反射，执行main()
        return RuntimeInit.applicationInit(targetSdkVersion, disabledCompatChanges, argv,
                classLoader);
    }
```

##### 4.1 commonInit

```java
 protected static final void commonInit() {
        if (DEBUG) Slog.d(TAG, "Entered RuntimeInit!");
		//设置默认的未捕捉到的异常的处理方法
        LoggingHandler loggingHandler = new LoggingHandler();
        RuntimeHooks.setUncaughtExceptionPreHandler(loggingHandler);
        Thread.setDefaultUncaughtExceptionHandler(new KillApplicationHandler(loggingHandler));

        //设置时区
        RuntimeHooks.setTimeZoneIdSupplier(() -> SystemProperties.get("persist.sys.timezone"));

        //重置Log配置
        LogManager.getLogManager().reset();
        new AndroidConfig();

        //设置默认的Http User-agent格式
        String userAgent = getDefaultUserAgent();
        System.setProperty("http.agent", userAgent);

        //设置Socket的Tagger，用于流量统计
        NetworkManagementSocketTagger.install();
		
        String trace = SystemProperties.get("ro.kernel.android.tracing");
        if (trace.equals("1")) {
            Slog.i(TAG, "NOTE: emulator trace profiling enabled");
            Debug.enableEmulatorTraceOutput();
        }

        initialized = true;
    }
```

##### 4.2  nativeZygoteInit

nativeZygoteInit在AndroidRuntime.cpp中

```c++
static void com_android_internal_os_ZygoteInit_nativeZygoteInit(JNIEnv* env, jobject clazz)
{
    gCurRuntime->onZygoteInit();
}

```

它调用了AndroidRuntime.cpp的onZygoteInit()回调函数，该回调函数在app_main.cpp中的AppRuntime中实现：

```c++
//app_main.cpp/AppRuntime
virtual void onZygoteInit()
    {
        sp<ProcessState> proc = ProcessState::self();
        ALOGV("App process: starting thread pool.\n");
        proc->startThreadPool();//开启binder线程
    }
```

ProcessState的主要工作是调用open(）打开/dev/binder驱动设备，再利用mmap()映射内核的地址空间，将Binder驱动的fd复制ProcessState对象中的变量mDriverFD，用于交互操作。ProcessState::self()是单例模式，得到一个ProcessState的对象，通过startThreadPool()创建binder线程。

##### 4.3 applicationInit

```java
    protected static Runnable applicationInit(int targetSdkVersion, long[] disabledCompatChanges,
            String[] argv, ClassLoader classLoader) {
		//true代表应用程序退出时不调用AppRuntime.onExit()否则会调用
        nativeSetExitWithoutCleanup(true);

        VMRuntime.getRuntime().setTargetSdkVersion(targetSdkVersion);
        VMRuntime.getRuntime().setDisabledCompatChanges(disabledCompatChanges);
		//解析参数
        final Arguments args = new Arguments(argv);
        Trace.traceEnd(Trace.TRACE_TAG_ACTIVITY_MANAGER);
		//寻找类的main方法并执行，
        return findStaticMain(args.startClass, args.startArgs, classLoader);
    }
```

在applicationInit中，根据forkSystemServer()中的参数，我们可以知道这里的args.startClass="com.android.server.SystemServer"

```
    protected static Runnable findStaticMain(String className, String[] argv,
            ClassLoader classLoader) {
        Class<?> cl;
        try {
            cl = Class.forName(className, true, classLoader);
        } catch (ClassNotFoundException ex) {
            throw new RuntimeException(
                    "Missing class when invoking static main " + className,
                    ex);
        }
        Method m;
        try {
            m = cl.getMethod("main", new Class[] { String[].class });
        } catch (NoSuchMethodException ex) {
            throw new RuntimeException(
                    "Missing static main on " + className, ex);
        } catch (SecurityException ex) {
            throw new RuntimeException(
                    "Problem getting static main on " + className, ex);
        }
        int modifiers = m.getModifiers();
        if (! (Modifier.isStatic(modifiers) && Modifier.isPublic(modifiers))) {
            throw new RuntimeException(
                    "Main method is not public and static on " + className);
        }
        //找到SystemServer的main方法后，传入MethodAndArgsCaller得到一个Runnable并返回
        return new MethodAndArgsCaller(m, argv);
    }
```

#### MethodAndArgsCaller

```
    static class MethodAndArgsCaller implements Runnable {
        ...
        public void run() {
            try {
               //
                mMethod.invoke(null, new Object[] { mArgs });
            } catch (IllegalAccessException ex) {
                throw new RuntimeException(ex);
            } catch (InvocationTargetException ex) {
                ...
                throw new RuntimeException(ex);
            }
        }
    }
}
```

所以我们知道，forkSystemServer()最终回在system_server进程中返回MethodAndArgsCaller对象，然后调用它的run()方法，在run方法中，通过反射调用SystemServer的main()方法。

#### SystemServer

##### 1.SystemServer.main

```java
    public static void main(String[] args) {
        new SystemServer().run();
    }

```

##### 2. SystemServer.run

```java
    private void run() {
   			...
   			//变更虚拟机的库文件，
            SystemProperties.set("persist.sys.dalvik.vm.lib.2", VMRuntime.getRuntime().vmLibrary());

            //清楚vm内存增长上限，由于启动过程需要较多的虚拟机内存空间
            VMRuntime.getRuntime().clearGrowthLimit();

            // 部分设备依赖于运行时生成指纹，因此在引导前就定义好了
            Build.ensureFingerprintProperty();

			//访问环境变量需要明确指定用户
            Environment.setUserRequired(true);

            BaseBundle.setShouldDefuse(true);

            // 在SystemServer中，当发生异常，要写入堆栈跟踪
            Parcel.setStackTraceParceling(true);

            //确保系统对Binder的调用都以前台优先级运行
            BinderInternal.disableBackgroundScheduling(true);

            //设置Binder线程最大数量
            BinderInternal.setMaxThreads(sMaxBinderThreads);

            // 主线程Looper就在当前线程运行
            android.os.Process.setThreadPriority(
                    android.os.Process.THREAD_PRIORITY_FOREGROUND);
            android.os.Process.setCanSelfBackground(false);
            Looper.prepareMainLooper();
            Looper.getMainLooper().setSlowLogThresholdMs(
                    SLOW_DISPATCH_THRESHOLD_MS, SLOW_DELIVERY_THRESHOLD_MS);

            //加载android_server.so库，源码在frameworks/base/services
            System.loadLibrary("android_servers");

            // Debug builds - allow heap profiling.
            if (Build.IS_DEBUGGABLE) {
                initZygoteChildHeapProfiling();
            }

            // Debug builds - spawn a thread to monitor for fd leaks.
            if (Build.IS_DEBUGGABLE) {
                spawnFdLeakCheckThread();
            }

            // 检查上次关机过程是否失败
            performPendingShutdown();

            // 初始化系统上下文
            createSystemContext();

            // 创建SystemServiceManager
            mSystemServiceManager = new SystemServiceManager(mSystemContext);
            mSystemServiceManager.setStartInfo(mRuntimeRestart,
                    mRuntimeStartElapsedTime, mRuntimeStartUptime);
            //将mSystemServiceManager添加到本地服务的集合中
            LocalServices.addService(SystemServiceManager.class, mSystemServiceManager);
            //准备好一个线程池，用于初始化任务
            SystemServerInitThreadPool.get();
            // Attach JVMTI agent if this is a debuggable build and the system property is set.
           ...
        //启动系统各种服务
        try {
            traceBeginAndSlog("StartServices");
            startBootstrapServices();
            startCoreServices();
            startOtherServices();
            //关闭线程池
            SystemServerInitThreadPool.shutdown();
        } catch (Throwable ex) {
            Slog.e("System", "******************************************");
            Slog.e("System", "************ Failure starting system services", ex);
            throw ex;
        } finally {
            traceEnd();
        }
		...

        //开启Loop循环
        Looper.loop();
        throw new RuntimeException("Main thread loop unexpectedly exited");
    }

```

SystemServer.main()函数创建了SystemServer对象，并执行它的run()函数。在run()方法中，首先是一系列的配置，然后通过createSystemContext()方法初始化系统上下文

###### 2.1 createSystemContext

```java
    private void createSystemContext() {
        //创建system_server进程的上下文信息
        ActivityThread activityThread = ActivityThread.systemMain();
        mSystemContext = activityThread.getSystemContext();
        //设置主题
        mSystemContext.setTheme(DEFAULT_SYSTEM_THEME);
        final Context systemUiContext = activityThread.getSystemUiContext();
        systemUiContext.setTheme(DEFAULT_SYSTEM_THEME);
    }

```

###### 2.2 创建SystemServiceManager

创建好system_server的上下文信息后，SystemServer就会创建SystemServiceManager对象，用于启动、管理一系列的系统服务

###### 2.3 启动系统服务

做好一系列的准备之后，SystemServer就会通过`startBootstrapServices()`、`startCoreServices()`、` startOtherServices()` 这三个方法启动系统服务，这里我就不展示 了，我们直接看到startOtherServices()的末尾部分

```
  private void startOtherServices() {
        ...
   		mActivityManagerService.systemReady(() -> {
            ...
        }, BOOT_TIMINGS_TRACE_LOG);
    }
```

当所有服务都启动完成，SystemServer就会调用ActivityManagerService的systemReady()方法，在SystemServer的systemReady方法中，会SystemServer启动的各个Service的onSystemReady()函数，

