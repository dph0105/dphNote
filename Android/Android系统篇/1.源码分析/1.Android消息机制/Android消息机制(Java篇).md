### 一、Android消息机制介绍

Android的消息机制是Android的一个重要的功能，它属于进程内部的一种通信方式。利用Android消息机制我们可以做到的功能主要有：1. 任务的延迟执行 2. 将任务放在其他线程执行而不是当前线程（即线程与线程的通信）

主要涉及到的 **Handler 、 Looper 、MessageQueue 、 Message** 这几个类。

- Handler：处理者，用于消息的发送（sendMessage）以及处理（handleMessage）
- Looper： 循环者，Java线程在执行完它的run方法后就会死亡，而Looper可以使线程存活，它实际上是一个死循环，不断的从MessageQueue中取出Message，并发送给相应的Handler
- MessageQueue：消息队列，内部存储了一组消息，以队列的形式对外提供向消息池投递消息(enqueueMessage)和取走消息(next)的工作，内部采用单链表的数据结构来存储消息列表。
- Message：消息。

消息机制的整体流程如图：

![](/imgs/Android消息机制流程图.webp)

#### 1.1 总结

**把总结写在前面，方便快速理解。**

Android的消息机制是Android进程内，即线程间的一种通讯方式。它可以用于使任务延迟执行或者是改变任务执行的线程。

