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

RefBase用来管理派生于它的类的对象的引用数，可以说RefBase相当于JVM给对象添加的**“引用计数器”**，除了引用计数的功能，RefBase还会在引用归零时负责删除对象释放内存。对于所有派生自RefBase的对象来说，都应该通过这种方式删除对象回收内存，而不是通过其他方式。当一个RefBase对象创建后，应在它的构造器后尽快增加它的强引用计数，一般通过**构造sp<>**或者是**调用incStrong**来完成。

注：RefBase的代码包含了分为Release版和Debug版，下面只分析Release版的代码

##### 1.0 RefBase 小例子

我们来看一个小例子，我抽取出了Android源码中的RefBase相关的一些代码，创建了从RefBase派生的类Test。

```c++
class Test : public  RefBase{

public:
    Test(){
         printf("Test:Test()\n");
    }
    ~Test(){
        printf("Test:~Test()\n");
    }

    void show(){
        printf("Test:show()\n");
    }

};
void test(){
    Test* pTest = new Test();
    
    pTest->incStrong(pTest);
    pTest->decStrong(pTest);
}
int main(int argc, char const *argv[])
{   
    test();
    return 0;
};
//输出
//Test:Test()
//Test:~Test()


```

执行这段代码，我们可以发现即使没有回收Test对象，依旧执行了RefBase的析构函数。incStrong增加了引用计数，而decStrong减少了引用计数，当引用计数为0时就回收对象。

接下来我们来看具体的代码。

##### 1.1 RefBase() 构造函数

首先我们来看RefBase的构造函数

```c++
//[system\core\libutils\RefBase.cpp]
RefBase::RefBase()
    : mRefs(new weakref_impl(this))
{
}
```

###### 1.1.1 weakref_impl

RefBase的构造函数中只做了一件事， 就是创建一个weakref_impl对象作为成员变量**mRefs**，**weakref_impl派生自RefBase的内部类weakref_type**，**它是真正管理引用计数的类**，我们看它的成员以及构造函数：

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

weakref_impl有四个成员变量，其中mStrong和mWeak都是原子类型的int值，分别管理强引用计数和弱引用计数，而对象的生命周期管理是强引用计数控制还是弱引用计数控制则通过mFlags来判断，mFlags可以设置3个值，分别为：

```c++
enum {
    OBJECT_LIFETIME_STRONG  = 0x0000,//强引用计数控制生命周期
    OBJECT_LIFETIME_WEAK    = 0x0001,//弱引用计数控制生命周期
    OBJECT_LIFETIME_MASK    = 0x0001 //用于掩膜运算，只关心第一位的值，即 OBJECT_LIFETIME_MASK & mFlag 的情况只有0001和0000两种
};
```
最后一个成员变量mBase即实际对象，也就是RefBase派生出来的类的对象。

weakref_impl的构造函数中为它的四个成员变量赋值，其中mFlogs为0，表示默认使用强引用计数控制对象生命周期。mWeak设为0，而mStrong却设置为INITIAL_STRONG_VALUE（1<<28)，这是为什么呢？下面会讲到。

##### 1.2 incStrong()

接下来看incStrong方法：

```c++
void RefBase::incStrong(const void* id) const
{
    weakref_impl* const refs = mRefs;
    refs->incWeak(id); //给弱引用计数加1
    refs->addStrongRef(id);//Release版中什么也不做
    const int32_t c = refs->mStrong.fetch_add(1, std::memory_order_relaxed);//强引用计数+1，返回的是旧值
    if (c != INITIAL_STRONG_VALUE)  {
        return;
    }
	//若第一次引用则减去INITIAL_STRONG_VALUE,使值为1
    int32_t old __unused = refs->mStrong.fetch_sub(INITIAL_STRONG_VALUE, std::memory_order_relaxed);
    //mBase指向实际对象，onFirstRef()，即第一次引用的回调，默认是空函数
    refs->mBase->onFirstRef();
}

```

incStrong的代码比较简单，其实就给mRefs中的强引用计数+1，并调用mRefs的incWeak函数

值得注意的是，这里的强引用计数mStrong计数在初次加一时，由于初始值为INITIAL_STRONG_VALUE，会在加一之后减去INITIAL_STRONG_VALUE的值，最终第一次强引用的值依旧是1。

###### 1.2.1 weakref_impl#incWeak

```c++
void RefBase::weakref_type::incWeak(const void* id)
{
    weakref_impl* const impl = static_cast<weakref_impl*>(this);
    impl->addWeakRef(id);//Release版中什么也不做
    const int32_t c __unused = impl->mWeak.fetch_add(1,
            std::memory_order_relaxed);//弱引用计数加1
}
```

incWeak函数做的事情就是将弱引用计数+1.

