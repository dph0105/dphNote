## NDK-JNI

NDK: Native Development Kit

JNI: Java Native Interface

### JniVmExt

构造函数

创建globals和weak_globals2个IndirectReferenceTable,kind分别是kGlobal和kWeakGlobal，大小都是51200个对象实例

创建Libraries对象,用于存储加载到内存的so文件，用于Native方法的查找

#### AttachCurrentThread

调用Thread.Attach方法创建线程，创建时会创建Thread对象，调用Init方法初始化栈，初始化EntryPoints等参数，然后创建java的Thread对象，再调用java_thread.init方法

#### DetachCurrentThread

调用runtime.thread_list.Unregister(Thread::Current())

调用Thread.Destroy()

### IndirectReferenceTable

用于管理Native代码执行过程中生成的object，分别kLocal、kGlobal、kWeakGlobal三种kind类型，kGlobal和kWeakGlobal类型的table存储在javaVmExt中，提供给所有线程访问；kLocal类型的table存储在用于每个thread实例的jniEnvExt中，只有单个线程才能访问。

table表内部通过IrtEntry数组的存储对象，kLocal可用扩展，当数量不足时可每次2倍扩容

### JniEnvExt

创建locals的属性IndirectReferenceTable,kind分别是kLocal，大小默认是512个对象实例，可用扩展大小

创建monitors对象，默认大小32个对象，最大4096个对象，用于存储MonitorEnter的对象

#### RegisterNatives

ArtMethod默认的ptr_sized_fields方法是GetJniDlsymLookupStub，用于在jvmExt的so库中查找。

将Native函数指针添加到ArtMethod的ptr_sized_fields的data属性中

#### PushLocalFrame

先确保capacity参数大小，小于就扩容，然后将jniEnvExt的local_ref_cookie保存到vector列表中，并将IndirectReferenceTable中当前位置的segment_state_保存到jniEnvExt的local_ref_cookie中，保证后面创建的IndirectReference只能在后面的列表中保存。

#### PopLocalFrame

将JniEnvExt的local_ref_cookie赋值给IndirectReferenceTable对象的segment_state_对象，用于设置当前最top位置。然后将stacked_local_ref_cookies 的最后一个元素出栈赋值给JniEnvExt的local_ref_cookie，用于设置IndirectReference起始位置

将java_survivor的jobject类型对象从后面分配的栈中获取Mirror::Object，然后在Pop完成后重新添加到IndirectReferenceTable中，生成新的jobject

#### NewLocalRef

将对象添加到locals表中，返回jobject值

#### NewString

Jchar* chars 2个字节

#### NewStringUTF 

char* utf utf-8

#### GetStringChars

```
static const jchar* GetStringChars(JNIEnv* env, jstring java_string, jboolean* is_copy) 
```

参数 is_copy:当设置为true时表示jchar*通过new操作符生成的，使用完需要释放内存。当jstring对象是在heap中的位置是可移动空间时/jstring是Compressed的就需要新建一个内存

#### ReleaseStringChars

```
static void ReleaseStringChars(JNIEnv* env, jstring java_string, const jchar* chars)
```

当jstring是Compressed的或者jstring的值于chars值不等，调用delete[] chars释放内存

### JNI方法

#### @FastNative

#### JNIMethodStart



#### JNIMethodEnd

