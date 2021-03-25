一般情况下，我们通过在一个Activity中，调用startActivity(Intent)的方式启动另一个Activity，就如下面的例子，MainActivity启动了Main2Activity。本文就来分析这一过程的实现：

```kotlin
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        setContentView(R.layout.activity_main)

        btn.setOnClickListener {
            startActivity(Intent(this,Main2Activity::class.java))
        }
    }
}
```

## 1. App进程发起启动Activity的请求

### 1.1 Activity发起启动请求

进入Activity中看到`startActivity(Intent intent)`方法

```java
//frameworks\base\core\java\android\app\Activity.java
	public void startActivity(Intent intent) {
        this.startActivity(intent, null);
    }

    public void startActivity(Intent intent, @Nullable Bundle options) {
        if (options != null) {
            startActivityForResult(intent, -1, options);
        } else {
            startActivityForResult(intent, -1);
        }
    }

    public void startActivityForResult(@RequiresPermission Intent intent, int requestCode) {
        startActivityForResult(intent, requestCode, null);
    }

```

可以看出，最终都是执行`startActivityForResult(Intent intent, int requestCode,Bundle options)`

```java
//frameworks\base\core\java\android\app\Activity.java
	public void startActivityForResult(@RequiresPermission Intent intent, int requestCode,
            @Nullable Bundle options) {
        if (mParent == null) {
            options = transferSpringboardActivityOptions(options);
            //调用Instrumentation.execStartActivity
            Instrumentation.ActivityResult ar =
                mInstrumentation.execStartActivity(
                    this, mMainThread.getApplicationThread(), mToken, this,
                    intent, requestCode, options);
            if (ar != null) {
                mMainThread.sendActivityResult(
                    mToken, mEmbeddedID, requestCode, ar.getResultCode(),
                    ar.getResultData());
            }
            if (requestCode >= 0) {
                mStartedActivity = true;
            }
            cancelInputsAndStartExitTransition(options);
        } else {
            ...
        }
    }
```

这里的mParent指Activity的父Activity，在没有Fragment之前，一般用Activity包裹Activity的方式，实现现在的一些四个分页的界面。目前基本是使用Fragment，当然不管是有没有mParent，最终也是调用`Instrumentation#execStartActivity`启动Activity

### 1.2 通过Instrumentation执行请求

```java
//frameworks\base\core\java\android\app\Instrumentation.java    
	public ActivityResult execStartActivity(
            Context who, IBinder contextThread, IBinder token, Activity target,
            Intent intent, int requestCode, Bundle options) {
        IApplicationThread whoThread = (IApplicationThread) contextThread;
        Uri referrer = target != null ? target.onProvideReferrer() : null;
        if (referrer != null) {
            intent.putExtra(Intent.EXTRA_REFERRER, referrer);
        }
        ...
        try {
            intent.migrateExtraStreamToClipData();
            intent.prepareToLeaveProcess(who);
            //获取ActivityTaskManagerService调用它的startActivity方法
            int result = ActivityTaskManager.getService()
                .startActivity(whoThread, who.getBasePackageName(), intent,
                        intent.resolveTypeIfNeeded(who.getContentResolver()),
                        token, target != null ? target.mEmbeddedID : null,
                        requestCode, 0, null, options);
            //检查Activity是否启动成功
            checkStartActivityResult(result, intent);
        } catch (RemoteException e) {
            throw new RuntimeException("Failure from system", e);
        }
        return null;
    }
```

`Instrumentation#execStartActivity`实际上是将启动Activity的请求传递给了系统服务**ActivityTaskManagerService**。这里首先通过aidl的方式得到`ActivityTaskManagerService`的Binder代理对象，然后远程调用ATMS的`startActivity`

```java
    public static IActivityTaskManager getService() {
        return IActivityTaskManagerSingleton.get();
    }

    private static final Singleton<IActivityTaskManager> IActivityTaskManagerSingleton =
            new Singleton<IActivityTaskManager>() {
                @Override
                protected IActivityTaskManager create() {
                    final IBinder b = ServiceManager.getService(Context.ACTIVITY_TASK_SERVICE);
                    return IActivityTaskManager.Stub.asInterface(b);
                }
            };
```

## 2. ATMS收到请求，开始处理

#### 2.0 介绍一下几个基本的类

