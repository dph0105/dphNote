## 1. Launcher点击图片启动app

当我们要打开一个app时，我们会点击手机桌面的app图标，然后Android系统会打开目标app的主页。而Android系统的桌面，本身也是一个app，这个app的Activity就是我们看到的桌面，这个Activity叫Launcher。我们可以在`packages\apps\Launcher3`源码路径下看到这个app的AndroidManifest：

```xml
        <activity
            android:name="com.android.launcher3.Launcher"
            android:launchMode="singleTask"
            android:clearTaskOnLaunch="true"
            android:stateNotNeeded="true"
            android:windowSoftInputMode="adjustPan"
            android:screenOrientation="unspecified"
            android:configChanges="keyboard|keyboardHidden|mcc|mnc|navigation|orientation|screenSize|screenLayout|smallestScreenSize"
            android:resizeableActivity="true"
            android:resumeWhilePausing="true"
            android:taskAffinity=""
            android:enabled="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.HOME" />
                <category android:name="android.intent.category.DEFAULT" />
                <category android:name="android.intent.category.MONKEY"/>
                <category android:name="android.intent.category.LAUNCHER_APP" />
            </intent-filter>
            <meta-data
                android:name="com.android.launcher3.grid.control"
                android:value="${packageName}.grid_control" />
        </activity>
```

它只有一个activity标签，它的intent-filter中有一个category标签为android.intent.category.HOME，当systemserver进程将各个系统服务启动完毕后，会启动有这个标签的Activity为系统桌面。

```java
public class Launcher extends BaseDraggingActivity implements LauncherExterns,
        Callbacks, LauncherProviderChangeListener, UserEventDelegate,
        InvariantDeviceProfile.OnIDPChangeListener {
     
    // Main container view for the all apps screen.
    AllAppsContainerView mAppsView;
    AllAppsTransitionController mAllAppsController;
    
 	...
}
```

### 1.1点击桌面app图标

AllAppsContainerView就是用于显示app图标的View，它使用RecyclerView显示app图片列表，我们直接看到它的Adapter：

```java
    public void onBindViewHolder(ViewHolder holder, int position) {
        switch (holder.getItemViewType()) {
            case VIEW_TYPE_ICON:
                AppInfo info = mApps.getAdapterItems().get(position).appInfo;
                BubbleTextView icon = (BubbleTextView) holder.itemView;
                icon.reset();
                icon.applyFromApplicationInfo(info);
                break;
                ...
        }
    }

	public ViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
        switch (viewType) {
            case VIEW_TYPE_ICON:
                BubbleTextView icon = (BubbleTextView) mLayoutInflater.inflate(
                        R.layout.all_apps_icon, parent, false);
                icon.setOnClickListener(ItemClickHandler.INSTANCE);
                icon.setOnLongClickListener(ItemLongClickListener.INSTANCE_ALL_APPS);
                icon.setLongPressTimeoutFactor(1f);
                icon.setOnFocusChangeListener(mIconFocusListener);

                // Ensure the all apps icon height matches the workspace icons in portrait mode.
                icon.getLayoutParams().height = mLauncher.getDeviceProfile().allAppsCellHeightPx;
                return new ViewHolder(icon);
                ...
        }
                
     }

```

它的Adapter为**AllAppsGridAdapter**，在onBindViewHolder中，调用了BubbleTextView的applyFromApplicationInfo，这里设置了BubbleTextView的Tag为AppInfo：

```java
    public void applyFromApplicationInfo(AppInfo info) {
        applyIconAndLabel(info);

        super.setTag(info);
		...
    }

```

在onCreateViewHolder中，设置了点击事件为**ItemClickHandler.INSTANCE**，来看它的onClikc函数

```java
    private static void onClick(View v, String sourceContainer) {
        if (v.getWindowToken() == null) return;

        Launcher launcher = Launcher.getLauncher(v.getContext());
        if (!launcher.getWorkspace().isFinishedSwitchingState()) return;
		//取到view的tag，tag为AppInfo对象
        Object tag = v.getTag();
        if (tag instanceof WorkspaceItemInfo) {
            onClickAppShortcut(v, (WorkspaceItemInfo) tag, launcher, sourceContainer);
        } else if (tag instanceof FolderInfo) {
            if (v instanceof FolderIcon) {
                onClickFolderIcon(v);
            }
        } else if (tag instanceof AppInfo) {
            //使用执行这个方法
            startAppShortcutOrInfoActivity(v, (AppInfo) tag, launcher,
                    sourceContainer == null ? CONTAINER_ALL_APPS: sourceContainer);
        } else if (tag instanceof LauncherAppWidgetInfo) {
            if (v instanceof PendingAppWidgetHostView) {
                onClickPendingWidget((PendingAppWidgetHostView) v, launcher);
            }
        }
    }

```



```java
    private static void startAppShortcutOrInfoActivity(View v, ItemInfo item, Launcher launcher,
            @Nullable String sourceContainer) {
        Intent intent;
        if (item instanceof PromiseAppInfo) {
            ...
        } else {
            intent = item.getIntent();
        }
        if (intent == null) {
            throw new IllegalArgumentException("Input must have a valid intent");
        }
        if (item instanceof WorkspaceItemInfo) {
            ...
        }
        if (v != null && launcher.getAppTransitionManager().supportsAdaptiveIconAnimation()) {
            // Preload the icon to reduce latency b/w swapping the floating view with the original.
            FloatingIconView.fetchIcon(launcher, v, item, true /* isOpening */);
        }
        //执行Launcher#startActivitySafely
        launcher.startActivitySafely(v, intent, item, sourceContainer);
    }

```

### 1.2 执行Launcher#startActivitySafely

```java
    public boolean startActivitySafely(View v, Intent intent, ItemInfo item,
            @Nullable String sourceContainer) {
       ...
        boolean success = super.startActivitySafely(v, intent, item, sourceContainer);
        if (success && v instanceof BubbleTextView) {
            BubbleTextView btv = (BubbleTextView) v;
            btv.setStayPressed(true);
            addOnResumeCallback(btv);
        }
        return success;
    }

```

### 1.3 执行startActivity