##### 1.3 decStrong()

再看decStrong方法

```c++
void RefBase::decStrong(const void* id) const
{
    weakref_impl* const refs = mRefs;
    refs->removeStrongRef(id);//Release版是空函数
    const int32_t c = refs->mStrong.fetch_sub(1, std::memory_order_release);//强引用计数-1
    if (c == 1) {//若原先的强引用计数为1，执行fetch_sub后为0了，即没有引用了
        std::atomic_thread_fence(std::memory_order_acquire);
        refs->mBase->onLastStrongRef(id);///mBase指向实际对象，onLastStrongRef()，即最后一个强引用的回调，默认是空函数
        int32_t flags = refs->mFlags.load(std::memory_order_relaxed);
        if ((flags&OBJECT_LIFETIME_MASK) == OBJECT_LIFETIME_STRONG) {
            //若Flag为OBJECT_LIFETIME_STRONG，delete这个对象
            delete this;
        }
    }
    refs->decWeak(id);
}
```

decStrong中将强引用计数减1，并通过判断变量mFlags的值是否等于`OBJECT_LIFETIME_STRONG`，即判断是否是强引用计数控制生命周期。当是强引用控制，并且强引用计数减1后为0，则删除对象释放内存。如同incStrong方法会调用incWeak方法，decStrong方法中最后也调用了decWeak方法：

###### 1.3.1 weakref_impl#decWeak

```c++
void RefBase::weakref_type::decWeak(const void* id)
{
    weakref_impl* const impl = static_cast<weakref_impl*>(this);
    impl->removeWeakRef(id);//Release版本中什么也不做
    const int32_t c = impl->mWeak.fetch_sub(1, std::memory_order_release);//弱引用计数减一，c为旧值
    LOG_ALWAYS_FATAL_IF(BAD_WEAK(c), "decWeak called on %p too many times",
            this);
    if (c != 1) return; //如果弱引用计数减一之前的值不是1，即还没有归零，直接返回
    atomic_thread_fence(std::memory_order_acquire);

    int32_t flags = impl->mFlags.load(std::memory_order_relaxed);
    if ((flags&OBJECT_LIFETIME_MASK) == OBJECT_LIFETIME_STRONG) {
         //如果是强引用计数控制生命周期
        if (impl->mStrong.load(std::memory_order_relaxed)
                == INITIAL_STRONG_VALUE) {
        		//如果强引用计数还是初始值，即实际对象没有被强引用这里不做处理
        } else {
            //强引用计数已经归零，这里删除weakref_impl对象
            delete impl;
        }
    } else {
        //如果是弱引用计数控制生命周期，这里调用回调，并且删除实际对象mBase
        impl->mBase->onLastWeakRef(id);
        delete impl->mBase;
    }
}
```

decWeak方法将弱引用计数减一，**并且这里我们明白为什么强引用计数的初始值不是0，而是设为INITIAL_STRONG_VALUE，因为需要判断对象的强引用计数是否是初始值。**

当mWeak即弱引用计数为0时，若是强引用计数控制生命周期，并且强引用计数已经不是初始值，则删除weakref_impl对象，释放内存。此时，实际对象已经在decStrong函数中被delete了。

而当生命周期由弱引用计数控制时，调用onLastWeakRef生命周期回调，并delete了实际对象，因为此时RefBase:decStrong函数并不执行删除实际对象的操作。

##### 1.4 ~RefBase()析构函数

不管是强引用生命周期控制对象的生命周期，还是弱引用控制，当他们的计数回归0时，都会删除RefBase实际对象，执行RefBase的析构函数

```c++
RefBase::~RefBase()
{
    int32_t flags = mRefs->mFlags.load(std::memory_order_relaxed);
    if ((flags & OBJECT_LIFETIME_MASK) == OBJECT_LIFETIME_WEAK) {
        if (mRefs->mWeak.load(std::memory_order_relaxed) == 0) {
            //当对象的生命周期是弱引用计数控制的，在RefBase的析构函数中，弱引用计数为0时，释放内存weakref_impl对象的内存
            delete mRefs;
        }
    } else if (mRefs->mStrong.load(std::memory_order_relaxed)
            == INITIAL_STRONG_VALUE) {
        //强引用控制，但是强引用计数还是为初始值，这里也释放weakref_impl对象的内存
        delete mRefs;
    }
    // 置NULL
    const_cast<weakref_impl*&>(mRefs) = NULL;
}
```



##### 1.5 RefBase小节

RefBase可以通过incStrong和decStrong管理引用计数，并回收对象，incStrong与decStrong必须成对出现。如A派生自RefBase，A被B引用时，需要调用A。`A ->incStrong(B)`，当B不再使用A时，需要调用 `A -> decStrong(B)`。这种方式虽然可以帮助程序员们回收对象但是依旧很麻烦，毕竟这是一种重复劳动，模板类sp和wp便应运而生。