![](https://img-blog.csdnimg.cn/20191206221732510.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L2ExNTExMDEwMzExNw==,size_16,color_FFFFFF,t_70)

- ActivityRecord 是 Activity 管理的最小单位，它对应着一个用户界面，是 AMS调度 Activity的基本单位。
- TaskRecord 是一个栈式管理结构，每一个 TaskRecord 都可能存在一个或多个 ActivityRecord，栈顶的ActivityRecord 表示当前可见的界面。启动 Activity时，需要找到 Activity的宿主任务，如果不存在，则需要新建一个，也就是说所有的 ActivityRecord都必须有宿主。
- ActivityStack 是一个栈式管理结构，每一个 ActivityStack 都可能存在一个或多个 TaskRecord，栈顶的TaskRecord 表示当前可见的任务。
- ActivityStackSupervisor 管理着多个 ActivityStack，但当前只会有一个获取焦点 (Focused)的 ActivityStack。
- ProcessRecord 记录着属于一个进程的所有 ActivityRecord，运行在不同 TaskRecord 中的 ActivityRecord 可能是属于同一个 ProcessRecord。AMS采用 ProcessRecord 这个数据结构来维护进程运行时的状态信息，当创建系统进程 (system_process) 或应用进程的时候，就会通过 AMS初始化一个 ProcessRecord。

### 2.1 ATMS开始启动Activity

在app进程中，通过Binder远程调用了`ActivityTaskManagerService#startActivity`

```java
//frameworks\base\services\core\java\com\android\server\wm\ActivityTaskManagerService.java
	@Override
    public final int startActivity(IApplicationThread caller, String callingPackage,
            Intent intent, String resolvedType, IBinder resultTo, String resultWho, int requestCode,int startFlags, ProfilerInfo profilerInfo, Bundle bOptions) {
        
        return startActivityAsUser(caller, callingPackage, intent, resolvedType, resultTo,resultWho, requestCode, startFlags, profilerInfo, bOptions,UserHandle.getCallingUserId());
    }

```

startActivity方法接受10个参数：

- caller：当前应用的ApplicationThread对象
- callingPackage: 调用当前ContextImpl.getBasePackageName(),获取当前Activity所在包名
- intent: 这便是启动Activity时,传递过来的参数
- resolvedType: null
- resultTo: 当前的Activity的mToken参数，是一个IBinder对象
- resultWho: null
- requestCode = -1
- startFlags = 0
- profilerInfo = null
- options = null

### 2.2 ATMS通过ActivityStarter执行请求

```java
//frameworks\base\services\core\java\com\android\server\wm\ActivityTaskManagerService.java    
	int startActivityAsUser(IApplicationThread caller, String callingPackage,
Intent intent, String resolvedType, IBinder resultTo, String resultWho, int requestCode,int startFlags, ProfilerInfo profilerInfo, Bundle bOptions, int userId,
boolean validateIncomingUser) {
        enforceNotIsolatedCaller("startActivityAsUser");

        userId = getActivityStartController().checkTargetUser(userId, validateIncomingUser,
                Binder.getCallingPid(), Binder.getCallingUid(), "startActivityAsUser");

        // 获取ActivityStartContoller，然后获取ActivityStarter,设置参数最终调用execute
        return getActivityStartController().obtainStarter(intent, "startActivityAsUser")
                .setCaller(caller)
                .setCallingPackage(callingPackage)
                .setResolvedType(resolvedType)
                .setResultTo(resultTo)
                .setResultWho(resultWho)
                .setRequestCode(requestCode)
                .setStartFlags(startFlags)
                .setProfilerInfo(profilerInfo)
                .setActivityOptions(bOptions)
                .setMayWait(userId)
                .execute();

    }
```

startActivityAsUser中，通过getActivityStartController得到的是ActivityStartContoller。通过ActivityStartController#obtainStarter得到的是ActivityStarter。最终会调用ActivityStarter#execute

#### 2.2.1 ActivityStartController介绍

ActivityStartController是启动Activity的控制器，它的主要目标是接受启动Activity的请求，并将一些列的Activity准备好交给ActivityStarter处理，它还负责处理围绕Activity启动的逻辑，但不一定会影响Activity启动。例如包括电源提示管理、通过Activity等待列表处理 以及记录home Activity启动。

ActivityStartController是ActivityTaskManagerService的成员函数，它是在ActivityTaskManagerService初始化方法initialize函数中被创建，而initialize函数是在ActivityManagerService的构造方法中被调用的。

```java
 //frameworks\base\services\core\java\com\android\server\wm\ActivityTaskManagerService.java
	public void initialize(IntentFirewall intentFirewall, PendingIntentController intentController,
            Looper looper) {
        ...
        mActivityStartController = new ActivityStartController(this);
        ...
    }

```

#### 2.2.2 ActivityStarter介绍

**ActivityStartController只是做好围绕启动Activity的工作，而真正的启动Activity的任务交给了ActivityStarter。**ActivityStartController通过obtainStarter得到ActivityStarter：

```java
//frameworks\base\services\core\java\com\android\server\wm\ActivityStartController.java
	ActivityStarter obtainStarter(Intent intent, String reason) {
        return mFactory.obtain().setIntent(intent).setReason(reason);
    }
```

这里通过ActivityStarter的工厂类DefaultFactory得到ActivityStarter对象：

```java
 //frameworks\base\services\core\java\com\android\server\wm\ActivityStarter.java
		@Override
        public ActivityStarter obtain() {
            ActivityStarter starter = mStarterPool.acquire();
            if (starter == null) {
                starter = new ActivityStarter(mController, mService, mSupervisor, mInterceptor);
            }
            return starter;
        }

```

### 2.3 ActivityStarter开始执行请求

得到ActivityStarter对象并且设置一系列参数后，最终调用了ActivityStarter的execute()方法：

```java
//frameworks\base\services\core\java\com\android\server\wm\ActivityStarter.java
	int execute() {
        try {
            if (mRequest.mayWait) {
                //进入该分支
                return startActivityMayWait(mRequest.caller, mRequest.callingUid,
                        mRequest.callingPackage, mRequest.realCallingPid, mRequest.realCallingUid,
                        mRequest.intent, mRequest.resolvedType,
                        mRequest.voiceSession, mRequest.voiceInteractor, mRequest.resultTo,
                        mRequest.resultWho, mRequest.requestCode, mRequest.startFlags,
                        mRequest.profilerInfo, mRequest.waitResult, mRequest.globalConfig,
                        mRequest.activityOptions, mRequest.ignoreTargetSecurity, mRequest.userId,
                        mRequest.inTask, mRequest.reason,
                        mRequest.allowPendingRemoteAnimationRegistryLookup,
                        mRequest.originatingPendingIntent, mRequest.allowBackgroundActivityStart);
            } else {
                ...
            }
        } finally {
            onExecutionComplete();
        }
    }

```

#### 2.3.1 ActivityStarter解析Intent信息

由于之前在startActivityAsUser函数中，调用了ActivityStarter的setMayWait()会设置mRequest.mayWait为true，所以这里会进入startActivityMayWait函数中

```java
//frameworks\base\services\core\java\com\android\server\wm\ActivityStarter.java    
	private int startActivityMayWait(IApplicationThread caller, int callingUid,
            String callingPackage, int requestRealCallingPid, int requestRealCallingUid,
            Intent intent, String resolvedType, IVoiceInteractionSession voiceSession,
            IVoiceInteractor voiceInteractor, IBinder resultTo, String resultWho, int requestCode,
            int startFlags, ProfilerInfo profilerInfo, WaitResult outResult,
            Configuration globalConfig, SafeActivityOptions options, boolean ignoreTargetSecurity,
            int userId, TaskRecord inTask, String reason,
            boolean allowPendingRemoteAnimationRegistryLookup,
            PendingIntentRecord originatingPendingIntent, boolean allowBackgroundActivityStart) {
        ...
            
        final Intent ephemeralIntent = new Intent(intent);
        // 创建新的Intent对象，即便intent被修改也不受影响
        intent = new Intent(intent);
		...
		//通过ActivityStackSupervisor解析Intent的信息
        //ResoleveInfo中包含要启动的Activity信息，即ActivityInfo对象
        ResolveInfo rInfo = mSupervisor.resolveIntent(intent, resolvedType, userId,
                0 /* matchFlags */,
                        computeResolveFilterUid(
                                callingUid, realCallingUid, mRequest.filterCallingUid));
        ...
        //收集Intent所指向的Activity信息
        //如果rInfo不为null则aInfo则直接取rInfo.activityInfo
        ActivityInfo aInfo = mSupervisor.resolveActivity(intent, rInfo, startFlags, profilerInfo);

        synchronized (mService.mGlobalLock) {
            //得到当前显示的Activity所属的ActivityStack
            final ActivityStack stack = mRootActivityContainer.getTopDisplayFocusedStack();
            stack.mConfigWillChange = globalConfig != null
                    && mService.getGlobalConfiguration().diff(globalConfig) != 0;
            if (DEBUG_CONFIGURATION) Slog.v(TAG_CONFIGURATION,
                    "Starting activity when config will change = " + stack.mConfigWillChange);

            final long origId = Binder.clearCallingIdentity();

            ...

            final ActivityRecord[] outRecord = new ActivityRecord[1];
            //调用startActivity
            int res = startActivity(caller, intent, ephemeralIntent, resolvedType, aInfo, rInfo,
                    voiceSession, voiceInteractor, resultTo, resultWho, requestCode, callingPid,
                    callingUid, callingPackage, realCallingPid, realCallingUid, startFlags, options,
                    ignoreTargetSecurity, componentSpecified, outRecord, inTask, reason,
                    allowPendingRemoteAnimationRegistryLookup, originatingPendingIntent,
                    allowBackgroundActivityStart);

            Binder.restoreCallingIdentity(origId);

            ...
            return res;
        }
    }


```

##### 2.3.1.1 mSupervisor.resolveIntent

startActivityMayWait中会调用mSupervisor.resolveIntent，其目的是找到相应的组件，mSupervisor的类型是ActivityStackSupervisor，在Android10之前，主要是由这个类启动Activity，Android10之后，将逐渐将它的功能分离给相应的类，例如将与层次结构相关的内容移动到RootWindowContainer。当然，目前这个类还没有一下子被取代。看它的resolveIntent：

```java
//frameworks\base\services\core\java\com\android\server\wm\ActivityStackSupervisor.java    
	ResolveInfo resolveIntent(Intent intent, String resolvedType, int userId, int flags,
            int filterCallingUid) {
        try {
            ...
            final long token = Binder.clearCallingIdentity();
            try {
                //获取PackageManagerInternalImpl对象，调用它的resolveIntent
                return mService.getPackageManagerInternalLocked().resolveIntent(
                        intent, resolvedType, modifiedFlags, userId, true, filterCallingUid);
            } finally {
                Binder.restoreCallingIdentity(token);
            }
        } finally {
            Trace.traceEnd(TRACE_TAG_ACTIVITY_MANAGER);
        }
    }

```

`resolveIntent`会获取PackageManagerService的本地服务类**PackageManagerInternalImpl**，它是在PackageManagerService构造时，创建并加入到LocalServices中的，它是PackageManagerService的内部类

```java
//frameworks\base\services\core\java\com\android\server\pm\PackageManagerService.java
	@Override
    public ResolveInfo resolveIntent(Intent intent, String resolvedType,
            int flags, int userId) {
        return resolveIntentInternal(intent, resolvedType, flags, userId, false,
                Binder.getCallingUid());
    }

    private ResolveInfo resolveIntentInternal(Intent intent, String resolvedType,
            int flags, int userId, boolean resolveForStart, int filterCallingUid) {
        try {
            Trace.traceBegin(TRACE_TAG_PACKAGE_MANAGER, "resolveIntent");

            if (!sUserManager.exists(userId)) return null;
            final int callingUid = Binder.getCallingUid();
            flags = updateFlagsForResolve(flags, userId, intent, filterCallingUid, resolveForStart);
            mPermissionManager.enforceCrossUserPermission(callingUid, userId,
                    false /*requireFullPermission*/, false /*checkShell*/, "resolve intent");

            Trace.traceBegin(TRACE_TAG_PACKAGE_MANAGER, "queryIntentActivities");
            //查询符合的组件
            final List<ResolveInfo> query = queryIntentActivitiesInternal(intent, resolvedType,
                    flags, filterCallingUid, userId, resolveForStart, true /*allowDynamicSplits*/);
            Trace.traceEnd(TRACE_TAG_PACKAGE_MANAGER);
			//选择最佳的组件
            final ResolveInfo bestChoice =
                    chooseBestActivity(intent, resolvedType, flags, query, userId);
            return bestChoice;
        } finally {
            Trace.traceEnd(TRACE_TAG_PACKAGE_MANAGER);
        }
    }

```

#### 2.3.2 创建要启动的Activity的ActivityRecord

startActivityMayWait最终会调用一系列的startActivity方法

```java
    private int startActivity(IApplicationThread caller, Intent intent, Intent ephemeralIntent,
            String resolvedType, ActivityInfo aInfo, ResolveInfo rInfo,
            IVoiceInteractionSession voiceSession, IVoiceInteractor voiceInteractor,
            IBinder resultTo, String resultWho, int requestCode, int callingPid, int callingUid,String callingPackage, int realCallingPid, int realCallingUid, int startFlags,
            SafeActivityOptions options, boolean ignoreTargetSecurity, boolean componentSpecified,ActivityRecord[] outActivity, TaskRecord inTask, String reason,boolean allowPendingRemoteAnimationRegistryLookup,PendingIntentRecord originatingPendingIntent, boolean allowBackgroundActivityStart) {
        ...
        mLastStartActivityResult = startActivity(caller, intent, ephemeralIntent, resolvedType,aInfo, rInfo, voiceSession, voiceInteractor, resultTo, resultWho, requestCode,callingPid, callingUid, callingPackage, realCallingPid, realCallingUid, startFlags,options, ignoreTargetSecurity, componentSpecified, mLastStartActivityRecord,
inTask, allowPendingRemoteAnimationRegistryLookup, originatingPendingIntent,allowBackgroundActivityStart);

        if (outActivity != null) {
            outActivity[0] = mLastStartActivityRecord[0];
        }
        return getExternalResult(mLastStartActivityResult);
    }

```



```java
    private int startActivity(IApplicationThread caller, Intent intent, Intent ephemeralIntent,
            String resolvedType, ActivityInfo aInfo, ResolveInfo rInfo,
            IVoiceInteractionSession voiceSession, IVoiceInteractor voiceInteractor,
            IBinder resultTo, String resultWho, int requestCode, int callingPid, int callingUid,
            String callingPackage, int realCallingPid, int realCallingUid, int startFlags,
            SafeActivityOptions options,
            boolean ignoreTargetSecurity, boolean componentSpecified, ActivityRecord[] outActivity,
            TaskRecord inTask, boolean allowPendingRemoteAnimationRegistryLookup,
            PendingIntentRecord originatingPendingIntent, boolean allowBackgroundActivityStart) {
        mSupervisor.getActivityMetricsLogger().notifyActivityLaunching(intent);
        int err = ActivityManager.START_SUCCESS;
        // Pull the optional Ephemeral Installer-only bundle out of the options early.
        final Bundle verificationBundle
                = options != null ? options.popAppVerificationBundle() : null;
		//得到进程控制器
        WindowProcessController callerApp = null;
        if (caller != null) {
            callerApp = mService.getProcessController(caller);
            if (callerApp != null) {
                callingPid = callerApp.getPid();
                callingUid = callerApp.mInfo.uid;
            } else {
                Slog.w(TAG, "Unable to find app for caller " + caller
                        + " (pid=" + callingPid + ") when starting: "
                        + intent.toString());
                err = ActivityManager.START_PERMISSION_DENIED;
            }
        }

        final int userId = aInfo != null && aInfo.applicationInfo != null
                ? UserHandle.getUserId(aInfo.applicationInfo.uid) : 0;

        if (err == ActivityManager.START_SUCCESS) {
            Slog.i(TAG, "START u" + userId + " {" + intent.toShortString(true, true, true, false)
                    + "} from uid " + callingUid);
        }

        ActivityRecord sourceRecord = null;
        ActivityRecord resultRecord = null;
        if (resultTo != null) {
            //获取当前Activity的ActivityRecord，即例子中的MainActivity的ActivityRecord
            sourceRecord = mRootActivityContainer.isInAnyStack(resultTo);
            ...
        }

		...

   		//创建要启动的Activity的ActivityRecord对象
        ActivityRecord r = new ActivityRecord(mService, callerApp, callingPid, callingUid,callingPackage, intent, resolvedType, aInfo, mService.getGlobalConfiguration(),resultRecord, resultWho, requestCode, componentSpecified, voiceSession != null,mSupervisor, checkedOptions, sourceRecord);
        if (outActivity != null) {
            outActivity[0] = r;
        }

        if (r.appTimeTracker == null && sourceRecord != null) {
            r.appTimeTracker = sourceRecord.appTimeTracker;
        }
		//得到当前正在显示的ActivityStack
        final ActivityStack stack = mRootActivityContainer.getTopDisplayFocusedStack();
        if (voiceSession == null && (stack.getResumedActivity() == null
                || stack.getResumedActivity().info.applicationInfo.uid != realCallingUid)) {
            //如果要启动的Activity与上前正在显示的Activity的用户id（uid）不同，即是不同app，检查是否允许切换App
            if (!mService.checkAppSwitchAllowedLocked(callingPid, callingUid,
                    realCallingPid, realCallingUid, "Activity start")) {
                if (!(restrictedBgActivity && handleBackgroundActivityAbort(r))) {
                    //不允许切换，则把要启动的Activity添加到ActivityStartController的mPendingActivityLaunches中
                    mController.addPendingActivityLaunch(new PendingActivityLaunch(r,
                            sourceRecord, startFlags, stack, callerApp));
                }
                ActivityOptions.abort(checkedOptions);
                //直接返回
                return ActivityManager.START_SWITCHES_CANCELED;
            }
        }
		//上面已经检查了禁切换，所以这里允许切换，一般是用户按下home，或者是用户点击主页的app图标
        mService.onStartActivitySetDidAppSwitch();
        //处理之前加入到ActivityStartController的mPendingActivityLaunches的Activity
        //，该方法最终也是会调用下面的这个startActivity
        mController.doPendingActivityLaunches(false);

        final int res = startActivity(r, sourceRecord, voiceSession, voiceInteractor, startFlags,true /* doResume */, checkedOptions, inTask, outActivity, restrictedBgActivity);
        mSupervisor.getActivityMetricsLogger().notifyActivityLaunched(res, outActivity[0]);
        return res;
    }
```

```java
    private int startActivity(final ActivityRecord r, ActivityRecord sourceRecord,
                IVoiceInteractionSession voiceSession, IVoiceInteractor voiceInteractor,
                int startFlags, boolean doResume, ActivityOptions options, TaskRecord inTask,ActivityRecord[] outActivity, boolean restrictedBgActivity) {
        int result = START_CANCELED;
        final ActivityStack startedActivityStack;
        try {
            //通知WindowManagerService暂停布局
            mService.mWindowManager.deferSurfaceLayout();
            result = startActivityUnchecked(r, sourceRecord, voiceSession, voiceInteractor,startFlags, doResume, options, inTask, outActivity,restrictedBgActivity);
        } finally {
            ...
            //启动Activity后，通知WindowManagerService继续布局
            mService.mWindowManager.continueSurfaceLayout();
        }

        postStartActivityProcessing(r, result, startedActivityStack);

        return result;
    }

```

之前的startActivity函数的作用主要就是根据传入的参数，创建要启动的Activity的ActivityRecord，以及处理一些情况。最终交给startActivityUnchecked

#### 2.3.3 设置ActivityRecord的TaskRecord和ActivityStack 

```java
    private int startActivityUnchecked(final ActivityRecord r, ActivityRecord sourceRecord,
            IVoiceInteractionSession voiceSession, IVoiceInteractor voiceInteractor,
            int startFlags, boolean doResume, ActivityOptions options, TaskRecord inTask,
            ActivityRecord[] outActivity, boolean restrictedBgActivity) {
        //初始化状态，这里会将ActivityStarter.mStartActivity设置为r，即药启动的Activity
        //设置当前的Activity为mSourceRecord
        setInitialState(r, options, inTask, doResume, startFlags, sourceRecord, voiceSession,voiceInteractor, restrictedBgActivity);

        final int preferredWindowingMode = mLaunchParams.mWindowingMode;
		//计算Launch Flags即启动模式
        computeLaunchingTaskFlags();
		//计算源Activity的ActivityStack是哪个
        computeSourceStack();

        mIntent.setFlags(mLaunchFlags);
		//根据flags获取是否存在可以复用的Activity
        ActivityRecord reusedActivity = getReusableIntentActivity();

        ...
       	
        if (reusedActivity != null) {
            ...
            //存在可复用地Activity，则走此逻辑，
            //若启动模式是SingleInSTANCE，会调用onNewIntent
        }
		...
        //要启动地Activity与Task顶部的Activity相同，判断是否继续启动
        final ActivityStack topStack = mRootActivityContainer.getTopDisplayFocusedStack();
        final ActivityRecord topFocused = topStack.getTopActivity();
        final ActivityRecord top = topStack.topRunningNonDelayedActivityLocked(mNotTop);
        final boolean dontStart = top != null && mStartActivity.resultTo == null
                && top.mActivityComponent.equals(mStartActivity.mActivityComponent)
                && top.mUserId == mStartActivity.mUserId
                && top.attachedToProcess()
                && ((mLaunchFlags & FLAG_ACTIVITY_SINGLE_TOP) != 0
                || isLaunchModeOneOf(LAUNCH_SINGLE_TOP, LAUNCH_SINGLE_TASK))
                && (!top.isActivityTypeHome() || top.getDisplayId() == mPreferredDisplayId);
        if (dontStart) {
            ...
            //重用栈顶的Activity，分发onNewIntent事件
            deliverNewIntent(top);
            return START_DELIVERED_TO_TOP;
        }

                boolean newTask = false;
        final TaskRecord taskToAffiliate = (mLaunchTaskBehind && mSourceRecord != null)
                ? mSourceRecord.getTaskRecord() : null;

        //设置要启动的ActivityRecord的TaskRecord,和ActivityStack
        int result = START_SUCCESS;
        if (mStartActivity.resultTo == null && mInTask == null && !mAddingToTask
                && (mLaunchFlags & FLAG_ACTIVITY_NEW_TASK) != 0) {
            //mStartActivity.resultTo表示是哪个Activity启动这个Actvity
            //这里要创建新的TaskRecord
            newTask = true;
            result = setTaskFromReuseOrCreateNewTask(taskToAffiliate);
        } else if (mSourceRecord != null) {
            //如果源Activity存在，则使用它的TaskRecord
            result = setTaskFromSourceRecord();
        } else if (mInTask != null) {
            result = setTaskFromInTask();
        } else {
            result = setTaskToCurrentTopOrCreateNewTask();
        }
        ...

        mTargetStack.startActivityLocked(mStartActivity, topFocused, newTask, mKeepCurTransition,
                mOptions);
        if (mDoResume) {
            final ActivityRecord topTaskActivity =
                    mStartActivity.getTaskRecord().topRunningActivityLocked();
            if (!mTargetStack.isFocusable()
                    || (topTaskActivity != null && topTaskActivity.mTaskOverlay
                    && mStartActivity != topTaskActivity)) {
                mTargetStack.ensureActivitiesVisibleLocked(mStartActivity, 0, !PRESERVE_WINDOWS);
                mTargetStack.getDisplay().mDisplayContent.executeAppTransition();
            } else {
                if (mTargetStack.isFocusable()
                        && !mRootActivityContainer.isTopDisplayFocusedStack(mTargetStack)) {
                    mTargetStack.moveToFront("startActivityUnchecked");
                }
                //调用RootActivityContainer的resumeFocusedStacksTopActivities
                mRootActivityContainer.resumeFocusedStacksTopActivities(
                        mTargetStack, mStartActivity, mOptions);
            }
        } else if (mStartActivity != null) {
            mSupervisor.mRecentTasks.add(mStartActivity.getTaskRecord());
        }
        mRootActivityContainer.updateUserStack(mStartActivity.mUserId, mTargetStack);

        mSupervisor.handleNonResizableTaskIfNeeded(mStartActivity.getTaskRecord(),
           redWindowingMode, mPreferredDisplayId, mTargetStack);

        return START_SUCCESS;
    }
```



### 2.4 RootActivityContainer设置顶部显示的ActivityRecord

RootActivityContainer表示Activity容器的根节点，ActivityStarter最终调用它的`resumeFocusedStacksTopActivities`来将**ActivityRecord置于它所属TaskRecord的顶部**，**将TaskRecord置于ActivityStack的顶部**，**以及将ActivityStack置于ActivityDisplay的顶部**

```java
    boolean resumeFocusedStacksTopActivities(ActivityStack targetStack, ActivityRecord target, ActivityOptions targetOptions) {

        if (!mStackSupervisor.readyToResume()) {
            return false;
        }
        boolean result = false;
        if (targetStack != null && (targetStack.isTopStackOnDisplay()
                || getTopDisplayFocusedStack() == targetStack)) {
            //调用ActivityStack的resumeTopActivityUncheckedLocked
            result = targetStack.resumeTopActivityUncheckedLocked(target, targetOptions);
        }
       ...
        return result;
    }

```

从代码中，我们可以看到，RootActivityContainer是将任务交给了具体的ActivityRecord所在的ActivityStack

### 2.5 ActivityStack设置顶部显示的ActivityRecord

```java
    boolean resumeTopActivityUncheckedLocked(ActivityRecord prev, ActivityOptions options) {
        if (mInResumeTopActivity) {
            return false;
        }

        boolean result = false;
        try {
           
            mInResumeTopActivity = true;
            //********************************
            result = resumeTopActivityInnerLocked(prev, options);
            final ActivityRecord next = topRunningActivityLocked(true /* focusableOnly */);
            if (next == null || !next.canTurnScreenOn()) {
                checkReadyForSleep();
            }
        } finally {
            mInResumeTopActivity = false;
        }

        return result;
    }

```

`resumeTopActivityInnerLocked`有两个主要功能，一是通知上一个Activity执行onPause，二是启动新的Activity

```java
    private boolean resumeTopActivityInnerLocked(ActivityRecord prev, ActivityOptions options) {
        if (!mService.isBooting() && !mService.isBooted()) {
            //系统没有进入booting或booted状态，则不允许启动Activity
            return false;
        }
        //这里取到的是ActivityStack的最后一个TaskRecord的最后一个的ActivityRecord，即例子中的MainActivity，因为前面在ActivityStarter中已经设置。
		ActivityRecord next = topRunningActivityLocked(true /* focusableOnly */);
        ...	
		//暂停其他Activity，存在要暂停的pausing，则为true
        boolean pausing = getDisplay().pauseBackStacks(userLeaving, next, false);
        if (mResumedActivity != null) {
            if (DEBUG_STATES) Slog.d(TAG_STATES,
                    "resumeTopActivityLocked: Pausing " + mResumedActivity);
            //2.7.1 当前resumd状态activity不为空，则需要先暂停该Activity
            pausing |= startPausingLocked(userLeaving, false, next, false);
        }
        
          if (pausing && !resumeWhilePausing) {
            //当暂停成功，并且没有设置Activity为FLAG_RESUME_WHILE_PAUSING，并且不是画中画模式
            //进入该分支
            ...
            return true;
        } else if (mResumedActivity == next && next.isState(RESUMED)
                && display.allResumedActivitiesComplete()) {
            executeAppTransition(options);
            if (DEBUG_STATES) Slog.d(TAG_STATES,
                    "resumeTopActivityLocked: Top activity resumed (dontWaitForPause) " + next);
            return true;
        }

 		...
        //暂停之后会重新进入这个方法
		//下半部分在启动Activity中分析
        return true;
    }


```

#### 2.5.1 暂停Activity

这里通过startPausingLocked通知上一个Activity进入onPause

```java
    final boolean startPausingLocked(boolean userLeaving, boolean uiSleeping,
            ActivityRecord resuming, boolean pauseImmediately) {
        ...
        //pre为当前的Activity,即例子中的MainActivity
        ActivityRecord prev = mResumedActivity;
        ...   
        mPausingActivity = prev;
        mLastPausedActivity = prev;
        mLastNoHistoryActivity = (prev.intent.getFlags() & Intent.FLAG_ACTIVITY_NO_HISTORY) != 0
                || (prev.info.flags & ActivityInfo.FLAG_NO_HISTORY) != 0 ? prev : null;
        //设置当前的Activity状态为PAUSING
        prev.setState(PAUSING, "startPausingLocked");
        prev.getTaskRecord().touchActiveTime();
        clearLaunchTime(prev);

        mService.updateCpuStats();
        if (prev.attachedToProcess()) {
            if (DEBUG_PAUSE) Slog.v(TAG_PAUSE, "Enqueueing pending pause: " + prev);
            try {
                EventLogTags.writeAmPauseActivity(prev.mUserId, System.identityHashCode(prev),prev.shortComponentName, "userLeaving=" + userLeaving);
				//向app进程发送暂停Activity的事务
                mService.getLifecycleManager().scheduleTransaction(prev.app.getThread(),
                        prev.appToken, PauseActivityItem.obtain(prev.finishing, userLeaving,prev.configChangeFlags, pauseImmediately));
            } catch (Exception e) {
				...
            }
        } else {
			...
        }
        
        if (!uiSleeping && !mService.isSleepingOrShuttingDownLocked()) {
            mStackSupervisor.acquireLaunchWakelock();
        }
       
        if (mPausingActivity != null) {
            if (!uiSleeping) {
                prev.pauseKeyDispatchingLocked();
            } else if (DEBUG_PAUSE) {
                 Slog.v(TAG_PAUSE, "Key dispatch not paused for screen off");
            }

            if (pauseImmediately) {
                completePauseLocked(false, resuming);
                return false;
            } else {
                //参数pauseImmediately为flase，进入该分支
                //设置暂停请求超时处理
                schedulePauseTimeout(prev);
                return true;
            }

        } else {
            ...
        }

        
    }

```

mService为ATMS对象，mService.getLifecycleManager()得到ATMS的成员变量ClientLifecycleManager对象，并执行scheduleTransaction，传入一个PauseActivityItem，表示将当前RESUME状态的Activity的状态设为PAUSE。

##### 2.5.1.1 ActivityStack通知app进程暂停Activity

```java
    void scheduleTransaction(ClientTransaction transaction) throws RemoteException {
        final IApplicationThread client = transaction.getClient();
        transaction.schedule();
        if (!(client instanceof Binder)) {
            transaction.recycle();
        }
    }

    void scheduleTransaction(@NonNull IApplicationThread client, @NonNull IBinder activityToken,
            @NonNull ActivityLifecycleItem stateRequest) throws RemoteException {
        //将本地的app进程Binder代理IApplicationThread和actiivtyToken以及stateRequest封装为ClientTransaction对象
        final ClientTransaction clientTransaction = transactionWithState(client, activityToken,
                stateRequest);
        scheduleTransaction(clientTransaction);
    }

```

```java
//ClientTransaction.java 中
	private IApplicationThread mClient;

    public void schedule() throws RemoteException {
        mClient.scheduleTransaction(this);
    }

```

我们看到，最终调用的IApplicationThread的scheduleTransaction方法，这是一个**远程调用**，会调用app进程中ApplicationThread中的`scheduleTransaction`：

```java
//ActivityThread.java#ApplicationThread   
        @Override
        public void scheduleTransaction(ClientTransaction transaction) throws RemoteException {
            ActivityThread.this.scheduleTransaction(transaction);
        }
```

ApplicationThread会调用ActivityThread中的同名方法，这个方法是在ActivityThread的父类ClientTransactionHandler中实现的：

```java
    void scheduleTransaction(ClientTransaction transaction) {
        transaction.preExecute(this);
        sendMessage(ActivityThread.H.EXECUTE_TRANSACTION, transaction);
    }

```

##### 2.5.1.2 app进程通过Handler处理事务消息

H是一个ActivityThread的内部Handler类，这里直接发送事务交给这个Handler处理：

```java
       public void handleMessage(Message msg) {
            if (DEBUG_MESSAGES) Slog.v(TAG, ">>> handling: " + codeToString(msg.what));
            switch (msg.what) {
                ...
                case EXECUTE_TRANSACTION:
                    final ClientTransaction transaction = (ClientTransaction) msg.obj;
                    mTransactionExecutor.execute(transaction);
                    if (isSystem()) {
                        transaction.recycle();
                    }
                    break;
    				...
            }
        }
    }

```

##### 2.5.1.3 TransactionExecutor处理暂停Activity的事务

Handler接受到这个消息，又会将消息交给ActivityThread的成员变量mTransactionExecutor处理，它的类型是TransactionExecutor，职责就是处理事务。

```java
//TransactionExecutor.java    
public void execute(ClientTransaction transaction) {
        ...
        //PauseActivityItem没有设置callback
        executeCallbacks(transaction);

        executeLifecycleState(transaction);
        mPendingActions.clear();
        if (DEBUG_RESOLVER) Slog.d(TAG, tId(transaction) + "End resolving transaction");
    }

   private void executeLifecycleState(ClientTransaction transaction) {
        final ActivityLifecycleItem lifecycleItem = transaction.getLifecycleStateRequest();
        ...
        lifecycleItem.execute(mTransactionHandler, token, mPendingActions);
        lifecycleItem.postExecute(mTransactionHandler, token, mPendingActions);
    }

```

TransactionExecutor最终将事务交给ActivityLifecycleItem本身去执行，这里的lifecycleItem，根据前文，我们知道是**PauseActivityItem**：

```java
    @Override
    public void execute(ClientTransactionHandler client, IBinder token,
            PendingTransactionActions pendingActions) {
        Trace.traceBegin(TRACE_TAG_ACTIVITY_MANAGER, "activityPause");
        client.handlePauseActivity(token, mFinished, mUserLeaving, mConfigChanges, pendingActions,
                "PAUSE_ACTIVITY_ITEM");
        Trace.traceEnd(TRACE_TAG_ACTIVITY_MANAGER);
    }

```

##### 2.5.1.4 PauseActivityItem通过ActivityThread暂停Activity

PauseActivityItem的execute调用了ClientTransactionHandler#handlePauseActivity，我们知道实际上这个client就是ActivityThread

```java
    @Override
    public void handlePauseActivity(IBinder token, boolean finished, boolean userLeaving,
            int configChanges, PendingTransactionActions pendingActions, String reason) {
        //得到指定Activity（即当前要pause的Activity）的ActivityClientRecord对象
        ActivityClientRecord r = mActivities.get(token);
        if (r != null) {
            ...
            performPauseActivity(r, finished, reason, pendingActions);
            ...
        }
    }

    private Bundle performPauseActivity(ActivityClientRecord r, boolean finished, String reason,
            PendingTransactionActions pendingActions) {
 		...
        performPauseActivityIfNeeded(r, reason);
		...
        return shouldSaveState ? r.state : null;
    }

    private void performPauseActivityIfNeeded(ActivityClientRecord r, String reason) {
        if (r.paused) {
            return;
        }
        reportTopResumedActivityChanged(r, false /* onTop */, "pausing");

        try {
            r.activity.mCalled = false;
            //通过mInstrumentation分发事件
            mInstrumentation.callActivityOnPause(r.activity);
            ..
        } catch (SuperNotCalledException e) {
            throw e;
        } catch (Exception e) {
            ..
        }
        
        r.setState(ON_PAUSE);
    }

```

##### 2.5.1.5 ActivityThread通过Instrumentation分发onPause事件

我们看到这里一路调用，最终调用了Instrumentation#callActivityOnPause：

```java
    //Instrumentation
    public void callActivityOnPause(Activity activity) {
        activity.performPause();
    }
    
    final void performPause() {
        dispatchActivityPrePaused();
        mDoReportFullyDrawn = false;
        mFragments.dispatchPause();
        mCalled = false;
        //调用生命周期方法
        onPause();
        writeEventLog(LOG_AM_ON_PAUSE_CALLED, "performPause");
        mResumed = false;
        if (!mCalled && getApplicationInfo().targetSdkVersion
                >= android.os.Build.VERSION_CODES.GINGERBREAD) {
            throw new SuperNotCalledException(
                    "Activity " + mComponent.toShortString() +
                    " did not call through to super.onPause()");
        }
        dispatchActivityPostPaused();
    }

```

##### 2.5.1.6  app进程通知ATMS已经暂停Activity

在app进程进行完暂停Activity之后，会调用PauseActivityItem的postExecute方法：

```java
   private void executeLifecycleState(ClientTransaction transaction) {
        final ActivityLifecycleItem lifecycleItem = transaction.getLifecycleStateRequest();
        ...
        lifecycleItem.execute(mTransactionHandler, token, mPendingActions);
        //执行postExecute，通知ATMS app进程暂停Activity成功
        lifecycleItem.postExecute(mTransactionHandler, token, mPendingActions);
    }
```

```java
    @Override
    public void postExecute(ClientTransactionHandler client, IBinder token,
            PendingTransactionActions pendingActions) {
        if (mDontReport) {//mDontReport为PauseActivityItem.obtain的第四个参数，为flase
            return;
        }
        try {
            //调用ATMS的activityPaused方法
            ActivityTaskManager.getService().activityPaused(token);
        } catch (RemoteException ex) {
            throw ex.rethrowFromSystemServer();
        }
    }
```

##### 2.5.1.7 ATMS通知ActivityStack已经暂停Activity

```java
@Override
public final void activityPaused(IBinder token) {
    final long origId = Binder.clearCallingIdentity();
    synchronized (mGlobalLock) {
        //通过Activity.mToken得到ActivityRecord，然后得到ActivityStack
        ActivityStack stack = ActivityRecord.getStackLocked(token);
        if (stack != null) {
            stack.activityPausedLocked(token, false);
        }
    }
    Binder.restoreCallingIdentity(origId);
}
```
到这里，Actvity的暂停处理已经走完了

##### 2.5.1.8 设置暂停Activity超时处理

回到startPausingLocked函数中，让我们看到它他发送暂停Activity的请求之后的操作：

```java
    final boolean startPausingLocked(boolean userLeaving, boolean uiSleeping,
            ActivityRecord resuming, boolean pauseImmediately) {
                if (prev.attachedToProcess()) {
        ...
        //发送事务后
        if (mPausingActivity != null) {  
			...
            //pauseImmediately是方法的参数，resumeTopActivityInnerLocked中传入false
            if (pauseImmediately) {
                completePauseLocked(false, resuming);
                return false;
            } else {
                //进入该分支
                schedulePauseTimeout(prev);
                return true;
            }

        } else {
            ...
        }
    }

```

```java
private void schedulePauseTimeout(ActivityRecord r) {
    final Message msg = mHandler.obtainMessage(PAUSE_TIMEOUT_MSG);
    msg.obj = r;
    r.pauseTime = SystemClock.uptimeMillis();
    mHandler.sendMessageDelayed(msg, PAUSE_TIMEOUT);//PAUSE_TIMEOUT=500
    if (DEBUG_PAUSE) Slog.v(TAG_PAUSE, "Waiting for pause to complete...");
}
```

这里通过mHandler发送了一个**PAUSE_TIMEOUT_MSG** 的 Message，并且延时了500ms发送，Message带有当前要暂停的ActivityRecord。mHandler对象的类型是ActivityStack的内部类ActivityStackHandler，继承Handler：

```java
  private class ActivityStackHandler extends Handler {
		...
        @Override
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case PAUSE_TIMEOUT_MSG: {
                    ActivityRecord r = (ActivityRecord)msg.obj;
                    Slog.w(TAG, "Activity pause timeout for " + r);
                    synchronized (mService.mGlobalLock) {
                        if (r.hasProcess()) {
                            mService.logAppTooSlow(r.app, r.pauseTime, "pausing " + r);
                        }
                        activityPausedLocked(r.appToken, true);
                    }
                } break;
                ...
            }
 		}
 }
```

当Handler收到超时消息时，最终会调用activityPausedLocked方法，这里的步骤和app成功暂停Activity通知ATMS相同，最终都调用了`activityPausedLocked`。

##### 2.5.1.9 **ActivityStack进行Activity暂停之后的工作**

```java
final void activityPausedLocked(IBinder token, boolean timeout) {
    if (DEBUG_PAUSE) Slog.v(TAG_PAUSE,
        "Activity paused: token=" + token + ", timeout=" + timeout);

    final ActivityRecord r = isInStackLocked(token);

    if (r != null) {
        //移除暂停Activity超时的Handler的消息
        mHandler.removeMessages(PAUSE_TIMEOUT_MSG, r);
        if (mPausingActivity == r) {
            //验证ActivityStack中的mPausingActivity与暂停的Activity是否是同一个
            if (DEBUG_STATES) Slog.v(TAG_STATES, "Moving to PAUSED: " + r
                    + (timeout ? " (due to timeout)" : " (pause complete)"));
            mService.mWindowManager.deferSurfaceLayout();
            try {
                completePauseLocked(true /* resumeNext */, null /* resumingActivity */);
            } finally {
                mService.mWindowManager.continueSurfaceLayout();
            }
            return;
        } else {
			...
        }
    }
    mRootActivityContainer.ensureActivitiesVisible(null, 0, !PRESERVE_WINDOWS);
}
```
activityPausedLocked首先验证app进程暂停的Activity与ActivityStack中记录的要暂停的Activity，相同则执行`completePauseLocked`，看方法名我们就能看出来，这个方法标志着暂停Activity的结束：

```java
private void completePauseLocked(boolean resumeNext, ActivityRecord resuming) {
    ActivityRecord prev = mPausingActivity;
    if (DEBUG_PAUSE) Slog.v(TAG_PAUSE, "Complete pause: " + prev);

    if (prev != null) {
        prev.setWillCloseOrEnterPip(false);
        final boolean wasStopping = prev.isState(STOPPING);
        //设置要暂停的ActivityRecord的State为PAUSED，
        //并且将当前的ActivityStack的mResumeActivity 设为null
        prev.setState(PAUSED, "completePausedLocked");
     	...
        //设置ActivityStack的mPausingActivity为null
        mPausingActivity = null;
    }
  
    if (resumeNext) {
        final ActivityStack topStack = mRootActivityContainer.getTopDisplayFocusedStack();
        if (!topStack.shouldSleepOrShutDownActivities()) {
            mRootActivityContainer.resumeFocusedStacksTopActivities(topStack, prev, null);
        } else {
            checkReadyForSleep();
            ActivityRecord top = topStack.topRunningActivityLocked();
            if (top == null || (prev != null && top != prev)) {
                mRootActivityContainer.resumeFocusedStacksTopActivities();
            }
        }
    }
	...
}
```
completePauseLocked首先设置mPausingActivity的状态为PAUSED，然后将它的值设为null。由于这里的方法参数resumeNext为ture，最后又重新来到`RootActivityContainer.resumeFocusedStacksTopActivities`方法中，在2.5.1暂停Activity一节中，我们知道最终调用`resumeTopActivityInnerLocked`，然后调用`startPausingLocked`方法开始暂停Activity。

而这一次重新来到这里，由于在`completePauseLocked`函数中，通过`prev.setState(PAUSED, "completePausedLocked")`将**mResumedActivity设置为null**，所以不会进入startPausingLocked中，具体看**2.5.2 启动新Activity** 一节分析

#### 2.5.2 启动新Activity

##### 2.5.2.1 重新来到ActivityStack#resumeTopActivityInnerLocked

```java
    private boolean resumeTopActivityInnerLocked(ActivityRecord prev, ActivityOptions options) {
        ...
        //由于mResumedActivity为null，pausing为false
        boolean pausing = getDisplay().pauseBackStacks(userLeaving, next, false);
        if (mResumedActivity != null) {
            //mResumedActivity为null，此时不进入
            if (DEBUG_STATES) Slog.d(TAG_STATES,
                    "resumeTopActivityLocked: Pausing " + mResumedActivity);
            pausing |= startPausingLocked(userLeaving, false, next, false);
        }
        if (pausing && !resumeWhilePausing) {
            ...
            return true;
        } else if (mResumedActivity == next && next.isState(RESUMED)
                && display.allResumedActivitiesComplete()) {
			...
            return true;
        }
       	...
		//代码执行到此处，next指要启动的Activity
        if (next.attachedToProcess()) {
            ...
        } else {
            // 一个新的Activity并没有关联到进程，所以进入此处
            if (!next.hasBeenLaunched) {
                next.hasBeenLaunched = true;
            } else {
                if (SHOW_APP_STARTING_PREVIEW) {
                    next.showStartingWindow(null /* prev */, false /* newTask */,
                            false /* taskSwich */);
                }
                if (DEBUG_SWITCH) Slog.v(TAG_SWITCH, "Restarting: " + next);
            }
            if (DEBUG_STATES) Slog.d(TAG_STATES, "resumeTopActivityLocked: Restarting " + next);
            //最终执行该方法
            mStackSupervisor.startSpecificActivityLocked(next, true, true);
        }

        return true;
    }

```

##### 2.5.2.2 ActivityStackSupervisor启动Activity

```java
    void startSpecificActivityLocked(ActivityRecord r, boolean andResume, boolean checkConfig) {
        //得到当前app的进程控制器，用于判断是否已经app进程是否已经存在并运行
        final WindowProcessController wpc =
                mService.getProcessController(r.processName, r.info.applicationInfo.uid);

        boolean knownToBeDead = false;
        if (wpc != null && wpc.hasThread()) {
            try {
                realStartActivityLocked(r, wpc, andResume, checkConfig);
                return;
            } catch (RemoteException e) {
                Slog.w(TAG, "Exception when starting activity "
                        + r.intent.getComponent().flattenToShortString(), e);
            }
            knownToBeDead = true;
        }
		...
    }
```



```java
   boolean realStartActivityLocked(ActivityRecord r, WindowProcessController proc,
            boolean andResume, boolean checkConfig) throws RemoteException {
		...

        final TaskRecord task = r.getTaskRecord();
        final ActivityStack stack = task.getStack();

        beginDeferResume();

        try {
            r.startFreezingScreenLocked(proc, 0);
            r.startLaunchTickingLocked();
			//设置ActivityRecord的进程控制器为proc
            r.setProcess(proc);

			...
            proc.addActivityIfNeeded(r);
            ...
            try {
                ...
                //创建启动Activity的事务
                final ClientTransaction clientTransaction = ClientTransaction.obtain(
                        proc.getThread(), r.appToken);
                final DisplayContent dc = r.getDisplay().mDisplayContent;
                //给事务添加回调，我们看到这里添加的是LaunchActivityItem
                clientTransaction.addCallback(LaunchActivityItem.obtain(new Intent(r.intent),
                        System.identityHashCode(r), r.info,
                        mergedConfiguration.getGlobalConfiguration(),
                        mergedConfiguration.getOverrideConfiguration(), r.compat,
                        r.launchedFromPackage, task.voiceInteractor, proc.getReportedProcState(),
                        r.icicle, r.persistentState, results, newIntents,
                        dc.isNextTransitionForward(), proc.createProfilerInfoIfNeeded(),
                                r.assistToken));

                //添加事务设置的生命周期状态项
                final ActivityLifecycleItem lifecycleItem;
                if (andResume) {//方法参数andResume为true，进入该分支
                    lifecycleItem = ResumeActivityItem.obtain(dc.isNextTransitionForward());
                } else {
                    lifecycleItem = PauseActivityItem.obtain();
                }
                clientTransaction.setLifecycleStateRequest(lifecycleItem);

                //远程执行该事务
                mService.getLifecycleManager().scheduleTransaction(clientTransaction);

     			...

            } catch (RemoteException e) {
               ...
            }
        } finally {
            endDeferResume();
        }

       	...
        // Perform OOM scoring after the activity state is set, so the process can be updated with
        // the latest state.
        proc.onStartActivity(mService.mTopProcessState, r.info);

       ...

        return true;
    }

```

与Activity的暂停类似，这里也创建了一个事务ClientTransaction，并且通过`mService.getLifecycleManager().scheduleTransaction(clientTransaction)`将事务发送到app进程中处理。与之前的暂停Activity处不同的是，这里同时给ClientTransaction调用了`clientTransaction.addCallback`和`clientTransaction.setLifecycleStateRequest(lifecycleItem)`

##### 2.5.2.3 通知app进程启动Activity

根据2.5.1暂停Activity一节的分析，我们知道ATMS将事务发送给app进程处理，会调用ActivityThread的scheduleTransaction方法，该方法有ActivityThread的父类实现：

```java
    void scheduleTransaction(ClientTransaction transaction) {
        transaction.preExecute(this);
        sendMessage(ActivityThread.H.EXECUTE_TRANSACTION, transaction);
    }

```

首先执行ClientTransaction#preExecute：

```java
    public void preExecute(android.app.ClientTransactionHandler clientTransactionHandler) {
        if (mActivityCallbacks != null) {
            final int size = mActivityCallbacks.size();
            for (int i = 0; i < size; ++i) {
                mActivityCallbacks.get(i).preExecute(clientTransactionHandler, mActivityToken);
            }
        }
        if (mLifecycleStateRequest != null) {
            mLifecycleStateRequest.preExecute(clientTransactionHandler, mActivityToken);
        }
    }

```

这里分别执行了ActivityCallbacks与LifecycleStateRequest的preExecute，我们知道设置的Callback为LaunchActivityItem，设置的LifecycleStateRequest为ResumeActivityItem：

```java
//LaunchActivityItem
	@Override
    public void preExecute(ClientTransactionHandler client, IBinder token) {
        //表示正在启动的Activity数量+1
        client.countLaunchingActivities(1);
        client.updateProcessState(mProcState, false);
        client.updatePendingConfiguration(mCurConfig);
    }
//ResumeActivityItem
    @Override
    public void preExecute(ClientTransactionHandler client, IBinder token) {
        if (mUpdateProcState) {
            client.updateProcessState(mProcState, false);
        }
    }

```

然后app进程会通过Handler发送消息到主线程，然后通过TransactionExecutor来处理。

```java
//TransactionExecutor.java    
public void execute(ClientTransaction transaction) {
        ...
        //在realStartActivityLocked中添加了Callback为LaunchActivityItem
        executeCallbacks(transaction);
        //在realStartActivityLocked中设置了LifecycleState为ResumeActivityItem
        executeLifecycleState(transaction);
        mPendingActions.clear();
        if (DEBUG_RESOLVER) Slog.d(TAG, tId(transaction) + "End resolving transaction");
    }
```

##### 2.5.2.4 LaunchActivityItem执行创建Activity的任务

在暂停Activity中，并没有添加Callback，所以我们没有进入该方法分析，现在我们可以进去看看：

```java
    public void executeCallbacks(ClientTransaction transaction) {
        final List<ClientTransactionItem> callbacks = transaction.getCallbacks();
        if (callbacks == null || callbacks.isEmpty()) {
            // No callbacks to execute, return early.
            return;
        }
        if (DEBUG_RESOLVER) Slog.d(TAG, tId(transaction) + "Resolving callbacks in transaction");

        final IBinder token = transaction.getActivityToken();
        ActivityClientRecord r = mTransactionHandler.getActivityClient(token);

        final ActivityLifecycleItem finalStateRequest = transaction.getLifecycleStateRequest();
        final int finalState = finalStateRequest != null ? finalStateRequest.getTargetState()
                : UNDEFINED;
        // Index of the last callback that requests some post-execution state.
        final int lastCallbackRequestingState = lastCallbackRequestingState(transaction);

        final int size = callbacks.size();
        for (int i = 0; i < size; ++i) {
            final ClientTransactionItem item = callbacks.get(i);
            if (DEBUG_RESOLVER) Slog.d(TAG, tId(transaction) + "Resolving callback: " + item);
            final int postExecutionState = item.getPostExecutionState();
            final int closestPreExecutionState = mHelper.getClosestPreExecutionState(r,
                    item.getPostExecutionState());
            if (closestPreExecutionState != UNDEFINED) {
                cycleToPath(r, closestPreExecutionState, transaction);
            }
			//在循环中调用
            item.execute(mTransactionHandler, token, mPendingActions);
            item.postExecute(mTransactionHandler, token, mPendingActions);
            if (r == null) {
                // Launch activity request will create an activity record.
                r = mTransactionHandler.getActivityClient(token);
            }
			...
        }
    }

```

我们知道这里的item是LaunchActivityItem：

```java
    @Override
    public void execute(ClientTransactionHandler client, IBinder token,
            PendingTransactionActions pendingActions) {
        Trace.traceBegin(TRACE_TAG_ACTIVITY_MANAGER, "activityStart");
        //创建ActivityClientRecord
        ActivityClientRecord r = new ActivityClientRecord(token, mIntent, mIdent, mInfo,
                mOverrideConfig, mCompatInfo, mReferrer, mVoiceInteractor, mState, mPersistentState,
                mPendingResults, mPendingNewIntents, mIsForward,
                mProfilerInfo, client, mAssistToken);
        //client就是ActivityThread，调用它的handleLaunchActivity
        client.handleLaunchActivity(r, pendingActions, null /* customIntent */);
        Trace.traceEnd(TRACE_TAG_ACTIVITY_MANAGER);
    }

    @Override
    public void postExecute(ClientTransactionHandler client, IBinder token,
            PendingTransactionActions pendingActions) {
        //表示正在启动的Activity数量-1
        client.countLaunchingActivities(-1);
    }
```

这里创建了app进程本地的ActivityClientRecord

```java
    public Activity handleLaunchActivity(ActivityClientRecord r,
            PendingTransactionActions pendingActions, Intent customIntent) {
        ...
        final Activity a = performLaunchActivity(r, customIntent);

        if (a != null) {
            r.createdConfig = new Configuration(mConfiguration);
            reportSizeConfigurations(r);
            if (!r.activity.mFinished && pendingActions != null) {
                pendingActions.setOldState(r.state);
                pendingActions.setRestoreInstanceState(true);
                pendingActions.setCallOnPostCreate(true);
            }
        } else {
            try {
                ActivityTaskManager.getService()
                        .finishActivity(r.token, Activity.RESULT_CANCELED, null,
                                Activity.DONT_FINISH_TASK_WITH_ACTIVITY);
            } catch (RemoteException ex) {
                throw ex.rethrowFromSystemServer();
            }
        }

        return a;
    }

```



```java
    private Activity performLaunchActivity(ActivityClientRecord r, Intent customIntent) {
        //得到ActivityInfo，Actiivty的基本信息
        ActivityInfo aInfo = r.activityInfo;
        if (r.packageInfo == null) {
            r.packageInfo = getPackageInfo(aInfo.applicationInfo, r.compatInfo,
                    Context.CONTEXT_INCLUDE_CODE);
        }
		//得到Activity的ComponentName，即包名和类名
        ComponentName component = r.intent.getComponent();
        if (component == null) {
            component = r.intent.resolveActivity(
                mInitialApplication.getPackageManager());
            r.intent.setComponent(component);
        }

        if (r.activityInfo.targetActivity != null) {
            component = new ComponentName(r.activityInfo.packageName,
                    r.activityInfo.targetActivity);
        }
		
        ContextImpl appContext = createBaseContextForActivity(r);
        Activity activity = null;
        //通过反射得到Activity对象
        try {
            java.lang.ClassLoader cl = appContext.getClassLoader();
            activity = mInstrumentation.newActivity(
                    cl, component.getClassName(), r.intent);
            StrictMode.incrementExpectedActivityCount(activity.getClass());
            r.intent.setExtrasClassLoader(cl);
            r.intent.prepareToEnterProcess();
            if (r.state != null) {
                r.state.setClassLoader(cl);
            }
        } catch (Exception e) {
            ...
        }

        try {
            //得到Application对象
            Application app = r.packageInfo.makeApplication(false, mInstrumentation);
            
            if (activity != null) {
                CharSequence title = r.activityInfo.loadLabel(appContext.getPackageManager());
                Configuration config = new Configuration(mCompatConfiguration);
                if (r.overrideConfig != null) {
                    config.updateFrom(r.overrideConfig);
                }
                if (DEBUG_CONFIGURATION) Slog.v(TAG, "Launching activity "
                        + r.activityInfo.name + " with config " + config);
                Window window = null;
                if (r.mPendingRemoveWindow != null && r.mPreserveWindow) {
                    window = r.mPendingRemoveWindow;
                    r.mPendingRemoveWindow = null;
                    r.mPendingRemoveWindowManager = null;
                }
                appContext.setOuterContext(activity);
                //调用attach方法，做的事情就是new一个PhoneWindow，并关联WindowManager
                activity.attach(appContext, this, getInstrumentation(), r.token,
                        r.ident, app, r.intent, r.activityInfo, title, r.parent,
                        r.embeddedID, r.lastNonConfigurationInstances, config,
                        r.referrer, r.voiceInteractor, window, r.configCallback,
                        r.assistToken);

                if (customIntent != null) {
                    activity.mIntent = customIntent;
                }
                r.lastNonConfigurationInstances = null;
                checkAndBlockForNetworkAccess();
                activity.mStartedActivity = false;
                int theme = r.activityInfo.getThemeResource();
                if (theme != 0) {
                    activity.setTheme(theme);
                }

                activity.mCalled = false;
                //调用Activity的onCreate()
                if (r.isPersistable()) {
                    mInstrumentation.callActivityOnCreate(activity, r.state, r.persistentState);
                } else {
                    mInstrumentation.callActivityOnCreate(activity, r.state);
                }
                if (!activity.mCalled) {
                    throw new SuperNotCalledException(
                        "Activity " + r.intent.getComponent().toShortString() +
                        " did not call through to super.onCreate()");
                }
                r.activity = activity;
            }
            //设置ActivityClientRecord的State为ON_CREATE
            r.setState(ON_CREATE);

            synchronized (mResourcesManager) {
                mActivities.put(r.token, r);
            }

        } catch (SuperNotCalledException e) {
            throw e;

        } catch (Exception e) {
			...
        }

        return activity;
    }

```

##### 2.5.2.5 ResumeActivityItem执行显示Activity的任务

好了，我们很清楚接下来会执行的是ResumeActivityItem的execute方法和postExecute方法，先来看第一个：

```java
//ResumeActivityItem
	@Override
    public void execute(ClientTransactionHandler client, IBinder token,
            PendingTransactionActions pendingActions) {
        Trace.traceBegin(TRACE_TAG_ACTIVITY_MANAGER, "activityResume");
        client.handleResumeActivity(token, true /* finalStateRequest */, mIsForward,
                "RESUME_ACTIVITY");
        Trace.traceEnd(TRACE_TAG_ACTIVITY_MANAGER);
    }

```

相同的套路，ResumeActivityItem也是调用了ActivityThread的`handleResumeActivity`方法：

```java
    @Override
    public void handleResumeActivity(IBinder token, boolean finalStateRequest, boolean isForward,
            String reason) {
    
        unscheduleGcIdler();
        mSomeActivitiesChanged = true;

        final ActivityClientRecord r = performResumeActivity(token, finalStateRequest, reason);
        ...
    }

```

```java
    public ActivityClientRecord performResumeActivity(IBinder token, boolean finalStateRequest,
            String reason) {
        final ActivityClientRecord r = mActivities.get(token);
        ...
        try {
            r.activity.onStateNotSaved();
            r.activity.mFragments.noteStateNotSaved();
            checkAndBlockForNetworkAccess();
            if (r.pendingIntents != null) {
                deliverNewIntents(r, r.pendingIntents);
                r.pendingIntents = null;
            }
            if (r.pendingResults != null) {
                deliverResults(r, r.pendingResults, reason);
                r.pendingResults = null;
            }
            //这里调用Activity的onResume方法
            r.activity.performResume(r.startsNotResumed, reason);

            r.state = null;
            r.persistentState = null;
            r.setState(ON_RESUME);
			//分发生命周期事件
            reportTopResumedActivityChanged(r, r.isTopResumedActivity, "topWhenResuming");
        } catch (Exception e) {
            if (!mInstrumentation.onException(r.activity, e)) {
                throw new RuntimeException("Unable to resume activity "
                        + r.intent.getComponent().toShortString() + ": " + e.toString(), e);
            }
        }
        return r;
    }

```

##### 2.5.2.6 通知ATMS新Activity显示完成

ResumeActivityItem的execute完成后执行postExecute：

```java
    @Override
    public void postExecute(ClientTransactionHandler client, IBinder token,
            PendingTransactionActions pendingActions) {
        try {
            ActivityTaskManager.getService().activityResumed(token);
        } catch (RemoteException ex) {
            throw ex.rethrowFromSystemServer();
        }
    }

```

```java
    public final void activityResumed(IBinder token) {
        final long origId = Binder.clearCallingIdentity();
        synchronized (mGlobalLock) {
            ActivityRecord.activityResumedLocked(token);
            mWindowManager.notifyAppResumedFinished(token);
        }
        Binder.restoreCallingIdentity(origId);
    }

```

























