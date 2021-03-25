### Android系统启动流程

#### 1.启动电源以及系统启动

​	当电源按下时，应道芯片代码从ROM中执行，加载引导程序BootLoader到RAM中，然后执行。

#### 2.引导程序BootLoader

​	引导程序BootLoader是在Android操作系统开始运行前的一个小程序，它的主要作用是把系统OS拉起来并运行

#### 3.Linux内核启动

​	当内核启动时，设置缓存、被保护存储器、计划列表、加载驱动。当内核完成系统设置时，首先启动Init进程，执行system\core\rootdir\main.cp中的main函数。在main函数中，会进行一些属性的初始化以及对Init.rc文件进行解析。

```c++
//init.rc
import /init.environ.rc
import /system/etc/init/hw/init.usb.rc
import /init.${ro.hardware}.rc
import /vendor/etc/init/hw/init.${ro.hardware}.rc
import /system/etc/init/hw/init.usb.configfs.rc
import /system/etc/init/hw/init.${ro.zygote}.rc
```

init.rc中导入.rc文件来初始化相应模块。对于android10来说，它提供了多个zygote相关的.rc文件，有init.zygote32.rc、init.zygote64.rc等，我们看到 Init.zygote64.rc中的实现：

```c++
//init.zygote64.rc
service zygote /system/bin/app_process64 -Xzygote /system/bin --zygote --start-system-server
    class main
    priority -20
    user root
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
```

由第一行，我们可以知道它首先会执行`/system/bin/app_process64`，后面的是它的参数。

app_process64的实现是app_main.cpp，它位于`frameworks\base\cmds\app_process`，在该文件夹下面还有它的配置文件Android.bp，根据该配置文件，我们可以知道，app_process的确对应着app_main.cpp

```c++
//Android.bp
cc_binary {
    name: "app_process",
    srcs: ["app_main.cpp"],
	//......
}
```

所以，这里会进入到app_main中的main函数中，并传入参数`-Xzygote /system/bin --zygote --start-system-server`

#### 4. Init进程

上一节我们了解到init进程会进入到app_main.cpp的main函数中，即下面的代码：

```c++
//app_main.cpp
int main(int argc, char* const argv[])
{
    //传到的参数argv为“-Xzygote /system/bin --zygote --start-system-server”
    //......
    AppRuntime runtime(argv[0], computeArgBlockSize(argc, argv));
	
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
            zygote = true;
            //niceName： 64位系统——》zygote64,32位系统——》zygote
            niceName = ZYGOTE_NICE_NAME;
        } else if (strcmp(arg, "--start-system-server") == 0) {
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
    // 定义参数args
	Vector<String8> args;
    if (!className.isEmpty()) {
      
        args.add(application ? String8("application") : String8("tool"));
        runtime.setClassNameAndArgs(className, argc - i, argv + i);

        if (!LOG_NDEBUG) {
          String8 restOfArgs;
          char* const* argv_new = argv + i;
          int argc_new = argc - i;
          for (int k = 0; k < argc_new; ++k) {
            restOfArgs.append("\"");
            restOfArgs.append(argv_new[k]);
            restOfArgs.append("\" ");
          }
          ALOGV("Class name = %s, args = %s", className.string(), restOfArgs.string());
        }
    } else {
        //进入zygote模式
        maybeCreateDalvikCache();

        if (startSystemServer) {
            args.add(String8("start-system-server"));
        }

        char prop[PROP_VALUE_MAX];
        if (property_get(ABI_LIST_PROPERTY, prop, NULL) == 0) {
            LOG_ALWAYS_FATAL("app_process: Unable to determine ABI list from property %s.",
                ABI_LIST_PROPERTY);
            return 11;
        }

        String8 abiFlag("--abi-list=");
        abiFlag.append(prop);
        args.add(abiFlag);

        // In zygote mode, pass all remaining arguments to the zygote
        // main() method.
        for (; i < argc; ++i) {
            args.add(String8(argv[i]));
        }
    }
    if (zygote) {
		//通过AppRunTime.start函数，调用ZygoteInit的main方法，进入Java世界
        runtime.start("com.android.internal.os.ZygoteInit", args, zygote);
    } else if (className) {
        runtime.start("com.android.internal.os.RuntimeInit", args, zygote);
    } else {
        fprintf(stderr, "Error: no class name or --zygote supplied.\n");
        app_usage();
        LOG_ALWAYS_FATAL("app_process: no class name or --zygote supplied.");
    }
}
```

在app_main.cpp的main函数中，会构造一个`AppRuntime`对象，在函数的最后，调用它的`start()`函数，根据我们在init.zygote64.rc中，看到的参数包含有--zygote，zygote为true，所以启动的是ZygoteInit类。

AppRuntime定义在app_main.cpp内，但是它是AndroidRuntime的派生类，AppRuntime主要实现了一些回调函数，并且并没有重写start()函数