#### 2. sp<>强引用指针

sp<>是一个模板类，它被声明在 \system\core\libutils\include\utils\StrongPointer.h中

使用sp后，我们就不需要手动地调用incStrong和decStrong来管理引用计数了，只需要在创建对象后，再构造一个sp对象。

##### 2.0 sp小例子

```c++
void test(){
    Test* pTest = new Test();
    sp<Test> spTest(pTest);
    spTest->show();
}

int main(int argc, char const *argv[])
{   
    test();
    return 0;
};
//输出
//Test:Test()
//Test:show()
//Test:~Test()
```

Test依旧是RefBase中的小例子中的类，`sp<Test> spTest(pTest);`通过这一行代码构造了一个sp<Test>对象，当test()执行完毕时，会自动调用Test的析构函数。

##### 2.1 sp的构造函数

```c++
template<typename T>
sp<T>::sp(T* other)
        : m_ptr(other) {//sp使用成员变量m_ptr来存放传入的实际对象
    if (other)//实际对象不为NULL，调用incStrong使强引用计数+1
        other->incStrong(this);
}

template<typename T>
sp<T>::sp(const sp<T>& other)
        : m_ptr(other.m_ptr) {
    if (m_ptr)//传入了其他sp对象，如果other中的m_ptr不为NULL，调用incStrong使强引用计数+1，这里实际对象m_ptr的强引用计数应该大于1
        m_ptr->incStrong(this);
}

template<typename T>
sp<T>::sp(sp<T>&& other)
        : m_ptr(other.m_ptr) {
    other.m_ptr = nullptr;//由于传入的是右值引用，不需要调用incStrong，将右值引用的m_ptr设置为空指针
}

//下面是将其他类型U转化为T类型的构造方式。主要用于父子类之间的转换
template<typename T> template<typename U>
sp<T>::sp(U* other)
        : m_ptr(other) {
    if (other)
        (static_cast<T*>(other))->incStrong(this);
}

template<typename T> template<typename U>
sp<T>::sp(const sp<U>& other)
        : m_ptr(other.m_ptr) {
    if (m_ptr)
        m_ptr->incStrong(this);
}
 
template<typename T> template<typename U>
sp<T>::sp(sp<U>&& other)
        : m_ptr(other.m_ptr) {
    other.m_ptr = nullptr;
}
```

<2.0 sp小例子>中使用的就是第一种构造方法，传入T类型的对象指针，将实际对象赋值给m_ptr变量，并调用实际对象的incStrong方法增加引用计数，从这里我们也可以看出，**sp中的T实际上就是RefBase的派生类**。

不管传入的是实际对象的对象指针还是另外一个sp的引用，都需要调用incStrong函数，使强引用计数在原来的基础上+1。但若是构造函数传入的是类型为sp<>的**右值引用**，则不需要再调用incStrong，只需要将传入的sp的m_ptr赋值给当前的m_ptr。

关于右值引用，根据自己的理解做一下简单解释，看如下的代码：

```
int a = 10;
int &rA = a;
```

引用一般通过&来声明，如上的rA，就是一个左值引用，通过将右值10，赋值给左值a之后，rA才能引用a。而右值10是一个临时值，在赋值完成后，相应的内存就释放掉了。我们不能通过左值引用去引用一个右值，**即`int &rA = 10;`是错误的**。C++11引入了右值引用的概念，即 `int &&rB = 10;`，rB就是一个右值引用，10所在的内存并不会被释放掉，并且可以被程序访问。回到我们的sp概念中：

```c++
sp<Test> getRight(){
    Test* pTest = new Test();
    sp<Test> spTest(pTest);
    return spTest;
}

void test(){
    sp<Test> && rSpTest = getRight();//若不使用右值引用，getRight的返回值在赋值完成后就被释放掉内存，使用右值引用，rSpTest指向返回的spTest
    sp<Test> spTest(rSpTest);
    spTest->show();
}
```

##### 2.2 sp operator重写

sp还重写了操作符=方便我们的使用，