```java
   public boolean startActivitySafely(View v, Intent intent, @Nullable ItemInfo item,
            @Nullable String sourceContainer) {
        if (mIsSafeModeEnabled && !PackageManagerHelper.isSystemApp(this, intent)) {
            Toast.makeText(this, R.string.safemode_shortcut_error, Toast.LENGTH_SHORT).show();
            return false;
        }

        Bundle optsBundle = (v != null) ? getActivityLaunchOptionsAsBundle(v) : null;
        UserHandle user = item == null ? null : item.user;

        //给Intent添加一个Flag，表示要在一个新的Task中打开Activity
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        if (v != null) {
            intent.setSourceBounds(getViewBounds(v));
        }
        try {
            boolean isShortcut = (item instanceof WorkspaceItemInfo)
                    && (item.itemType == Favorites.ITEM_TYPE_SHORTCUT
                    || item.itemType == Favorites.ITEM_TYPE_DEEP_SHORTCUT)
                    && !((WorkspaceItemInfo) item).isPromise();
            if (isShortcut) {
                // Shortcuts need some special checks due to legacy reasons.
                startShortcutIntentSafely(intent, optsBundle, item, sourceContainer);
            } else if (user == null || user.equals(Process.myUserHandle())) {
                //调用Activity的startActivity
                startActivity(intent, optsBundle);
                AppLaunchTracker.INSTANCE.get(this).onStartApp(intent.getComponent(),
                        Process.myUserHandle(), sourceContainer);
            } else {
                LauncherAppsCompat.getInstance(this).startActivityForProfile(
                        intent.getComponent(), user, intent.getSourceBounds(), optsBundle);
                AppLaunchTracker.INSTANCE.get(this).onStartApp(intent.getComponent(), user,
                        sourceContainer);
            }
            getUserEventDispatcher().logAppLaunch(v, intent);
            getStatsLogManager().logAppLaunch(v, intent);
            return true;
        } catch (NullPointerException|ActivityNotFoundException|SecurityException e) {
            Toast.makeText(this, R.string.activity_not_found, Toast.LENGTH_SHORT).show();
            Log.e(TAG, "Unable to launch. tag=" + item + " intent=" + intent, e);
        }
        return false;
    }

```

startActivitySafely中就是开启Activity的流程，即` startActivityForResult` > `Instrumentation.execStartActivity` >  `ActivityTaskManager.getService().startActivity`，通过Binder将请求交给systemserver进程中的ATMS进行处理。

## 2. ATMS启动app进程

### 2.1 从开启Activity变成开启进程

ATMS服务通过Binder被远程调用startActivity方法后，会调用一系列方法最终调用`ActivityStackSupervisor#startSpecificActivityLocked`，但是由于此时进程没有启动，所以由开启Activity转为开启进程：

```java
    void startSpecificActivityLocked(ActivityRecord r, boolean andResume, boolean checkConfig) {
        //进程没有开启 wpc为null
        final WindowProcessController wpc =
                mService.getProcessController(r.processName, r.info.applicationInfo.uid);

        boolean knownToBeDead = false;
        if (wpc != null && wpc.hasThread()) {
            try {
                //不执行这里
                realStartActivityLocked(r, wpc, andResume, checkConfig);
                return;
            } catch (RemoteException e) {
                Slog.w(TAG, "Exception when starting activity "
                        + r.intent.getComponent().flattenToShortString(), e);
            }
            knownToBeDead = true;
        }

        try {
            if (Trace.isTagEnabled(TRACE_TAG_ACTIVITY_MANAGER)) {
                Trace.traceBegin(TRACE_TAG_ACTIVITY_MANAGER, "dispatchingStartProcess:"
                        + r.processName);
            }
            //发送开启app进程的消息到ActivityManagerService中
            final Message msg = PooledLambda.obtainMessage(
                    ActivityManagerInternal::startProcess, mService.mAmInternal, r.processName,
                    r.info.applicationInfo, knownToBeDead, "activity", r.intent.getComponent());
            mService.mH.sendMessage(msg);
        } finally {
            Trace.traceEnd(TRACE_TAG_ACTIVITY_MANAGER);
        }
    }

```

开启进程的操作通过发送消息给ATMS的Handler来进行

#### 2.1.1 PooledLambda.obtainMessage

```java
    static <A, B, C, D, E, F> Message obtainMessage(
            HexConsumer<? super A, ? super B, ? super C, ? super D, ? super E, ? super F> function,
            A arg1, B arg2, C arg3, D arg4, E arg5, F arg6) {
        synchronized (Message.sPoolSync) {
            PooledRunnable callback = acquire(PooledLambdaImpl.sMessageCallbacksPool,
                    function, 6, 0, ReturnType.VOID, arg1, arg2, arg3, arg4, arg5, arg6, null, null,
                    null);
            return Message.obtain().setCallback(callback.recycleOnUse());
        }
    }

```

`PooledLambda.obtainMessage`给Message设置了一个Handler，在Handler机制中，当Message有Callback时，当Looper将消息分发给Handler调用它的`dispatchMessage`时：

```java
    public void dispatchMessage(@NonNull Message msg) {
        if (msg.callback != null) {
            handleCallback(msg);
        } else {
            if (mCallback != null) {
                if (mCallback.handleMessage(msg)) {
                    return;
                }
            }
            handleMessage(msg);
        }
    }

    private static void handleCallback(Message message) {
        message.callback.run();
    }
```

即这里这里会调用`ActivityManagerInternal#startProcess`

### 2.2 ActivityManagerInternal#startProcess

PooledLambda.obtainMessage调用了ActivityManagerInternal#startProcess,ActivityManagerInternal是ActivityManagerService下的LoaclService继承的：

```java
        @Override
        public void startProcess(String processName, ApplicationInfo info,
                boolean knownToBeDead, String hostingType, ComponentName hostingName) {
            try {
                if (Trace.isTagEnabled(Trace.TRACE_TAG_ACTIVITY_MANAGER)) {
                    Trace.traceBegin(Trace.TRACE_TAG_ACTIVITY_MANAGER, "startProcess:"
                            + processName);
                }
                synchronized (ActivityManagerService.this) {
                    startProcessLocked(processName, info, knownToBeDead, 0 /* intentFlags */,
                            new HostingRecord(hostingType, hostingName),
                            false /* allowWhileBooting */, false /* isolated */,
                            true /* keepIfLarge */);
                }
            } finally {
                Trace.traceEnd(Trace.TRACE_TAG_ACTIVITY_MANAGER);
            }
        }

```