```c++
//frameworks\base\core\jni\AndroidRuntime.cpp
void AndroidRuntime::start(const char* className, const Vector<String8>& options, bool zygote)
{
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
    free(slashClassName);
    ALOGD("Shutting down VM\n");
    if (mJavaVM->DetachCurrentThread() != JNI_OK)
        ALOGW("Warning: unable to detach main thread\n");
    if (mJavaVM->DestroyJavaVM() != 0)
        ALOGW("Warning: VM did not shut down cleanly\n");
}
```

####  5.Zygote进程

ZygoteInit.java中的main函数是Zygote进程的入口，在这里会启动Zygote服务，加载资源，并且为fork application处理相关的任务和准备进程

```java
    public static void main(String argv[]) {
        ZygoteServer zygoteServer = null;

        Runnable caller;
        try {
            
            if (!enableLazyPreload) {
                //... 5.1 如果没有开启懒加载，那么就预加载类和资源
                preload(bootTimingsTraceLog);
                //...
            }

            Zygote.initNativeState(isPrimaryZygote);

            ZygoteHooks.stopZygoteNoThreadCreation();
			//5.2 创建ZygoteServer并创建Socket
            zygoteServer = new ZygoteServer(isPrimaryZygote);

            if (startSystemServer) {
                //5.3 fork出SystemServer进程，并执行
                Runnable r = forkSystemServer(abiList, zygoteSocketName, zygoteServer);
                //当r！=null时，表示处于子进程system_server中，运行runnable
                if (r != null) {
                    r.run();
                    return;
                }
            }
            //5.4  能执行到这一步，表示在父进程zygote进程中，进入循环，等待接收消息
            caller = zygoteServer.runSelectLoop(abiList);
        } catch (Throwable ex) {
            Log.e(TAG, "System zygote died with exception", ex);
            throw ex;
        } finally {
            if (zygoteServer != null) {
                zygoteServer.closeServerSocket();
            }
        }

        if (caller != null) {
            caller.run();
        }
    }
```

##### 5.1 preload 预加载

首先看到 第一点preload(bootTimingsTraceLog);

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

##### 5.2 创建ZygoteServer并开启Sokcet

在预加载资源后，ZygoteInit会创建ZygoteServer对象，在ZygoteServer的构造方法中，会创建Socket用于接收消息：

