### 引言

Java语言的一个重要特性即垃圾回收机制是基于JVM虚拟机实现的，它帮助Java管理内存的回收。而Android系统的Native层基本是通过C++的实现的，与Java语言不同的是，C++没有垃圾回收机制，C++必须手动释放在堆内存中分配的内存即new出来的对象，稍有不慎就会造成内存泄漏。针对这个问题，在Android中实现了一套类似Java垃圾回收机制的智能指针，实现对象的自动回收。下面我们将分析智能指针是如何实现自动回收对象内存的。

### 一、Android智能指针的原理

#### 1. Java垃圾回收引用计数法

Java垃圾回收判断对象是否存活一般有两种方法，根搜索算法和引用计数法，Android智能指针借鉴了引用计数法的思想，我们再复习一下什么是引用计数法：

给对象添加一个引用计数器，每当有一个地方引用它时，计数器值加１；当引用失效时，计数器值减１.任何时刻计数器值为０的对象就是不可能再被使用的。

那为什么主流的Java虚拟机里面都没有选用这种算法呢？其中最主要的原因是它很难解决对象之间相互循环引用的问题。

#### 2. Android智能指针原理

借鉴Java引用计数法，首先需要给对象添加一个引用计数器。Android设计了基类RefBase，用来管理引用数，所有的类必须从RefBase派生。

### 二、智能指针的实现

#### 1. RefBase

RefBase用来管理派生于它的类的对象的引用数，它有两个主要的方法`incStrong(const void* id)`和`decStrong(const void* id)`分别用于引用计数加1和减1。

首先我们来看RefBase的构造函数

```c++
RefBase::RefBase()
    : mRefs(new weakref_impl(this))
{
}

explicit weakref_impl(RefBase* base)
	: mStrong(INITIAL_STRONG_VALUE)
    , mWeak(0)
    , mBase(base)
    , mFlags(0)
{
}

```

RefBase的构造函数中只做了一件事， 就是创建一个weakref_impl对象mRefs，weakref_impl从weakref_type派生，它是真正管理引用计数的类，我们看它的成员以及构造函数：

```c++
class RefBase::weakref_impl : public RefBase::weakref_type
{
public:
    std::atomic<int32_t>    mStrong;	//强引用计数
    std::atomic<int32_t>    mWeak;		//弱引用计数
    RefBase* const          mBase;		//实际对象
    std::atomic<int32_t>    mFlags;		//生命周期是由强引用管理还是弱引用管理的标志

    explicit weakref_impl(RefBase* base)
        : mStrong(INITIAL_STRONG_VALUE)	//默认值为INITIAL_STRONG_VALUE
        , mWeak(0)						//默认值为0
        , mBase(base)					//指向实际对象
        , mFlags(0)						//默认强引用管理生命周期
    {
    }
   
    ...
}
```

weakref_impl管理了强引用和弱引用的计数，它通过mFlags来判断对象的回收是看强引用还是弱引用，mFlags可以设置3个值，分别为：

```c++
enum {
    OBJECT_LIFETIME_STRONG  = 0x0000,
    OBJECT_LIFETIME_WEAK    = 0x0001,
    OBJECT_LIFETIME_MASK    = 0x0001
};
```
weakref_impl的构造器中，设置弱引用计数为0，设置强引用计数为INITIAL_STRONG_VALUE（ 1 << 28）



接下来看incStrong方法：

```c++
void RefBase::incStrong(const void* id) const
{
    weakref_impl* const refs = mRefs;
    refs->incWeak(id); //给弱引用计数加1
    refs->addStrongRef(id);//Release中什么也不做
    const int32_t c = refs->mStrong.fetch_add(1, std::memory_order_relaxed);//强引用计数+1，返回的是旧值
    if (c != INITIAL_STRONG_VALUE)  {
        return;
    }
	//若第一次引用则减去INITIAL_STRONG_VALUE,使值为1
    int32_t old __unused = refs->mStrong.fetch_sub(INITIAL_STRONG_VALUE, std::memory_order_relaxed);
    //mBase指向实际对象，onFirstRef()，即第一次引用的回调，默认是空函数
    refs->mBase->onFirstRef();
}

void RefBase::weakref_type::incWeak(const void* id)
{
    weakref_impl* const impl = static_cast<weakref_impl*>(this);
    impl->addWeakRef(id);//Release版中什么也不做
    const int32_t c __unused = impl->mWeak.fetch_add(1,
            std::memory_order_relaxed);
}


```