```java
    final ProcessRecord startProcessLocked(String processName,
            ApplicationInfo info, boolean knownToBeDead, int intentFlags,
            HostingRecord hostingRecord, boolean allowWhileBooting,
            boolean isolated, boolean keepIfLarge) {
        return mProcessList.startProcessLocked(processName, info, knownToBeDead, intentFlags,
                hostingRecord, allowWhileBooting, isolated, 0 /* isolatedUid */, keepIfLarge,
                null /* ABI override */, null /* entryPoint */, null /* entryPointArgs */,
                null /* crashHandler */);
    }

```



```java
    final ProcessRecord startProcessLocked(String processName, ApplicationInfo info,
            boolean knownToBeDead, int intentFlags, HostingRecord hostingRecord,
            boolean allowWhileBooting, boolean isolated, int isolatedUid, boolean keepIfLarge,
            String abiOverride, String entryPoint, String[] entryPointArgs, Runnable crashHandler) {
        long startTime = SystemClock.elapsedRealtime();
        ProcessRecord app;
        ...
        if (app == null) {
            checkSlow(startTime, "startProcess: creating new process record");
            //创建一个ProcessRecord对象
            app = newProcessRecordLocked(info, processName, isolated, isolatedUid, hostingRecord);
            if (app == null) {
                Slog.w(TAG, "Failed making new process record for "
                        + processName + "/" + info.uid + " isolated=" + isolated);
                return null;
            }
            app.crashHandler = crashHandler;
            app.isolatedEntryPoint = entryPoint;
            app.isolatedEntryPointArgs = entryPointArgs;
            checkSlow(startTime, "startProcess: done creating new process record");
        } else {
            app.addPackage(info.packageName, info.longVersionCode, mService.mProcessStats);
            checkSlow(startTime, "startProcess: added package to existing proc");
        }
		...
        
        checkSlow(startTime, "startProcess: stepping in to startProcess");
        //开启进程
        final boolean success = startProcessLocked(app, hostingRecord, abiOverride);
        checkSlow(startTime, "startProcess: done starting proc!");
        return success ? app : null;
    }

```



```java
    boolean startProcessLocked(ProcessRecord app, HostingRecord hostingRecord,
            boolean disableHiddenApiChecks, boolean mountExtStorageFull,
            String abiOverride) {
        ...

        try {
          ...
           //app进程入口类
           final String entryPoint = "android.app.ActivityThread";  
              
            return startProcessLocked(hostingRecord, entryPoint, app, uid, gids,
                    runtimeFlags, mountExternal, seInfo, requiredAbi, instructionSet, invokeWith,
                    startTime);
        } catch (RuntimeException e) {
            Slog.e(ActivityManagerService.TAG, "Failure starting process " + app.processName, e);
            mService.forceStopPackageLocked(app.info.packageName, UserHandle.getAppId(app.uid),
                    false, false, true, false, false, app.userId, "start failure");
            return false;
        }
    }

```



```java
    boolean startProcessLocked(HostingRecord hostingRecord,
            String entryPoint,
            ProcessRecord app, int uid, int[] gids, int runtimeFlags, int mountExternal,
            String seInfo, String requiredAbi, String instructionSet, String invokeWith,
            long startTime) {
        app.pendingStart = true;
        app.killedByAm = false;
        app.removed = false;
        app.killed = false;
        ...
		//FLAG_PROCESS_START_ASYNC为ture
        if (mService.mConstants.FLAG_PROCESS_START_ASYNC) {
            if (DEBUG_PROCESSES) Slog.i(TAG_PROCESSES,
                    "Posting procStart msg for " + app.toShortString());
            mService.mProcStartHandler.post(() -> {
                //开启异步
                try {
                    final Process.ProcessStartResult startResult = startProcess(app.hostingRecord,
                            entryPoint, app, app.startUid, gids, runtimeFlags, mountExternal,
                            app.seInfo, requiredAbi, instructionSet, invokeWith, app.startTime);
                    synchronized (mService) {
                        //执行handleProcessStartedLocked
                        handleProcessStartedLocked(app, startResult, startSeq);
                    }
                } catch (RuntimeException e) {
                    synchronized (mService) {
                        Slog.e(ActivityManagerService.TAG, "Failure starting process "
                                + app.processName, e);
                        mPendingStarts.remove(startSeq);
                        app.pendingStart = false;
                        mService.forceStopPackageLocked(app.info.packageName,
                                UserHandle.getAppId(app.uid),
                                false, false, true, false, false, app.userId, "start failure");
                    }
                }
            });
            return true;
        } else {
           ...
        }
    }

```



```java
    private Process.ProcessStartResult startProcess(HostingRecord hostingRecord, String entryPoint,
            ProcessRecord app, int uid, int[] gids, int runtimeFlags, int mountExternal,
            String seInfo, String requiredAbi, String instructionSet, String invokeWith,
            long startTime) {
        try {
            Trace.traceBegin(Trace.TRACE_TAG_ACTIVITY_MANAGER, "Start proc: " +
                    app.processName);
            checkSlow(startTime, "startProcess: asking zygote to start proc");
            final Process.ProcessStartResult startResult;
            if (hostingRecord.usesWebviewZygote()) {
               ...
            } else if (hostingRecord.usesAppZygote()) {
                ...
            } else {
                startResult = Process.start(entryPoint,
                        app.processName, uid, uid, gids, runtimeFlags, mountExternal,
                        app.info.targetSdkVersion, seInfo, requiredAbi, instructionSet,
                        app.info.dataDir, invokeWith, app.info.packageName,
                        new String[] {PROC_START_SEQ_IDENT + app.startSeq});
            }
            checkSlow(startTime, "startProcess: returned from zygote!");
            return startResult;
        } finally {
            Trace.traceEnd(Trace.TRACE_TAG_ACTIVITY_MANAGER);
        }
    }

```



```java
    public static ProcessStartResult start(@NonNull final String processClass,
                                           @Nullable final String niceName,
                                           int uid, int gid, @Nullable int[] gids,
                                           int runtimeFlags,
                                           int mountExternal,
                                           int targetSdkVersion,
                                           @Nullable String seInfo,
                                           @NonNull String abi,
                                           @Nullable String instructionSet,
                                           @Nullable String appDataDir,
                                           @Nullable String invokeWith,
                                           @Nullable String packageName,
                                           @Nullable String[] zygoteArgs) {
        //ZYGOTE_PROCESS 是 ZygoteProcess类的常量对象
        return ZYGOTE_PROCESS.start(processClass, niceName, uid, gid, gids,
                    runtimeFlags, mountExternal, targetSdkVersion, seInfo,
                    abi, instructionSet, appDataDir, invokeWith, packageName,
                    /*useUsapPool=*/ true, zygoteArgs);
    }

```



