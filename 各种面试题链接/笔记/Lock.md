## 关于锁

* Mutex: 互斥体
* Guard：警卫
* contender：竞争者
* recursive：递归

## 原子操作

循环变量如何标记

Extern "C" 作用

编译

JIT：just in time

AOT： ahead of time

一个类如何加载到内存

ART中函数栈的数据结构

## CAS

CAS的意思是compare and set，比较并交换，他的机制是通过原子性将内存中的值与指定值进行比较，当数据一样时将内存的中的数据替换为新的值。在Android中Atomic开头的类实现了CAS机制，这些类都是通过Unsafe类来实现的。在Art虚拟机中，Unsafe的原子操作通过c++中std::atomic来实现的，其中内存模型指定了序列一致性。

## volatile  [ˈvɒlətaɪl]

声明为volatile的字段有可见性和有序性2种特性。在Art虚拟机中，在解释执行模式时，读写volatile字段时（生成iget,iput指令）最终会调用ArtField通过atmoic和序列一致性的memory_order来操作变量地址保证以上特性；而在机器码执行模式时，在dex2oat阶段会

## Synchronize [ˈsɪŋkrənaɪz]

声明为synchronize方法或代码段会在生成字节码时在代码块头尾加上MoniterEnter指令和MoniterExit指令。

ART虚拟机中每个对象都有一个名为moniter_的unit32_t字段，该字段在使用时会被转化为LockWord类，LockWord类用来表示当前对象锁的状态、hashcode、转发地址等等

### synchronize native func();

本地方法声明了synchronize标志的话，dex文件中会将方法的access_flag设置上SYNCHRONIZED,DECLARED_SYNCHRONIZED标志。

Native在执行之前都会先执行JniMethodStart方法，执行之后会执行JniMethodEnd方法。如果一个native方法声明synchronize标志的话，那么在调用JniMethodStart之前会先调用moniterEnter方法，在jniMethodEnd之后会调用moniterExit方法。

#### MoniterEnter 

当ART虚拟机执行到MoniterEnter指令时，会执行Moniter类的静态方法MoniterEnter,MoniterEnter方法执行时会先判断监控对象的LockWord的状态：当状态是未锁住时，会将当前线程id写入LockWork，使其状态变成瘦锁退出；

当状态是瘦锁时，判断当前线程Id是否和瘦锁状态存入的线程Id一致，一致的话判断瘦锁重入数量是否大于2^12-1，小于直接将值+1退出；否则会将锁膨胀成fat胖锁并使用互斥锁请求锁；当线程不一致时，会将锁膨胀成fat胖锁并使用互斥锁请求锁；

瘦锁膨胀成胖锁时，当执行线程和LockWord持有的线程Id是不一致的话，会先请求LockWord持有的线程Id对应的线程设置为suspend挂起状态（通过设置tls32.state_and_flag.falg设置为kSuspendRequest），在线程成功被挂机后，

执行线程会创建一个Moniter对象，然后将moniter对象转化为胖锁结构更新LockWord；Moniter对象中有一个互斥锁，再结构更新成胖锁成功后会先调用互斥锁的Lock方法请求锁阻塞。

当状态一键是fat胖锁时，会直接调用monitor对象的Lock方法请求锁阻塞。

当状态是hashcode时，直接将锁转为胖锁。

#### MoniterExit

当LockWord状态是瘦锁时，如果减到0了，就将LockWord中的LockCount和ThreadId请0刷新，否则将瘦锁重入计数减一刷新；

当LockWord状态是胖锁时，会调用Moniter对象的Unlock方法，Unlock方法中会判断lock_count_是不是0，不是0将lock_count - 1退出；是0的话会调用moniter_lock_.Unlock方法释放锁，同时将signal通知其他线程等待的Waiter条件锁；

胖锁的放气操作不会在MoniterExit执行（为了性能效率），而是在堆的垃圾回收时，会将无法访问到的对象的moniter对象析构释放

### wait

java中执行wait方法时，ART虚拟机会调用Moniter.wait方法

wait方法会先获取对象的LockWord的状态是不是胖锁，如果状态是未锁住状态，报错；如果是瘦锁状态，会先将瘦锁膨胀为fat胖锁，然后调用胖锁中Moniter对象的Wait方法；

Moniter.wait方法执行，会将当前thread对象添加到moniter对象的链表尾部，然后设置thread的wait_monitor为当前moniter对象，然后释放当前持有的互斥锁并唤醒其他线程的Waiter，然后执行thread.wait_cond_条件锁的Wait方法阻塞等待;

ConditoinVariable::TimeWait:调用futex进行等待

当其他线程执行对象的wait/notify方法时，wait会被有序的唤醒，唤醒时会先将当前线程的wait_monitor对象设置为null,然后判断是否被interrupted打断，如果被打断thread会调用ThrowNewException跑出打断interruptException打断异常，最后会将当前thread从moniter的wait_set移除。

### notify

java调用notify方法，ART虚拟机会执行moniter.notify方法；

notify方法会先判断当前是否持有锁。然后将wait_set_等待线程链表中第一个Thread移除，添加到wake_set_中。这样在MONITOR_EXIT指令执行时会从wake_set中获取thread进行唤醒