```java
template<typename T>
sp<T>& sp<T>::operator =(const sp<T>& other) {
    // Force m_ptr to be read twice, to heuristically check for data races.
    T* oldPtr(*const_cast<T* volatile*>(&m_ptr));
    T* otherPtr(other.m_ptr);
    if (otherPtr) otherPtr->incStrong(this);
    if (oldPtr) oldPtr->decStrong(this);
    if (oldPtr != *const_cast<T* volatile*>(&m_ptr)) sp_report_race();
    m_ptr = otherPtr;
    return *this;
}

template<typename T>
sp<T>& sp<T>::operator =(sp<T>&& other) {
    T* oldPtr(*const_cast<T* volatile*>(&m_ptr));
    if (oldPtr) oldPtr->decStrong(this);
    if (oldPtr != *const_cast<T* volatile*>(&m_ptr)) sp_report_race();
    m_ptr = other.m_ptr;
    other.m_ptr = nullptr;
    return *this;
}

template<typename T>
sp<T>& sp<T>::operator =(T* other) {
    T* oldPtr(*const_cast<T* volatile*>(&m_ptr));
    if (other) other->incStrong(this);
    if (oldPtr) oldPtr->decStrong(this);
    if (oldPtr != *const_cast<T* volatile*>(&m_ptr)) sp_report_race();
    m_ptr = other;
    return *this;
}
```

赋值号=的重写方便了创建sp对象，我们可以直接通过`sp<Test> spTest = pTest;`获得一个sp对象

而其他一些操作符，则使用实际对象来做处理：

```c++
    COMPARE(==)
    COMPARE(!=)
    COMPARE(>)
    COMPARE(<)
    COMPARE(<=)
    COMPARE(>=)
    
#define COMPARE(_op_)                                           \
inline bool operator _op_ (const sp<T>& o) const {              \
    return m_ptr _op_ o.m_ptr;                                  \
}                                                               \
inline bool operator _op_ (const T* o) const {                  \
    return m_ptr _op_ o;                                        \
}                                                               \
template<typename U>                                            \
inline bool operator _op_ (const sp<U>& o) const {              \
    return m_ptr _op_ o.m_ptr;                                  \
}                                                               \
template<typename U>                                            \
inline bool operator _op_ (const U* o) const {                  \
    return m_ptr _op_ o;                                        \
}                                                               \
inline bool operator _op_ (const wp<T>& o) const {              \
    return m_ptr _op_ o.m_ptr;                                  \
}                                                               \
template<typename U>                                            \
inline bool operator _op_ (const wp<U>& o) const {              \
    return m_ptr _op_ o.m_ptr;                                  \
}

```

这里有一个重要的操作符 -> 

```c++
inline T*       operator-> () const    { return m_ptr;  }
```

所以在sp的小例子中，我们可以sp直接调用Test类的函数。

##### 2.3 sp的析构函数

```c++
template<typename T>
sp<T>::~sp() {
    if (m_ptr)
        m_ptr->decStrong(this);
}
```

在sp的析构函数中，如果m_ptr不为NULL，会调用其decStrong函数，在上文中，我们知道RefBase被强引用控制生命周期时，调用decStrong后调用delete释放内存。

##### 2.4 sp小结

sp是专门用来使用RefBase的派生类的模板类，通过它可以方便的管理RefBase的强引用计数。使用sp需要传入一个RefBase对象，不管是sp的构造函数还是它的赋值符=重写中，都会调用RefBase的incStrong函数来增加引用计数（右值引用除外），由于我们创建sp是通过**静态分配内存的方式**，所以sp会自动调用它的析构函数，在它的析构函数中，又会调用RefBase实际对象的decStrong函数使强引用计数-1，若RefBase的强引用计数为0并且是强引用计数控制生命周期，则会使用delete释放内存。

什么地方需要使用sp呢？建议有所有权关系的对象使用sp，即A拥有B时，我们要使用sp。通常，新分配的对象将立即用于初始化sp，然后可以将其用于构造或分配给其他sp和wp。

#### 3. wp<>弱引用指针



##### 3.1 wp的构造函数

```c++
template<typename T>
wp<T>::wp(T* other)
    : m_ptr(other)
{
    if (other) m_refs = other->createWeak(this);
}

template<typename T>
wp<T>::wp(const wp<T>& other)
    : m_ptr(other.m_ptr), m_refs(other.m_refs)
{
    if (m_ptr) m_refs->incWeak(this);
}

template<typename T>
wp<T>::wp(const sp<T>& other)
    : m_ptr(other.m_ptr)
{
    if (m_ptr) {
        m_refs = m_ptr->createWeak(this);
    }
}

```



##### 3.2 wp#promote

```c++
template<typename T>
sp<T> wp<T>::promote() const
{
    sp<T> result;
    if (m_ptr && m_refs->attemptIncStrong(&result)) {
        result.set_pointer(m_ptr);
    }
    return result;
}
```

我们可以通过wp的成员变量m_ptr以及unsafe_get()函数访问实际对象，但是这种做法是错误的，因为它很有可能返回已经释放的对象的指针。我们应该通过promote方法获取对象的sp之后，再进行操作。



##### 3.3 wp的析构函数

```c++
template<typename T>
wp<T>::~wp()
{
    if (m_ptr) m_refs->decWeak(this);
}
```