```java
    public final Process.ProcessStartResult start(@NonNull final String processClass,
                                                  final String niceName,
                                                  int uid, int gid, @Nullable int[] gids,
                                                  int runtimeFlags, int mountExternal,
                                                  int targetSdkVersion,
                                                  @Nullable String seInfo,
                                                  @NonNull String abi,
                                                  @Nullable String instructionSet,
                                                  @Nullable String appDataDir,
                                                  @Nullable String invokeWith,
                                                  @Nullable String packageName,
                                                  boolean useUsapPool,
                                                  @Nullable String[] zygoteArgs) {
        // TODO (chriswailes): Is there a better place to check this value?
        if (fetchUsapPoolEnabledPropWithMinInterval()) {
            informZygotesOfUsapPoolStatus();
        }

        try {
            //Via：通过的意思，这个方法的意思就是通过Zygote开启app进程
            return startViaZygote(processClass, niceName, uid, gid, gids,
                    runtimeFlags, mountExternal, targetSdkVersion, seInfo,
                    abi, instructionSet, appDataDir, invokeWith, /*startChildZygote=*/ false,
                    packageName, useUsapPool, zygoteArgs);
        } catch (ZygoteStartFailedEx ex) {
            Log.e(LOG_TAG,
                    "Starting VM process through Zygote failed");
            throw new RuntimeException(
                    "Starting VM process through Zygote failed", ex);
        }
    }

```



```java
    private Process.ProcessStartResult startViaZygote(@NonNull final String processClass,
                                                      @Nullable final String niceName,
                                                      final int uid, final int gid,
                                                      @Nullable final int[] gids,
                                                      int runtimeFlags, int mountExternal,
                                                      int targetSdkVersion,
                                                      @Nullable String seInfo,
                                                      @NonNull String abi,
                                                      @Nullable String instructionSet,
                                                      @Nullable String appDataDir,
                                                      @Nullable String invokeWith,
                                                      boolean startChildZygote,
                                                      @Nullable String packageName,
                                                      boolean useUsapPool,
                                                      @Nullable String[] extraArgs)
                                                      throws ZygoteStartFailedEx {
        ArrayList<String> argsForZygote = new ArrayList<>();

        // --runtime-args, --setuid=, --setgid=,
        // and --setgroups= must go first
        argsForZygote.add("--runtime-args");
        argsForZygote.add("--setuid=" + uid);
        argsForZygote.add("--setgid=" + gid);
        argsForZygote.add("--runtime-flags=" + runtimeFlags);
        if (mountExternal == Zygote.MOUNT_EXTERNAL_DEFAULT) {
            argsForZygote.add("--mount-external-default");
        } else if (mountExternal == Zygote.MOUNT_EXTERNAL_READ) {
            argsForZygote.add("--mount-external-read");
        } else if (mountExternal == Zygote.MOUNT_EXTERNAL_WRITE) {
            argsForZygote.add("--mount-external-write");
        } else if (mountExternal == Zygote.MOUNT_EXTERNAL_FULL) {
            argsForZygote.add("--mount-external-full");
        } else if (mountExternal == Zygote.MOUNT_EXTERNAL_INSTALLER) {
            argsForZygote.add("--mount-external-installer");
        } else if (mountExternal == Zygote.MOUNT_EXTERNAL_LEGACY) {
            argsForZygote.add("--mount-external-legacy");
        }

        argsForZygote.add("--target-sdk-version=" + targetSdkVersion);

        // --setgroups is a comma-separated list
        if (gids != null && gids.length > 0) {
            StringBuilder sb = new StringBuilder();
            sb.append("--setgroups=");

            int sz = gids.length;
            for (int i = 0; i < sz; i++) {
                if (i != 0) {
                    sb.append(',');
                }
                sb.append(gids[i]);
            }

            argsForZygote.add(sb.toString());
        }

        //进程名称，也就是包名
        if (niceName != null) {
            argsForZygote.add("--nice-name=" + niceName);
        }
        if (seInfo != null) {
            argsForZygote.add("--seinfo=" + seInfo);
        }
        if (instructionSet != null) {
            argsForZygote.add("--instruction-set=" + instructionSet);
        }
        //app目录 /data/user/0/包名/
        if (appDataDir != null) {
            argsForZygote.add("--app-data-dir=" + appDataDir);
        }
        if (invokeWith != null) {
            argsForZygote.add("--invoke-with");
            argsForZygote.add(invokeWith);
        }
        if (startChildZygote) {
            argsForZygote.add("--start-child-zygote");
        }
        if (packageName != null) {//包名
            argsForZygote.add("--package-name=" + packageName);
        }
        //processClass就是app进程的入口类  ActivityThread
        argsForZygote.add(processClass);
        if (extraArgs != null) {
            Collections.addAll(argsForZygote, extraArgs);
        }
        synchronized(mLock) {
            return zygoteSendArgsAndGetResult(openZygoteSocketIfNeeded(abi),
                                              useUsapPool,
                                              argsForZygote);
        }
    }

```



```java
    private Process.ProcessStartResult zygoteSendArgsAndGetResult(
            ZygoteState zygoteState, boolean useUsapPool, @NonNull ArrayList<String> args)
            throws ZygoteStartFailedEx {
        for (String arg : args) {
            if (arg.indexOf('\n') >= 0) {
                throw new ZygoteStartFailedEx("Embedded newlines not allowed");
            } else if (arg.indexOf('\r') >= 0) {
                throw new ZygoteStartFailedEx("Embedded carriage returns not allowed");
            }
        }


        String msgStr = args.size() + "\n" + String.join("\n", args) + "\n";

        if (useUsapPool && mUsapPoolEnabled && canAttemptUsap(args)) {
            try {
                return attemptUsapSendArgsAndGetResult(zygoteState, msgStr);
            } catch (IOException ex) {
                // If there was an IOException using the USAP pool we will log the error and
                // attempt to start the process through the Zygote.
                Log.e(LOG_TAG, "IO Exception while communicating with USAP pool - "
                        + ex.getMessage());
            }
        }

        return attemptZygoteSendArgsAndGetResult(zygoteState, msgStr);
    }

```



