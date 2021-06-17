## ClassLinker

#### ArtField

在类加载期间只是计算了静态属性和实例属性在Class和Object中的偏移量offset_

#### ArtMethod



#### InternTable 

常量池；

## ClassLoader

#### BootClassLoader

使用BootClassLoader来加载类时，只会在class_linker的boot_class_table和boot_class_path中查找类

#### PathClassLoader/DexClassLoader

先从parent查找类

再从shared libraries中查找

再从class_loader当前类查找

#### InMemoryClassLoader

先从parent查找类

再从shared libraries中查找

再从class_loader当前类查找

#### DelegateLastClassLoader

先从bootClassLoader中查找类

再从shared libraries中查找类

再从class_loader当前类查找

再从parent中查找类

#### BaseDexClassLoader

BaseDexClassLoader中有一个DexPathList对象，DexPathList中有一个dexElements数组，DexElement对应一个DexFilejava对象，每个DexFilejava对象在native层对应一个oatFile和一组nativeDexFile对象。

当要从BaseDexClassLoader获取类时，会先从每个nativeDexFile对象中查找与类描述符匹配的class_def数据块，然后调用class_linker的DefineClass将class_def数据块的内容加载成Mirror::Class对象

## 类加载机制

在ART虚拟机中，java类加载的逻辑主要由ClassLinker类来实现。ClassLinker内部持有不同的class_loader对象(java层的BootClassLoader对应class_linker中boot_class_path)，每个class_loader对象内部持有一个dexFile列表和 一个ClassTable对象，DexFile对象对应于apk中每个class.dex文件，classTable对象存储这已经被当前class_loader解析的类的缓存。

当通过New指令，或者调用classloader加载类时，会先根据传递进来的class_loader中的classTable缓存中查找。

查找不到时，再根据class_loder的类型，遍历父子关系class_loader定义类。

如果class_loader类型是BootClassLoader,会从class_linker的boot_class_path_的DexFile中查找。

如果class_loader类型是PathClassLoader、DexClassLoader、InMemoryDexClassLoader的话，会先从class_loader的父类查找，查找不到再从本class_loader查找。

如果class_loader类型是DelegateLastClassLoader的话，会先从class_linker的boot_class_path_的DexFile中查找，查找不到会从本class_loader中查找，查找不到的话，才会去class_loader的父类中查找。

如果class_loader是其他类型的话，会通过jni调用class_loader的loadClass方法查找类。

不管通过什么方式查找类，最后都会调用ClassLinker的DefineClass方法。Define方法中会查找class_table中是否已经被加载过了，加载过了会直接返回。

#### DefineClass主要流程

先获取class_def静态属性的数量，调用heap分配不包含EmbeddedTable的class对象。

然后根据当前DexFile创建一个dex_cache对象和一个ClassTable绑定

##### SetupClass 加载id信息

从dex_file中读取类的id相关信息赋值到class对象中

##### LoadClass 加载属性和方法

从dex_file中将类的属性和方法实例化成ArtField和ArtMethod添加到Class对象中

##### LoadSuperAndInterfaces 加载父类和接口方法

将当前类的父类和接口信息赋值给class

##### LinkerClass 根据父子类的对象大小计算属性在内存中偏移量，将重写的父类和接口方法替换接口方法和父类方法

将类字段的内存偏移量设置到ArtField实例中

根据类的是否可实例化类型，遍历类的接口方法和类的父类方法，将方法转换成重写的方法。

#### VerifyClass

验证类

#### InitializeClass

调用class的静态构造方法

## 对象new的过程

函数中声明的new指令语句会在编译时转化为newinstance指令，newinstance指令中包含目标类的索引。

ART虚拟机在解释newinstance指令时先使用classlinker根据索引解析出目标类对象。

当目标类对象是String.class时，直接分配一个空字符串对象，当目标类不是String.class时,会在对象分配完成之后，判断是否类有定义finalized方法，有的话会将该对象通过FinalizerReference.add存储在FinalizerReference.head对象链表中。

##  Class.forName过程