再看decStrong方法

```c++
void RefBase::decStrong(const void* id) const
{
    weakref_impl* const refs = mRefs;
    refs->removeStrongRef(id);//Release版是空函数
    const int32_t c = refs->mStrong.fetch_sub(1, std::memory_order_release);//强引用计数-1
    if (c == 1) {//若原先的强引用计数为1，执行fetch_sub后为0了，即没有引用了
        std::atomic_thread_fence(std::memory_order_acquire);
        refs->mBase->onLastStrongRef(id);///mBase指向实际对象，onLastStrongRef()，即没有引用的回调，默认是空函数
        int32_t flags = refs->mFlags.load(std::memory_order_relaxed);
        if ((flags&OBJECT_LIFETIME_MASK) == OBJECT_LIFETIME_STRONG) {
            //若Flag为OBJECT_LIFETIME_STRONG，delete这个对象
            delete this;
        }
    }
    refs->decWeak(id);
}

```







```c++
class RefBase
{
public:
            void            incStrong(const void* id) const;
            void            decStrong(const void* id) const;
    
            void            forceIncStrong(const void* id) const;

    class weakref_type
    {
    public:
        RefBase*            refBase() const;

        void                incWeak(const void* id);
        void                decWeak(const void* id);

        // acquires a strong reference if there is already one.
        bool                attemptIncStrong(const void* id);

        // acquires a weak reference if there is already one.
        // This is not always safe. see ProcessState.cpp and BpBinder.cpp
        // for proper use.
        bool                attemptIncWeak(const void* id);
    };

            weakref_type*   createWeak(const void* id) const;
            
            weakref_type*   getWeakRefs() const;

    typedef RefBase basetype;

protected:
                            RefBase();
    virtual                 ~RefBase();
    
    //! Flags for extendObjectLifetime()
    enum {
        OBJECT_LIFETIME_STRONG  = 0x0000,
        OBJECT_LIFETIME_WEAK    = 0x0001,
        OBJECT_LIFETIME_MASK    = 0x0001
    };
    
            void            extendObjectLifetime(int32_t mode);
            
    //! Flags for onIncStrongAttempted()
    enum {
        FIRST_INC_STRONG = 0x0001
    };
    
    // Invoked after creation of initial strong pointer/reference.
    virtual void            onFirstRef();
    // Invoked when either the last strong reference goes away, or we need to undo
    // the effect of an unnecessary onIncStrongAttempted.
    virtual void            onLastStrongRef(const void* id);
    // Only called in OBJECT_LIFETIME_WEAK case.  Returns true if OK to promote to
    // strong reference. May have side effects if it returns true.
    // The first flags argument is always FIRST_INC_STRONG.
    // TODO: Remove initial flag argument.
    virtual bool            onIncStrongAttempted(uint32_t flags, const void* id);
    // Invoked in the OBJECT_LIFETIME_WEAK case when the last reference of either
    // kind goes away.  Unused.
    // TODO: Remove.
    virtual void            onLastWeakRef(const void* id);

private:
    friend class weakref_type;
    class weakref_impl;
    
                            RefBase(const RefBase& o);
            RefBase&        operator=(const RefBase& o);

private:
    friend class ReferenceMover;

    static void renameRefs(size_t n, const ReferenceRenamer& renamer);

    static void renameRefId(weakref_type* ref,
            const void* old_id, const void* new_id);

    static void renameRefId(RefBase* ref,
            const void* old_id, const void* new_id);

        weakref_impl* const mRefs;
};
```