```java
    private Process.ProcessStartResult attemptZygoteSendArgsAndGetResult(
            ZygoteState zygoteState, String msgStr) throws ZygoteStartFailedEx {
        try {
            final BufferedWriter zygoteWriter = zygoteState.mZygoteOutputWriter;
            final DataInputStream zygoteInputStream = zygoteState.mZygoteInputStream;

            zygoteWriter.write(msgStr);
            zygoteWriter.flush();

            Process.ProcessStartResult result = new Process.ProcessStartResult();
            result.pid = zygoteInputStream.readInt();
            result.usingWrapper = zygoteInputStream.readBoolean();

            if (result.pid < 0) {
                throw new ZygoteStartFailedEx("fork() failed");
            }

            return result;
        } catch (IOException ex) {
            zygoteState.close();
            Log.e(LOG_TAG, "IO Exception while communicating with Zygote - "
                    + ex.toString());
            throw new ZygoteStartFailedEx(ex);
        }
    }

```



-超时处理





```java
    private boolean handleProcessStartedLocked(ProcessRecord pending,
            Process.ProcessStartResult startResult, long expectedStartSeq) {
        // Indicates that this process start has been taken care of.
        if (mPendingStarts.get(expectedStartSeq) == null) {
            if (pending.pid == startResult.pid) {
                pending.setUsingWrapper(startResult.usingWrapper);
                // TODO: Update already existing clients of usingWrapper
            }
            return false;
        }
        return handleProcessStartedLocked(pending, startResult.pid, startResult.usingWrapper,
                expectedStartSeq, false);
    }

```



```java
    boolean handleProcessStartedLocked(ProcessRecord app, int pid, boolean usingWrapper,
            long expectedStartSeq, boolean procAttached) {
        ...
        synchronized (mService.mPidsSelfLocked) {
            if (!procAttached) {
                //发送启动进程超时消息，PROC_START_TIMEOUT：10*1000
                Message msg = mService.mHandler.obtainMessage(PROC_START_TIMEOUT_MSG);
                msg.obj = app;
                mService.mHandler.sendMessageDelayed(msg, usingWrapper
                        ? PROC_START_TIMEOUT_WITH_WRAPPER : PROC_START_TIMEOUT);
            }
        }
        checkSlow(app.startTime, "startProcess: done updating pids map");
        return true;
    }

```



## 3. Zygote进程fork app进程

## 4. App进程启动

```java
	public static void main(String[] args) {
        Trace.traceBegin(Trace.TRACE_TAG_ACTIVITY_MANAGER, "ActivityThreadMain");

        // 安装选择性系统调用拦截
        AndroidOs.install();
		...
		//初始化app主线程Looper
        Looper.prepareMainLooper();

        //得到传入的序列号 传入的参数如果有 seq=114 这样子的，startSeq就是114
        long startSeq = 0;
        if (args != null) {
            for (int i = args.length - 1; i >= 0; --i) {
                if (args[i] != null && args[i].startsWith(PROC_START_SEQ_IDENT)) {
                    startSeq = Long.parseLong(
                            args[i].substring(PROC_START_SEQ_IDENT.length()));
                }
            }
        }
        //创建ActivityThread对象
        ActivityThread thread = new ActivityThread();
        //调用ActivityThread.attach 传入参数false，表示是应用程序进程
        thread.attach(false, startSeq);
		
        if (sMainThreadHandler == null) {
            sMainThreadHandler = thread.getHandler();
        }
		...

      	//开启消息循环
        Looper.loop();

        throw new RuntimeException("Main thread loop unexpectedly exited");
    }

```

#### 1.1 进入ActivityThread.attach

```java
    private void attach(boolean system, long startSeq) {
        sCurrentActivityThread = this;
        mSystemThread = system;
        if (!system) {
            //在ActivityThread.main方法中，我们知道传入的system为flase，进入该分支
            android.ddm.DdmHandleAppName.setAppName("<pre-initialized>",
                                                    UserHandle.myUserId());
            RuntimeInit.setApplicationObject(mAppThread.asBinder());
            //获取AMS的本地代理类
            final IActivityManager mgr = ActivityManager.getService();
            try {
                //通过Binder调用远端的AMS的attachApplication方法
                mgr.attachApplication(mAppThread, startSeq);
            } catch (RemoteException ex) {
                throw ex.rethrowFromSystemServer();
            }
            // Watch for getting close to heap limit.
            BinderInternal.addGcWatcher(new Runnable() {
                @Override public void run() {
                    if (!mSomeActivitiesChanged) {
                        return;
                    }
                    Runtime runtime = Runtime.getRuntime();
                    long dalvikMax = runtime.maxMemory();
                    long dalvikUsed = runtime.totalMemory() - runtime.freeMemory();
                    if (dalvikUsed > ((3*dalvikMax)/4)) {
                        if (DEBUG_MEMORY_TRIM) Slog.d(TAG, "Dalvik max=" + (dalvikMax/1024)
                                + " total=" + (runtime.totalMemory()/1024)
                                + " used=" + (dalvikUsed/1024));
                        mSomeActivitiesChanged = false;
                        try {
                            ActivityTaskManager.getService().releaseSomeActivities(mAppThread);
                        } catch (RemoteException e) {
                            throw e.rethrowFromSystemServer();
                        }
                    }
                }
            });
        } else {
            //system_server进程会进入该分支
        }

        ViewRootImpl.ConfigChangedCallback configChangedCallback
                = (Configuration globalConfig) -> {
            synchronized (mResourcesManager) {
                // We need to apply this change to the resources immediately, because upon returning
                // the view hierarchy will be informed about it.
                if (mResourcesManager.applyConfigurationToResourcesLocked(globalConfig,
                        null /* compat */)) {
                    updateLocaleListFromAppContext(mInitialApplication.getApplicationContext(),
                            mResourcesManager.getConfiguration().getLocales());

                    // This actually changed the resources! Tell everyone about it.
                    if (mPendingConfiguration == null
                            || mPendingConfiguration.isOtherSeqNewer(globalConfig)) {
                        mPendingConfiguration = globalConfig;
                        sendMessage(H.CONFIGURATION_CHANGED, globalConfig);
                    }
                }
            }
        };
        ViewRootImpl.addConfigCallback(configChangedCallback);
    }

```