使用Handler的前提是需要线程中存在Looper，即必须在普通线程中调用[Looper#prepare](#2. prepare)创建线程中的Looper，并且调用[Looper#loop](#3. loop)开启消息循环，而在Android主线程中，或者HandlerThread中已经调用了这两个方法。Looper#loop方法会开启一个死循环，不断调用[MessageQueue#next](#3. next)方法，next方法同样会进入一个死循环中，不断调用nativePollOnce，nativePollOnce是一个JNI方法，它会进入阻塞状态，直到超时或者被` nativeWake`方法唤醒。当被唤醒之后，若MessageQueue被设置同步屏障，则取异步消息，否则取同步消息（即普通的消息）。若没有消息则进入下一次循环重新阻塞，若有消息，判断是否到了执行的时间，可以执行则直接返回跳出循环。若还没有可以执行，则设置阻塞超时时间，进入下一次循环。在进入下一次循环之前，首先会判断是否有闲时任务的存在，若不存在，则直接进入next的下一次循环；若存在，则遍历调用IdleHandler#queueIdle方法进行处理，并设置nativePollOnce的超时时间为0，即不阻塞，并进入next的下一次循环。

Handler通过[Handler#sendMessage](#2.1 sendMessage)或者[Handler#post](#2.2 post)发送一个Message，在Handler#enqueueMessage方法中会设置Message的target为当前的Handle。然后调用[MessageQueue#enqueueMessage](#2. enqueueMessage)将Message按照执行时间排序加入到消息队列中，然后调用` nativeWake(mPtr)`唤醒 MessageQueue#next死循环中阻塞的nativePollOnce。

Looper#loop的循环中获取到了Message后最终通过Message.target即发送Message的Handler的 [Handler#dispatchMessage](#1. dispatchMessage)方法分发任务交给Handler#handleMessage或Handler#handleCallback到Handler所在的线程执行。





#### 1.2 简单使用

```java
    class LooperThread extends Thread{
        Handler mHandler;
        @Override
        public void run() {
            //创建Looper
            Looper.prepare();
            //需要在Looper.loop之前创建Handler，因为loop已经开启了死循环
            mHandler = new Handler(){
                @Override
                public void handleMessage(@NonNull Message msg) {
                    super.handleMessage(msg);
                }
            };
            //开启消息循环
            Looper.loop();
        }
    }
```

创建了LooperThread后，可以在其他线程中调用LooperThread中的sendMessage 或者 post 等方法发送消息

```java
     mHandler.sendMessage(Message.obtain());
     mHandler.post(new Runnable() {
         @Override
         public void run() {
                    
         }
     });
```





### 二、源码分析

#### 2.1 Message

Message是对消息的分装，先来看一下Message中比较重要的成员变量

```java
public final class Message implements Parcelable {

    //what arg1 arg2 data是经常用到的传递参数的对象
    public int what;
    public int arg1; 
    public int arg2;
 	Bundle data;
    /** If set message is asynchronous */
    static final int FLAG_ASYNCHRONOUS = 1 << 1;
	//表示消息分发的时间，基于SystemClock#uptimeMillis，从开机开始计时的时间，不包含系统休眠的时间
    long when;
    //表示消息发送的目标Handler，即发送给哪个Handler
    Handler target;
    //表示执行的回调，即发送的不是消息，而是任务
    Runnable callback;
    //Message可以作为单向链表的item，next指向下一个Message
    Message next;
    
    //Message池相关
    private static final Object sPoolSync = new Object();
    private static Message sPool; //消息池链表的头
    private static int sPoolSize = 0;//消息池链表的大小
    private static final int MAX_POOL_SIZE = 50;//默认最大大小

    ......
}

```

##### 1. 消息池

Message引入了消息池，这样的好处是，当消息池不为空时，可以直接从消息池中获取Message对象，而不是直接创建，提高效率。类似于设计模式中享元模式这么一个概念。

因为有了消息池，所以需要通过Handler#sendMessage发送消息时，可以直接调用Message#obtain获取Message对象，以及发送完消息之后调用Message#recycle回收消息

##### 2. Message#obtain

```java
public static Message obtain() {
    synchronized (sPoolSync) {
        if (sPool != null) {
            Message m = sPool;//取到当前链表的头
            sPool = m.next;//设置消息链表的头为下一个
            m.next = null;//断开消息链
            m.flags = 0; // clear in-use flag
            sPoolSize--;
            return m;
        }
    }
    return new Message();
}
```

obtain方法从消息池取Message，都是把消息池表头的Message取走，再把表头指向next;

obtain还有许多重载方法，基本都是为了提高效率以及使用方便。

##### 3. Message#recycle

```java
    public void recycle() {
        if (isInUse()) {//判断消息是否正在使用
            if (gCheckRecycle) {
                throw new IllegalStateException("This message cannot be recycled because it "
                        + "is still in use.");
            }
            return;
        }
        recycleUnchecked();
    }

    void recycleUnchecked() {
        //设置消息标志位位IN_USE表示不再使用，并清空所有参数
        flags = FLAG_IN_USE;
        what = 0;
        arg1 = 0;
        arg2 = 0;
        obj = null;
        replyTo = null;
        sendingUid = UID_NONE;
        workSourceUid = UID_NONE;
        when = 0;
        target = null;
        callback = null;
        data = null;

        synchronized (sPoolSync) {//若消息池没有满，则加入消息池链表中
            if (sPoolSize < MAX_POOL_SIZE) {
                next = sPool;
                sPool = this;
                sPoolSize++;
            }
        }
    }
```

回收消息的过程就是将Message加入到消息池链表中，并设置为表头

#### 2.2 Handler

Handler是消息处理者，用来发送消息以及接收消息进行处理

##### 1. Handler#init

```java
//这种方法已经废弃
public Handler() {
    this(null, false);
}

public Handler(Callback callback, boolean async) {
	...
    mLooper = Looper.myLooper();
    if (mLooper == null) {
        throw new RuntimeException(
            "Can't create handler inside thread " + Thread.currentThread()
                    + " that has not called Looper.prepare()");
    }
    mQueue = mLooper.mQueue;
    mCallback = callback;
    mAsynchronous = async;
}
```
早先的Handler的无参构造器中，首先会判断Looper对象是否存在。因此，在一个普通的线程中要想使用Handler，必须现调用Looper#prepare，Android应用的主线程，已经调用了Looper#prepare。

至于最新的推荐使用的Handler构造器，需要我们传入一个Looper对象。

```java
    public Handler(@NonNull Looper looper) {
        this(looper, null, false);
    }

    public Handler(@NonNull Looper looper, @Nullable Callback callback) {
        this(looper, callback, false);
    }

    public Handler(@NonNull Looper looper, @Nullable Callback callback, boolean async) {
        mLooper = looper;
        mQueue = looper.mQueue;
        mCallback = callback;
        mAsynchronous = async;
    }
```

Handler的构造方法还允许我们传入一个Callback参数，它可以避免我们必须创建Handler子类实现handleMessage方法

```java
    public interface Callback {
        boolean handleMessage(@NonNull Message msg);
    }
```



##### 2. 发送消息

Handler主要的使用就是发送消息（sendMessage），以及使任务在Handler所在的线程执行（post）

###### 2.1 sendMessage

```java
public final boolean sendMessage(@NonNull Message msg) {
    return sendMessageDelayed(msg, 0);
}

public final boolean sendMessageDelayed(@NonNull Message msg, long delayMillis) {
    if (delayMillis < 0) {
        delayMillis = 0;
    }
    //这里传入的时间为开机到当前的时间（不包括系统休眠的时间）+ 设置的延时时间
    return sendMessageAtTime(msg, SystemClock.uptimeMillis() + delayMillis);
}

public boolean sendMessageAtTime(@NonNull Message msg, long uptimeMillis) {
    MessageQueue queue = mQueue;
    if (queue == null) {
        RuntimeException e = new RuntimeException(
                this + " sendMessageAtTime() called with no mQueue");
        Log.w("Looper", e.getMessage(), e);
        return false;
    }
    return enqueueMessage(queue, msg, uptimeMillis);
}
```
###### 2.2 post

```java
	public final boolean post(@NonNull Runnable r) {
   		return  sendMessageDelayed(getPostMessage(r), 0);
	}

    private static Message getPostMessage(Runnable r) {
        Message m = Message.obtain();
        m.callback = r;
        return m;
    }
```

post实际上也是调用了`sendMessageDelayed`，传入的Runnable被设置为Message的callback。

###### 2.3 enqueueMessage

```java
private boolean enqueueMessage(@NonNull MessageQueue queue, @NonNull Message msg,
        long uptimeMillis) {
    msg.target = this; //设置Message的target为当前的Handler
    msg.workSourceUid = ThreadLocalWorkSource.getUid();

    if (mAsynchronous) {
        msg.setAsynchronous(true);
    }
    return queue.enqueueMessage(msg, uptimeMillis);
}
```

在enqueueMessage这一步，设置了Message的target为当前的Handler，确定了Message的目标。

最终会通过MessageQueue的enqueueMessage将Message加入消息队列中

##### 3 处理消息

消息的处理最终会调用Handler#dispatchMessage和handleMessage

###### 1. dispatchMessage

```java
public void dispatchMessage(@NonNull Message msg) {
    if (msg.callback != null) {//判断Message是否设置了callback，有则执行handleCallback
        handleCallback(msg);
    } else {
        //在构造器中若设置了Handler的Callback，则走这里
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

dispatchMessage就是为了找到消费Message的地方，逻辑这里就不再赘述了。

#### 2.3 Looper

在[Handler#init](#1. Handler#init)这一节中，我们知道了使用Handler必须传入Looper对象，或者当前线程已经存在Looper对象。创建Looper需要调用Looper#prepar

##### 1. Looper#init

```java
private Looper(boolean quitAllowed) {
    mQueue = new MessageQueue(quitAllowed);
    mThread = Thread.currentThread();
}
```

Looper的构造器中，创建了MessageQueue对象以及获取了当前的线程Thread对象作为自己的成员变量mQueue和mThread。

Looper的构造方法是私有的，要想创建Looper，必须使用prepare方法

##### 2. prepare

```java
public static void prepare() {
    prepare(true);
}

static final ThreadLocal<Looper> sThreadLocal = new ThreadLocal<Looper>();

private static void prepare(boolean quitAllowed) {
    if (sThreadLocal.get() != null) {
        throw new RuntimeException("Only one Looper may be created per thread");
    }
    sThreadLocal.set(new Looper(quitAllowed));
}
```
prepare的作用就是创建一个Looper对象，并存放到当前线程的sThreadLocal当中。prepare方法只允许调用一次，若`sThreadLocal.get() != null`，则会抛出异常，即每个线程只能存在一个Looper

##### 3. loop

```java
public static void loop() {
    //myLooper()会从sThreadLocal中取出Looper对象
    final Looper me = myLooper();
    if (me == null) {
        throw new RuntimeException("No Looper; Looper.prepare() wasn't called on this thread.");
    }
    //获取Looper中的MessageQueue对象
    final MessageQueue queue = me.mQueue;
	......
    for (;;) { //进入loop循环，是一个死循环
        //获取消息队列中的消息，可能会阻塞
        Message msg = queue.next();
        if (msg == null) {
            return;
        }
        try {
            //分发消息
            msg.target.dispatchMessage(msg);
        } finally {
            ......
        }
       //回收消息
        msg.recycleUnchecked();
    }
}
```
Looper#loop方法使当前线程进入死循环当中，不断调用MessageQueue#next取出Message，并通过Message.target分发消息，最后回收Message，直到取出的Message为null。在[enqueueMessage](#2.3 enqueueMessage)这一小节中，我们知道哪个Handler发送的Message，Message.target就是那个Handler。

#### 2.4 MessageQueue

MessageQueue除了用作存取消息的链表之外，还是连接Java和native之间的纽带，大部分核心的方法，都交给了native层去处理。本篇文章，我们只关心java方法，对于一些native方法只会说明其作用，具体的源码解析将会放在下一篇再进行分析。

##### 1. MessageQueue#init

```java
MessageQueue(boolean quitAllowed) {
    mQuitAllowed = quitAllowed;
    mPtr = nativeInit();//nativeInit会在native层创建NativeMessageQueue，并返回它的地址，即mPtr
}
```

MessageQueue的创建需要传入一个布尔参数quitAllowed，这个参数用来判断消息循环能否停止。MessageQueue是在Looper的构造方法中被初始化的，quitAllowed也是此时被传入的。当我们调用默认的无参方法prepare时，quitAllowed为false，而Android应用的入口ActivityThread#main方法中，调用的是prepareMainLooper方法，这里会传入quitAllowed为false，即不能停止。

##### 2. enqueueMessage

无论是Handler的sendMessage还是post，最终都会执行到MessageQueue的enqueueMessage方法，即将消息加入到消息队列中

```java
boolean enqueueMessage(Message msg, long when) {
    if (msg.target == null) {
        throw new IllegalArgumentException("Message must have a target.");
    }
    if (msg.isInUse()) {
        throw new IllegalStateException(msg + " This message is already in use.");
    }

    synchronized (this) {
        if (mQuitting) {
            //如果正在关闭，则释放Message
            msg.recycle();
            return false;
        }
        msg.markInUse();
        msg.when = when;
        Message p = mMessages;//获取MessageQueue持有的Message队列的头
        boolean needWake;
        if (p == null || when == 0 || when < p.when) {
            // 若当前的消息队列的头为null，或者执行时间为0，或者执行时间要比消息头的执行时间早
            // 设置新的消息队列的头
            msg.next = p;
            mMessages = msg;
            //mBlocked表示是否是阻塞状态，若是，则下面需要唤醒底层的eventfd
            needWake = mBlocked;
        } else {
            // p.target == null即存在同步屏障消息
            // msg.isAsynchronous()即消息是异步消息，这种情况才需唤醒
            needWake = mBlocked && p.target == null && msg.isAsynchronous();
            //将消息按时间顺序插入到MessageQueue。
            Message prev;
            for (;;) {
                prev = p;
                p = p.next;
                if (p == null || when < p.when) {
                    break;
                }
                if (needWake && p.isAsynchronous()) {
                    needWake = false;
                }
            }
            msg.next = p; // invariant: p == prev.next
            prev.next = msg;
        }
		//唤醒nativePollOnce
        if (needWake) {
            nativeWake(mPtr);
        }
    }
    return true;
}
```

enqueueMessage方法将Message加入到消息队列中，并判断了是否唤醒在next方法中阻塞的nativePollOnce方法。

若消息的执行时间小于当前消息头的时间，则设置该消息为新的消息头；否则，消息的执行时间顺序添加消息

**是否唤醒阻塞nativePollOnce方法的判断：**

若是新的消息头，并当前为阻塞状态，则唤醒（为新的消息头说明需要马上处理）

若不是消息头，则需要同时满足，当前状态为阻塞状态，且设置了同步屏障，并且新加的消息为异步消息（按顺序加入队列本来应该不用唤醒，但是若是设置了同步屏障，需要优先处理异步消息，所以这时候要唤醒。）

##### 3. next

要想从MessageQueue取出Message，需要调用它的next方法，在[Looper#loop](#3. loop)这一节中，我们看到loop在死循环中不断调用该方法：

```java
Message next() {
    final long ptr = mPtr;
    //ptr为native层NativeMessageQueue的地址，若等于0说明已经退出
    if (ptr == 0) {
        return null;
    }
	//等待中的闲时执行的Handler的数量，初始值为-1
    int pendingIdleHandlerCount = -1;
    int nextPollTimeoutMillis = 0;
    for (;;) {// next操作也是死循环
        if (nextPollTimeoutMillis != 0) {
            Binder.flushPendingCommands();
        }
		//阻塞操作，当超时nextPollTimeoutMillis后，或者调用了nativeWake被唤醒之后都会返回
        nativePollOnce(ptr, nextPollTimeoutMillis);
		//唤醒之后
        synchronized (this) {
            final long now = SystemClock.uptimeMillis();
            Message prevMsg = null;
            Message msg = mMessages;//sMessages即当前消息队列的头部
            //msg.target==null说明是设置了同步屏障，那么循环找到下一个异步消息，同步消息则不能被发出，直到撤销同步屏障
            if (msg != null && msg.target == null) {
                do {
                    prevMsg = msg;
                    msg = msg.next;
                } while (msg != null && !msg.isAsynchronous());
            }
            if (msg != null) {
                if (now < msg.when) {
                    //若当前时间小于消息的执行时间，说明消息还没准备好执行，设置下一次超时唤醒的时间
                    nextPollTimeoutMillis = (int) Math.min(msg.when - now, Integer.MAX_VALUE);
                } else {
                    mBlocked = false;//设置当前非阻塞
                    //设置下一个Message为表头
                    if (prevMsg != null) {
                        prevMsg.next = msg.next;
                    } else {
                        mMessages = msg.next;
                    }
                    msg.next = null;
                    if (DEBUG) Log.v(TAG, "Returning message: " + msg);
                    msg.markInUse();
                    //返回消息
                    return msg;
                }
            } else {
                //因为没有Message，设置-1，即没有超时时间，一直睡，直到被主动唤醒，即调用nativeWake
                nextPollTimeoutMillis = -1;
            }
            if (mQuitting) {
                //正在停止，关闭NativeMessageQueue，并返回null
                dispose();
                return null;
            }
            //走到这一步，说明MessageQueue没有要处理的消息，即线程空闲下来了，这里就是Handler闲时机制
            if (pendingIdleHandlerCount < 0
                    && (mMessages == null || now < mMessages.when)) {
                //这里又一次判断了当消息队列没有消息，或者消息的执行时间还到时
                //设置要处理的空闲任务的数量
                pendingIdleHandlerCount = mIdleHandlers.size();
            }
            //小于等于0说明没有要处理的空闲任务，重新设置阻塞状态，并进入下一轮循环
            if (pendingIdleHandlerCount <= 0) {
                mBlocked = true;
                continue;
            }

            if (mPendingIdleHandlers == null) {
                mPendingIdleHandlers = new IdleHandler[Math.max(pendingIdleHandlerCount, 4)];
            }
            mPendingIdleHandlers = mIdleHandlers.toArray(mPendingIdleHandlers);
        }
        //循环处理所有的闲时任务
        for (int i = 0; i < pendingIdleHandlerCount; i++) {
            final IdleHandler idler = mPendingIdleHandlers[i];
            mPendingIdleHandlers[i] = null; // 释放数组中的引用

            boolean keep = false;
            try {
                //调用queueIdle方法，即我们实现的方法
                keep = idler.queueIdle();
            } catch (Throwable t) {
                Log.wtf(TAG, "IdleHandler threw exception", t);
            }
			//queueIdle返回true时，则不移除闲时任务，返回false，则移除
            if (!keep) {
                synchronized (this) {
                    mIdleHandlers.remove(idler);
                }
            }
        }
     	//设置闲时任务数量为0
        pendingIdleHandlerCount = 0;
		//处理完闲时任务，可能已经有消息准备好了，所以设置超时时间为0，直接唤醒。
        nextPollTimeoutMillis = 0;
    }
}
```

next方法可以说Android消息机制最核心的方法了。

next方法会通过死循环来获取消息，首先调用nativePollOnce进入阻塞状态，一开始的nextPollTimeoutMillis为0，即马上超时被唤醒

 1. 此时若没有消息（即mMessage == null），则设置nextPollTimeoutMillis为-1，当下一次循环调用nativePollOnce时，就会一直阻塞，直到被唤醒（在上一小节[MessageQueue#enqueueMessage](#2. enqueueMessage)方法中，向消息队列加入消息后，最后会调用nativeWake方法唤醒nativePollOnce）。

 2. 若有消息首先会判断是否存在同步屏障

     	1. 若有同步屏障，即MessageQueue的mMessage.target==null，此时会遍历消息队列，若存在异步消息，则取该消息，若不存在，就设置nextPollTimeoutMillis为-1
     	2. 若不存在同步屏障则直接使用该Message。

    得到Message之后，首先判断Message是否可以执行了，即Message.when 是否大于**当前时间 now**，若大于，则设置MessageQueue新的队列头部为下一条消息，**并且next方法返回该Message**。否则设置nextPollTimeoutMillis为Message.when - now，即下一次超时唤醒的时间。

若没有消息返回则会处理IdleHandler，即闲时任务。

若闲时任务数量不大于0，即没有闲时任务时，进入下一次循环。

否则就会循环调用IdleHandler#queueIdle，执行闲时任务，queueIdle返回true时，则不移除闲时任务，返回false，则移除。

当处理完闲时任务，此时可能过去了一段时间，可能已经有一下个Message准备好了，所以设置nextPollTimeoutMillis为0，下一次循环调用nativePollOnce直接超时唤醒，不阻塞。