### notifyAll

java调用notifyAll方法，ART虚拟机会执行moniter.notifyALl方法

notifyAll方法会先判断当前是否持有锁，然后将wait_set等待线程链表所有元素移除并添加到wake_set_等待唤醒队列中。这样在MONITOR_EXIT指令执行时会从wake_set中获取thread进行唤醒

### Thread.sleep

java调用Thread.sleep方法时，会将Thread.lock当成监控对象传递到线程中,调用ART的sleep方法，sleep方法会将Thrad.lock当成参数直接调用Moniter.wait静态方法。

### Thread.jon

调用Thread.lock.wait()方法，当join的线程退出时会调用lock.notifyAll唤起等待线程

## Lock

### AbstractQueueSynchronizer

AQS内部有一个volatile的int类型state，onwer持有线程对象和一个Node链表。

当一个线程要申请互斥锁时，会先尝试用CAS操作state从0变成1，成功的话就将AQS的owner赋值为当前线程，否则会生成一个互斥状态（nextWaiter）的Node添加到AQS的tail尾部，然后将head的状态改为Node,SIGNAL并挂起线程。

当一个线程要释放锁时，会将state值减去要释放的数量，当state值的变成0时，会将AQS.owner赋值为null,然后将head等待状态改为0，并将head之后不是取消状态的后继结点恢复线程，线程恢复后会尝试申请锁，成功的话会将AQS.owner设置为当前线程，并将head移除并将当前node设置head。

##### ConditionObject

内部有firstWaiter和lastWaiter2个Node类型的属性。

当一个线程调用await等待时，会new一个状态为CONDITION，thread是当前线程的Node结点添加到ConditionObject的waiter链接中，然后将当前AQS.state保存并释放当前线程持有的锁，之后再挂起当前线程，等待其他线程调用ConditionObject.signal。

当一个线程调用awaitNanos时，会new一个状态为CONDITION，thread是当前线程的Node结点添加到ConditionObject的waiter链接中，然后将当前AQS.state保存并释放当前线程持有的锁，之后再挂起当前线程，等待其他线程调用,当等待时间超时时，会将当前node的状态改为0，添加到AQS.tail链表尾部，然后再挂起线程等待。

当一个线程调用signal时，会从ConditionObject的waiter链表头取出一个状态时Node，然后尝试用CAS修改node的状态为0，成功就将node添加到AQS.tail尾部，失败返回。

### ReentrantLock  [riː'entrənt] 

可重入锁本质是通过AQS类来实现的

### Mutex C++ 

mutextLock内部有一个原子变量state_and_contenders_对象和exclusive_owner_线程id原子对象和recursive_递归锁对象，state_and_contentders的第1位用于表示当前是否有线程持有锁，其余位数用于表示竞争者的数量，recursive__表示当前锁被递归次数

#### futex

futex对象是一个32位的值，其地址提供给系统调用futex().futex对象在所有平台都是32位。所有futex操作都受这个值(futex)管理。为了在进程间共享一个futex值，futex通常放在共享内存中。这导致在不同的进程中，futex有不一样的虚拟地址的值，不过其指向的物理地址都是一样的。在多线程的程序中，只需将futex值作为全局变量即可被所有线程共享。

#### FUTEX_WAIT(since Linux 2.6.0)

这个是futex_op参数上一种选择，即如果uaddr上所指向的futex值和val值相同，则将当前进程/线程会阻塞在uaddr所指向的futex值上，并等待*uaddr的FUTEX_WAKE操作；如果*uaddr != val，那么futex(2)失败并返回伴着EAGAIN错误码。

如果参数timeout非空，那么timeout指向的结构体会指定休眠的时间。(此时间间隔会向上取到系统时间的粒度，并保证不会提前到期)。timeout默认会根据CLOCK_MONOTONIC时钟来计算，但从Linux4.5开始，可以在futex_op上指定FUTEX_CLOCK_REALTIME来选择CLOCK_REALTIME时钟。如果timeout为空，那么调用会无限期阻塞。

#### FUTEX_WAKE(since Linux 2.6.0)

FUTEX_WAKE操作可以唤醒最多val个在uaddr指向的futex字上的等待者。最常见的是val为1，表示唤醒一个等待者，或者val为INT_MAX，表示唤醒所有等待者。不保证某个等待者被唤醒。比如说不保证高调度优先级的等待者先于低优先级等待者被唤醒。

#### Synchronized和ReentrantLock和区别

都是通过原子操作来控制锁的分配，ReentrantLock基于链表，能实现公平锁和非公平锁。Synchronized在没有锁竞争的情况下只是用瘦锁，当锁发现竞争是才会使用Monitor对象将锁升级成胖锁，Monitor锁的原理用的是Mutex类，该类通过原子变量state_contentder管理锁，也支持递归

#### 死锁发生场景

`Thread-1`持有资源`Object1`但是需要资源`Object2`完成自身任务，同样的，`Thread-2`持有资源`Object2`但需要`Object1`，双方都在等待对方手中的资源但都不释放自己手中的资源，从而进入死锁。