##### 1.1.1 得到ActivityManagerService本地Binder

在attach中，如果传入的system参数为false，则表示是app进程，进入相应分支。首先会通过`ActivityManager.getService()`得到ActivityManagerService的本地Binder代理。

```java
    public static IActivityManager getService() {
        return IActivityManagerSingleton.get();
    }

    @UnsupportedAppUsage
    private static final Singleton<IActivityManager> IActivityManagerSingleton =
            new Singleton<IActivityManager>() {
                @Override
                protected IActivityManager create() {
                    final IBinder b = ServiceManager.getService(Context.ACTIVITY_SERVICE);
                    final IActivityManager am = IActivityManager.Stub.asInterface(b);
                    return am;
                }
            };
```

##### 1.1.2 调用ActivityManagerService的attachApplication方法

通过ActivityManagerService的Binder，调用它的attachApplication方法，执行该方法的进程是启动和维护ActivityManagerService的SystemServer进程。

并且我们看到，这里传入的参数为mAppThread，这是一个ApplicationThread类，代表着app进程中的Binder。

```java
  private class ApplicationThread extends IApplicationThread.Stub {
        private static final String DB_INFO_FORMAT = "  %8s %8s %14s %14s  %s";
	...
  }

```

当我们想要访问systemserver中的系统服务时（这里为ActivityManagerService），由于ServiceManager进程作为Service时，它在Binder驱动中的句柄为0，任何Client想要访问ServiceManager，可以直接通过句柄0来进行访问，从而得到AMS的远程代理Binder。因此，从app进程到AMS的消息路线已经打通，但是AMS传递消息到app进程也是一个跨进程通信，需要得到app进程的binder。因此这里直接将ApplicationThread传递到AMS中，用于通信，我们接下来看，是否是这样。

### 2. 来到SystemServer进程中

```java
   //frameworks\base\services\core\java\com\android\server\am\ActivityManagerService.java
	@Override
    public final void attachApplication(IApplicationThread thread, long startSeq) {
        synchronized (this) {
            int callingPid = Binder.getCallingPid();
            final int callingUid = Binder.getCallingUid();
            final long origId = Binder.clearCallingIdentity();
            attachApplicationLocked(thread, callingPid, callingUid, startSeq);
            Binder.restoreCallingIdentity(origId);
        }
    }

```



```java
   
    private final boolean attachApplicationLocked(IApplicationThread thread,
            int pid, int callingUid, long startSeq) {
        //1.获得ProcessRecord
        ProcessRecord app;
        long startTime = SystemClock.uptimeMillis();
        long bindApplicationTimeMillis;
        if (pid != MY_PID && pid >= 0) {
            synchronized (mPidsSelfLocked) {
                app = mPidsSelfLocked.get(pid);
            }
            if (app != null && (app.startUid != callingUid || app.startSeq != startSeq)) {
                String processName = null;
                final ProcessRecord pending = mProcessList.mPendingStarts.get(startSeq);
                if (pending != null) {
                    processName = pending.processName;
                }
                final String msg = "attachApplicationLocked process:" + processName
                        + " startSeq:" + startSeq
                        + " pid:" + pid
                        + " belongs to another existing app:" + app.processName
                        + " startSeq:" + app.startSeq;
                Slog.wtf(TAG, msg);
                // SafetyNet logging for b/131105245.
                EventLog.writeEvent(0x534e4554, "131105245", app.startUid, msg);
                // If there is already an app occupying that pid that hasn't been cleaned up
                cleanUpApplicationRecordLocked(app, false, false, -1,
                            true /*replacingPid*/);
                mPidsSelfLocked.remove(app);
                app = null;
            }
        } else {
            app = null;
        }

        // It's possible that process called attachApplication before we got a chance to
        // update the internal state.
        if (app == null && startSeq > 0) {
            final ProcessRecord pending = mProcessList.mPendingStarts.get(startSeq);
            if (pending != null && pending.startUid == callingUid && pending.startSeq == startSeq
                    && mProcessList.handleProcessStartedLocked(pending, pid, pending
                            .isUsingWrapper(),
                            startSeq, true)) {
                app = pending;
            }
        }


        try {
            ...
            bindApplicationTimeMillis = SystemClock.elapsedRealtime();
            mAtmInternal.preBindApplication(app.getWindowProcessController());
            final ActiveInstrumentation instr2 = app.getActiveInstrumentation();
            if (mPlatformCompat != null) {
                mPlatformCompat.resetReporting(app.info);
            }
            //调用ApplicationThread的runIsolatedEntryPoint或者bindApplication
            if (app.isolatedEntryPoint != null) {
                thread.runIsolatedEntryPoint(app.isolatedEntryPoint, app.isolatedEntryPointArgs);
            } else if (instr2 != null) {
                thread.bindApplication(processName, appInfo, providers,
                        instr2.mClass,
                        profilerInfo, instr2.mArguments,
                        instr2.mWatcher,
                        instr2.mUiAutomationConnection, testMode,
                        mBinderTransactionTrackingEnabled, enableTrackAllocation,
                        isRestrictedBackupMode || !normalMode, app.isPersistent(),
                        new Configuration(app.getWindowProcessController().getConfiguration()),
                        app.compat, getCommonServicesLocked(app.isolated),
                        mCoreSettingsObserver.getCoreSettingsLocked(),
                        buildSerial, autofillOptions, contentCaptureOptions,
                        app.mDisabledCompatChanges);
            } else {
                thread.bindApplication(processName, appInfo, providers, null, profilerInfo,
                        null, null, null, testMode,
                        mBinderTransactionTrackingEnabled, enableTrackAllocation,
                        isRestrictedBackupMode || !normalMode, app.isPersistent(),
                        new Configuration(app.getWindowProcessController().getConfiguration()),
                        app.compat, getCommonServicesLocked(app.isolated),
                        mCoreSettingsObserver.getCoreSettingsLocked(),
                        buildSerial, autofillOptions, contentCaptureOptions,
                        app.mDisabledCompatChanges);
            }
            if (profilerInfo != null) {
                profilerInfo.closeFd();
                profilerInfo = null;
            }
			//ProcessRecord记录下ApplicationThread
            app.makeActive(thread, mProcessStats);
            checkTime(startTime, "attachApplicationLocked: immediately after bindApplication");
            mProcessList.updateLruProcessLocked(app, false, null);
            checkTime(startTime, "attachApplicationLocked: after updateLruProcessLocked");
            app.lastRequestedGc = app.lastLowMemory = SystemClock.uptimeMillis();
        } catch (Exception e) {
            ...
        }

        ...
        return true;
    }
```

