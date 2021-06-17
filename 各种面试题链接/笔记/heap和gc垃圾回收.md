## Heap堆

在ART虚拟机中，Heap堆由存储对象实例的space类，分配对象的分配器算法，控制对象回收的垃圾回收算法和其他用于对象标记功能的辅助类等组成。

在ART虚拟机初始化runtime.Init方法中会构造Heap堆对象。heap对象构造方法的主要流程有：

1.在Android R中，默认采用GC_TYPE_CC并发复制的垃圾回收算法，并且支持分代回收。该算法对象的分配器是ALLOCK_TYPE_REGION_TLAB.

2.将预先编译好的.art文件通过内存映射方式转换成ImageSpace添加到heap中。.art文件中主要存储了启动相关的一些jar包中的类。

3.创建non_moving_space对象。

4.所以这时会创建region_space对象添加到heap中。

5.创建large_object_space对象。

6.创建card_table,ModUnionTable等和对象标记有关的辅助类

7.根据选择的垃圾回收算法，创建垃圾回收想关的类，CC的算法实现类是ConcurrentCopying对象，然后将region_space对象关联到算法实现类中。

### RegionSpace

RegionSpace会将可分配内存按256KB字节划分成对个Region对象。每个Region对象都有state_和type_2个标识。初始情况下Region的state是KRegionStateFree表示还没有内存被分配，type是kRegionTypeNone。当分配的内存小于256KB字节时，会按顺序获取一个state_是Free的Region,并将状态改为allocated，type_改为to_Space。

### ALLOCK_TYPE_REGION_TLAB分配算法

第一次分配时，会从region_space取出一个空的Region绑定到当前线程的局部变量对象中，tlsPtr_.thread_local_start表示Region起点，tlsPtr_.thread_local_end表示起点+16KB，tlsPtr_.thread_local_limit表示Region终点。之后的分配会先判断线程的局部变量空间是否满足分配大小，不满足的话，会先将该局部变量空间对应的Region从线程对象中移除，并将剩余空间添加到partial_tlabs_multimap中，然后从partial_tlabs_遍历判断是否有可用的部分存储用于分配，没有的话再拿出一个Region添加绑定到线程局部变量中。

### Heap.preZygoteFock



## CC并发拷贝垃圾回收算法

##### 选择垃圾回收策略

根据回收力度选择要使用的回收器

```c++
active_concurrent_copying_collector_ = (gc_type == collector::kGcTypeSticky) ?
    young_concurrent_copying_collector_ : concurrent_copying_collector_;
```

##### 初始化阶段

标记是否需要需要清除所有RegionSpace

初始化标志位：done_scanning_,

标记zygote控件的大对象

##### 新生代标记阶段

将object对应space的bitmap的执行地址标记为1，并添加到mark stack

计算存活的对象在各自Region的比例（只计算不是新分配的region，因为新分配的region一定是待回收Region）

##### 翻转线程阶段（只标记线程root对象）

挂起除了gc线程之外的线程

将满足条件的ToSpace设置为fromSpace：当回收策略是kGcTypeSticky时，选择toSpace存活对象比例小于75%space空间大小和新添加的Region设置为fromSpace

不满足条件的ToSpace设置为unevacFromSpace

线程执行标记对象，设置线程的flipFunction，再线程恢复时执行flipFunction。

###### 标记方法

* 如果已经在toSpace，直接返回
* 如果在fromSpace，先判断LockWord的对象转发地址，如果不为null，表示已经被拷贝到toSpace了，否则调用region_space分配region拷贝对象到toSpace，并将fromSpace的对象的LockWord设置为转发地址模式
* 如果在unevacFromSpace，设置对象LockWord的rb状态为灰色，添加到mark stack
* 如果在immuneSpace，设置对象LockWord的rb状态为灰色，添加到immune_gray_stack_栈
* 如果在NonMovingSpace，设置对象LockWord的rb状态为灰色，添加到immune_gray_stack_栈

##### 拷贝阶段

将对象的属性引用标记

##### 清除阶段



##### 完成阶段









## 四个引用的作用

### SoftReference

当软引用所引用的对象还没被标记时，同时垃圾回收标记了清除软引用时，软引用对象会被添加到ReferenceQueue的unenqueued链表中。

Daemons中ReferenceDaemon后台线程执行时，如果softReference中的queue不为null，那么会将softReference添加到ReferenceQueue的head和tail队列中。

### WeakReference

引用所引用的对象没有被标记，当触发了垃圾回收时，引用会被添加到ReferenceQueue的unenqueued链表中。

Daemons中ReferenceDaemon后台线程执行时，如果Reference中的queue不为null，那么会将WeakReference添加到ReferenceQueue的head和tail队列中。

### FinalizerReference

当引用的对象中重写了finalize()方法时，在类实例化的时候，会实例化FinalizerReference(queue参数为FinalizerReference.Queue)，添加到FinalizerReference的head链表结构中。

在垃圾回收中，ReferenceProcessor的处理中，会将FinalizerReference对象引用的对象标记后赋值给zombie，同时清除refer值。然后再垃圾回收结束时，会将对象添加到ReferenceQueue的unenqueued链表中。

Daemons中ReferenceDaemon后台线程执行时，如果softReference中的queue不为null，那么会将FinalizerReference添加到ReferenceQueue的head和tail队列链表中。

在Daemons的FinalizerDaemon后台线程中，会每次从FinalizerReference的queue的链表头出栈获取FinalizerReference对象，然后判断所引用的对象中zombie对象有没有值，有值执行finalize()操作，没值等待下一个对象。执行结束后会将对象从FinalizerRerfence.head链表中移除，并清除zombie值

### PhantomReference [ˈfæntəm] 

引用所引用的对象没有被标记，当触发了垃圾回收时，引用会被添加到ReferenceQueue的unenqueued链表中。

Daemons中ReferenceDaemon后台线程执行时，如果Reference中的queue不为null，那么会将Reference添加到ReferenceQueue的head和tail队列中。

通过调用PhantomReference.remove()，可以监听对象的回收，并执行回收钱的清理工作 ，相当于FinalizerReference更灵活，可以在任意线程执行清理工作。