```java
    ZygoteServer(boolean isPrimaryZygote) {
        mUsapPoolEventFD = Zygote.getUsapPoolEventFD();

        if (isPrimaryZygote) {
            //使用ZygoteSocket记录Socket
            mZygoteSocket = Zygote.createManagedSocketFromInitSocket(Zygote.PRIMARY_SOCKET_NAME);
            mUsapPoolSocket =
                    Zygote.createManagedSocketFromInitSocket(
                            Zygote.USAP_POOL_PRIMARY_SOCKET_NAME);
        } else {
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

##### 5.3 创建SystemServer进程

创建完ZygoteServer后，ZygoteInit创建SystemServer进程，我们来看forkSystemServer:

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

`forkSystemServer()`函数准备参数并且fork出新进程，从它的参数可以看出，system_server进程参数信息uid=1000，gid=1000，进程名为system_server。Linux通过fork创建新进程会有两次返回，当pid == 0时，表示处于新进程中，我们可以看到上面的代码中，此时就会继续处理system_server剩余的工作。当pid>0时，pid表示子进程的id，此时处于父进程中，即Zygote进程中，此时返回null。

##### 5.4 开启Zygote进程中消息循环

通过5.3小节的分析，我们知道当返回null时，表示此时在Zygote进程自身中，那么将会执行`caller = zygoteServer.runSelectLoop(abiList);`，进行消息循环

```java
    Runnable runSelectLoop(String abiList) {
        ArrayList<FileDescriptor> socketFDs = new ArrayList<>();
        ArrayList<ZygoteConnection> peers = new ArrayList<>();
		//mZygoteSocket即在5.2节中创建的LocalServerSocket
        socketFDs.add(mZygoteSocket.getFileDescriptor());
        peers.add(null);

        mUsapPoolRefillTriggerTimestamp = INVALID_TIMESTAMP;

        while (true) {
            ...
            int pollReturnValue;
            try {
                //处理轮询状态，当pollFds有事件到来，则往下执行，否则阻塞
                pollReturnValue = Os.poll(pollFDs, pollTimeoutMs);
            } catch (ErrnoException ex) {
                throw new RuntimeException("poll failed", ex);
            }

            if (pollReturnValue == 0) {
          mUsapPoolRefillTriggerTimestamp = INVALID_TIMESTAMP;
                mUsapPoolRefillAction = UsapPoolRefillAction.DELAYED;

            } else {
                boolean usapPoolFDRead = false;
				//
                while (--pollIndex >= 0) {
                    //采用I/O多路复用，没有消息，直接continue，有消息则往下执行
                    if ((pollFDs[pollIndex].revents & POLLIN) == 0) {
                        continue;
                    }
                    if (pollIndex == 0) {
						//pollIndex为0，表示是建立连接
                        ZygoteConnection newPeer = acceptCommandPeer(abiList);
                        peers.add(newPeer);
                        //添加到socketFDs中
                        socketFDs.add(newPeer.getFileDescriptor());

                    } else if (pollIndex < usapPoolEventFDIndex) {
                       	//大于0小于usapPoolEventFDIndex是进行数据请求
                        try {
                            ZygoteConnection connection = peers.get(pollIndex);
                            //在processOneCommand中创建新进程
                            final Runnable command = connection.processOneCommand(this);
                            if (mIsForkChild) {
                                //处于子进程中
                                if (command == null) {
                                    throw new IllegalStateException("command == null");
                                }
                                return command;
                            } else {
                                //处于父进程中
                                if (command != null) {
                                    throw new IllegalStateException("command != null");
                                }
                                if (connection.isClosedByPeer()) {
                                    connection.closeSocket();
                                    peers.remove(pollIndex);
                                    socketFDs.remove(pollIndex);
                                }
                            }
                        } catch (Exception e) {
	
                          ....
    }
```

#### 6.SystemServer进程

```java
    public static void main(String[] args) {
        new SystemServer().run();
    }

```

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

在startOtherServices开启全部的服务后，在该方法的末尾，就会调用ActivityManagerService的systemready方法

#### 7.启动Launcher页

```java
 //ActivityManagerService
public void systemReady(final Runnable goingCallback, TimingsTraceLog traceLog) {

           	...
            if (bootingSystemUser) {
                //开启系统主页
                mAtmInternal.startHomeOnAllDisplays(currentUserId, "systemReady");
            }
  }

```



```java
//ActivityTaskManagerService
        @Override
        public boolean startHomeOnAllDisplays(int userId, String reason) {
            synchronized (mGlobalLock) {
                return mRootActivityContainer.startHomeOnAllDisplays(userId, reason);
            }
        }

```

```java
//RootActivityContainer   
boolean startHomeOnAllDisplays(int userId, String reason) {
        boolean homeStarted = false;
        for (int i = mActivityDisplays.size() - 1; i >= 0; i--) {
            final int displayId = mActivityDisplays.get(i).mDisplayId;
            homeStarted |= startHomeOnDisplay(userId, reason, displayId);
        }
        return homeStarted;
    }

```

```java
    boolean startHomeOnDisplay(int userId, String reason, int displayId, boolean allowInstrumenting,
            boolean fromHomeKey) {
        // Fallback to top focused display if the displayId is invalid.
        if (displayId == INVALID_DISPLAY) {
            displayId = getTopDisplayFocusedStack().mDisplayId;
        }

        Intent homeIntent = null;
        ActivityInfo aInfo = null;
        if (displayId == DEFAULT_DISPLAY) {
            //启动Home Activity的intent
            homeIntent = mService.getHomeIntent();
            aInfo = resolveHomeActivity(userId, homeIntent);
        } else if (shouldPlaceSecondaryHomeOnDisplay(displayId)) {
            Pair<ActivityInfo, Intent> info = resolveSecondaryHomeActivity(userId, displayId);
            aInfo = info.first;
            homeIntent = info.second;
        }
		...
        //启动Home Activity
        mService.getActivityStartController().startHomeActivity(homeIntent, aInfo, myReason,
                displayId);
     
```



```java

    void startHomeActivity(Intent intent, ActivityInfo aInfo, String reason, int displayId) {
        final ActivityOptions options = ActivityOptions.makeBasic();
        options.setLaunchWindowingMode(WINDOWING_MODE_FULLSCREEN);
        if (!ActivityRecord.isResolverActivity(aInfo.name)) {
            // The resolver activity shouldn't be put in home stack because when the foreground is
            // standard type activity, the resolver activity should be put on the top of current
            // foreground instead of bring home stack to front.
            options.setLaunchActivityType(ACTIVITY_TYPE_HOME);
        }
        options.setLaunchDisplayId(displayId);
        mLastHomeActivityStartResult = obtainStarter(intent, "startHomeActivity: " + reason)
                .setOutActivity(tmpOutRecord)
                .setCallingUid(0)
                .setActivityInfo(aInfo)
                .setActivityOptions(options.toBundle())
                .execute();
        mLastHomeActivityStartRecord = tmpOutRecord[0];
        final ActivityDisplay display =
                mService.mRootActivityContainer.getActivityDisplay(displayId);
        final ActivityStack homeStack = display != null ? display.getHomeStack() : null;
        if (homeStack != null && homeStack.mInResumeTopActivity) {
            mSupervisor.scheduleResumeTopActivities();
        }
    }

```