attachApplicationLocked方法比较长，主要有三部分

#### 2.1 获得ProcessRecord

首先获取ProcessRecord，ProcessRecord的用于描述进程的数据结构，例如：

1. processName：记录进程名，默认情况下进程名和该进程运行的第一个apk的包名是相同的，当然也可以自定义进程名；
2. pid: 记录进程pid，该值在由进程创建时内核所分配的。
3. thread：执行完attachApplicationLocked()方法，会把客户端进程ApplicationThread的binder服务的代理端传递到 AMS，并保持到ProcessRecord的成员变量thread；
4. info：记录运行在该进程的第一个应用
5. pkgList: 记录运行在该进程中所有的包名，比如通过addPackage()添加
6.  ...

#### 2.2 调用ApplicationThread.bindApplication

```java
        public final void bindApplication(String processName, ApplicationInfo appInfo,
                List<ProviderInfo> providers, ComponentName instrumentationName,
                ProfilerInfo profilerInfo, Bundle instrumentationArgs,
                IInstrumentationWatcher instrumentationWatcher,
                IUiAutomationConnection instrumentationUiConnection, int debugMode,
                boolean enableBinderTracking, boolean trackAllocation,
                boolean isRestrictedBackupMode, boolean persistent, Configuration config,
                CompatibilityInfo compatInfo, Map services, Bundle coreSettings,
                String buildSerial, AutofillOptions autofillOptions,
                ContentCaptureOptions contentCaptureOptions, long[] disabledCompatChanges) {
            if (services != null) {
                // Setup the service cache in the ServiceManager
                ServiceManager.initServiceCache(services);
            }

            setCoreSettings(coreSettings);

            AppBindData data = new AppBindData();
            data.processName = processName;
            data.appInfo = appInfo;
            data.providers = providers;
            data.instrumentationName = instrumentationName;
            data.instrumentationArgs = instrumentationArgs;
            data.instrumentationWatcher = instrumentationWatcher;
            data.instrumentationUiAutomationConnection = instrumentationUiConnection;
            data.debugMode = debugMode;
            data.enableBinderTracking = enableBinderTracking;
            data.trackAllocation = trackAllocation;
            data.restrictedBackupMode = isRestrictedBackupMode;
            data.persistent = persistent;
            data.config = config;
            data.compatInfo = compatInfo;
            data.initProfilerInfo = profilerInfo;
            data.buildSerial = buildSerial;
            data.autofillOptions = autofillOptions;
            data.contentCaptureOptions = contentCaptureOptions;
            data.disabledCompatChanges = disabledCompatChanges;
            sendMessage(H.BIND_APPLICATION, data);
        }
```

这里构造了一个AppBindData对象，然后通过Handler发送消息

```java
 public void handleMessage(Message msg) {
            if (DEBUG_MESSAGES) Slog.v(TAG, ">>> handling: " + codeToString(msg.what));
            switch (msg.what) {
                case BIND_APPLICATION:
                    Trace.traceBegin(Trace.TRACE_TAG_ACTIVITY_MANAGER, "bindApplication");
                    AppBindData data = (AppBindData)msg.obj;
                    handleBindApplication(data);
                    Trace.traceEnd(Trace.TRACE_TAG_ACTIVITY_MANAGER);
                    break;
                 ...
             }
   }
```

handler接收到消息后，执行handleBindApplication方法

```java

 private void handleBindApplication(AppBindData data) {
   //通过反射创建Instrumentation
   try {
       final ClassLoader cl = instrContext.getClassLoader();
       mInstrumentation = (Instrumentation)
       cl.loadClass(data.instrumentationName.getClassName()).newInstance();
    }
	...
     
 	//获得LoadedApk，表示当前加载的apk
   data.info = getPackageInfoNoCheck(data.appInfo, data.compatInfo);
 
   ......
  
   try {
       //创建一个Application对象     
       Application app = data.info.makeApplication(data.restrictedBackupMode,null);
       mInitialApplication = app;
 
       
       try {
          //通过mInstrumentation对象，回调Application的onCreate()方法  
           mInstrumentation.callApplicationOnCreate(app);
       } catch (Exception e) {
            ......
        } finally {
            ......
        }
 
        ......
}
```

##### 2.2.1 创建Application对象

getPackageInfoNoCheck(data.appInfo, data.compatInfo)方法会得到一个LoadedApk对象，赋值给data.info。LoadedApk表示一个当前加载的apk，通过它的makeApplication，创建Application对象

```java
    public Application makeApplication(boolean forceDefaultAppClass,
            Instrumentation instrumentation) {
        if (mApplication != null) {
            return mApplication;
        }
		...

        Application app = null;

        String appClass = mApplicationInfo.className;
        if (forceDefaultAppClass || (appClass == null)) {
            appClass = "android.app.Application";
        }

        try {
            java.lang.ClassLoader cl = getClassLoader();
            if (!mPackageName.equals("android")) {
                Trace.traceBegin(Trace.TRACE_TAG_ACTIVITY_MANAGER,
                        "initializeJavaContextClassLoader");
                initializeJavaContextClassLoader();
                Trace.traceEnd(Trace.TRACE_TAG_ACTIVITY_MANAGER);
            }
            //创建ContextImpl实例
            ContextImpl appContext = ContextImpl.createAppContext(mActivityThread, this);
            //创建Application实例
            app = mActivityThread.mInstrumentation.newApplication(
                    cl, appClass, appContext);
            appContext.setOuterContext(app);
        } catch (Exception e) {
            if (!mActivityThread.mInstrumentation.onException(app, e)) {
                Trace.traceEnd(Trace.TRACE_TAG_ACTIVITY_MANAGER);
                throw new RuntimeException(
                    "Unable to instantiate application " + appClass
                    + ": " + e.toString(), e);
            }
        }
        mActivityThread.mAllApplications.add(app);
        mApplication = app;

        if (instrumentation != null) {
            //这里不会执行，因为传递来的instrumentation为null
            ...
        }

   		...
        return app;
    }
```



```java
    public Application newApplication(ClassLoader cl, String className, Context context)
            throws InstantiationException, IllegalAccessException, 
            ClassNotFoundException {
        Application app = getFactory(context.getPackageName())
                .instantiateApplication(cl, className);
        app.attach(context);
        return app;
    }
```

getFactory得到一个AppComponentFactory对象，然后调用它的instantiateApplication方法创建Application对象，默认的AppComponentFactory的实现为：

```java
    public @NonNull Service instantiateService(@NonNull ClassLoader cl,
            @NonNull String className, @Nullable Intent intent)
            throws InstantiationException, IllegalAccessException, ClassNotFoundException {
        return (Service) cl.loadClass(className).newInstance();
    }

```

直接通过反射创建实例。然后调用Application.attach方法，即Application中的attachBaseContext(context);

##### 2.2.2 调用Application的onCreate

回到ActivityThread中的handleBindApplication方法中，在得到Application后，会通过mInstrumentation.callApplicationOnCreate(app)回调Application的onCreate方法：

```
    public void callApplicationOnCreate(Application app) {
        app.onCreate();
    }
```

该方法直接调用app.onCreate();

#### 2.3  ProcessRecord存储ApplicationThread

在ActivityManagerService中，最终还是绕回到app进程中，执行创建Application等一系列工作。而ApplicationThread将会被ProcessRecord存下来。

```java
 public void makeActive(IApplicationThread _thread, ProcessStatsService tracker) {
        if (thread == null) {
            final ProcessState origBase = baseProcessTracker;
            if (origBase != null) {
                origBase.setState(ProcessStats.STATE_NOTHING,
                        tracker.getMemFactorLocked(), SystemClock.uptimeMillis(), pkgList.mPkgList);
                for (int ipkg = pkgList.size() - 1; ipkg >= 0; ipkg--) {
                    StatsLog.write(StatsLog.PROCESS_STATE_CHANGED,
                            uid, processName, pkgList.keyAt(ipkg),
                            ActivityManager.processStateAmToProto(ProcessStats.STATE_NOTHING),
                            pkgList.valueAt(ipkg).appVersion);
                }
                origBase.makeInactive();
            }
            baseProcessTracker = tracker.getProcessStateLocked(info.packageName, info.uid,
                    info.longVersionCode, processName);
            baseProcessTracker.makeActive();
            for (int i=0; i<pkgList.size(); i++) {
                ProcessStats.ProcessStateHolder holder = pkgList.valueAt(i);
                if (holder.state != null && holder.state != origBase) {
                    holder.state.makeInactive();
                }
                tracker.updateProcessStateHolderLocked(holder, pkgList.keyAt(i), info.uid,
                        info.longVersionCode, processName);
                if (holder.state != baseProcessTracker) {
                    holder.state.makeActive();
                }
            }
        }
        thread = _thread;
        mWindowProcessController.setThread(thread);
    }

```

ProcessRecord中的thread字段，记录着app进程作为远程服务端时提供的Binder代理对象

#### 2.4 启动Activity

在AMS的attachApplicationLocked方法中，在创建Application并且调用它的生命周期方法，以及记录下app进程的ApplicationThread后，接下来会做什么呢？

我们看到attachApplicationLocked下面的代码：

```java
 private final boolean attachApplicationLocked(IApplicationThread thread,
            int pid, int callingUid, long startSeq) {
  
  // See if the top visible activity is waiting to run in this process...
        if (normalMode) {
            try {
                didSomething = mAtmInternal.attachApplication(app.getWindowProcessController());
            } catch (Exception e) {
                Slog.wtf(TAG, "Exception thrown launching activities in " + app, e);
                badApp = true;
            }
        }

        // Find any services that should be running in this process...
        if (!badApp) {
            try {
                didSomething |= mServices.attachApplicationLocked(app, processName);
                checkTime(startTime, "attachApplicationLocked: after mServices.attachApplicationLocked");
            } catch (Exception e) {
                Slog.wtf(TAG, "Exception thrown starting services in " + app, e);
                badApp = true;
            }
        }

        // Check if a next-broadcast receiver is in this process...
        if (!badApp && isPendingBroadcastProcessLocked(pid)) {
            try {
                didSomething |= sendPendingBroadcastsLocked(app);
                checkTime(startTime, "attachApplicationLocked: after sendPendingBroadcastsLocked");
            } catch (Exception e) {
                // If the app died trying to launch the receiver we declare it 'bad'
                Slog.wtf(TAG, "Exception thrown dispatching broadcasts in " + app, e);
                badApp = true;
            }
        }

        // Check whether the next backup agent is in this process...
        if (!badApp && backupTarget != null && backupTarget.app == app) {
            if (DEBUG_BACKUP) Slog.v(TAG_BACKUP,
                    "New app is backup target, launching agent for " + app);
            notifyPackageUse(backupTarget.appInfo.packageName,
                             PackageManager.NOTIFY_PACKAGE_USE_BACKUP);
            try {
                thread.scheduleCreateBackupAgent(backupTarget.appInfo,
                        compatibilityInfoForPackage(backupTarget.appInfo),
                        backupTarget.backupMode, backupTarget.userId);
            } catch (Exception e) {
                Slog.wtf(TAG, "Exception thrown creating backup agent in " + app, e);
                badApp = true;
            }
        }

        if (badApp) {
            app.kill("error during init", true);
            handleAppDiedLocked(app, false, true);
            return false;
        }

        if (!didSomething) {
            updateOomAdjLocked(OomAdjuster.OOM_ADJ_REASON_PROCESS_BEGIN);
            checkTime(startTime, "attachApplicationLocked: after updateOomAdjLocked");
        }

        return true;
    }
```



##### 2.4.1 mAtmInternal.attachApplication

mAtmInternal的类型是ActivityTaskManagerInternal，它是一个抽象类，具体的实现是ActivityTaskManagerService.LocalService。这里首先调用了它的attachApplication方法：

```java
        public boolean attachApplication(WindowProcessController wpc) throws RemoteException {
            synchronized (mGlobalLockWithoutBoost) {
                return mRootActivityContainer.attachApplication(wpc);
            }
        }
```



