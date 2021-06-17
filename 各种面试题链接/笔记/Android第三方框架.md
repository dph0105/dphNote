### Glide

glide内部将数据抽象成Model（原始数据），DATA（中间数据），RESOURCE（最终数据）。

##### glide初始化

注册ModelLoader用于将MODEL转化成DATA的数据

注册ResouceDecode用于将DATA转化成RESOURCE的数据

注册Encoder用于将DATA缓存到本地File

注册ResourceEncoder用于将Resource缓存到本地File

注册DataRewinder用于控制数据回到起点

注册Transcoder用于Resource之间的转化

##### glide缓存

glide中使用了资源池管理数据，优化加载速度。

###### ArrayPool

数组池中使用了NavigableMap用于获取最近的key

###### BitmapPool

bitmap池中使用NavigableMap用于获取最近的key

###### memoryCache

使用LruCache

###### diskCache

使用DiskCache

##### glide加载

先获取RequestManager

通过RequestManager构造RequestBuilder对象

调用RequestBuilder.into方法构造Request，并调用RequestManager.track启动请求

SingleRequest启动时，会先去获取View的宽高

获取到宽高后，调用Engine.load进行Model数据的加载

Engine.load方法会先判断memoryCache已经有缓存，有就返回缓存

没有缓存的话，会判断当前是否有该资源的EngineJob任务在执行，有的话添加回调，等待完成

没有EngineJob在执行的话，会构造EngineJobe和DecodeJob执行资源加载

当数据可以通过缓存加载时，会使用diskCacheExecutor线程池加载，否则使用SourceExecutor请求

DecodeJob中有SourceGenerator，DataCacheGenerator和ResourceCacheGenerator三种生成器。

ResourceCacheGenerator将从磁盘缓存中获取ResourceKey的Resource缓存File

DataCacheGenerator将从磁盘缓存中获取ModelLoader的Key的Data缓存FIle

SourceGenerator将获取ModelLoader将Model解析成Data数据，当可缓存时会将Data数据添加到Data缓存中，并使用DataCacheGenerator获取Data缓存数据

然后将Data数据通过LoadPath转换成Resource

转化成Resouce后，当dataSource来源不是RESOURCE_DISK_CACHE的话会使用transformation将Resource转换下

Resouece转换完后，通知resource已经被加载成功

通知完后判断是否可以缓存Resouce，是的话缓存resource

### OkHttp

内部有一个Dispatcher类，该类使用一个线程池对象管理连接的请求。

每个http请求会通过RealCall类来实现，ReallCall类执行会通过拦截器链路将请求发送到服务端，并接受服务端响应结果处理。

在请求执行时，会创建StreamAllocation对象，该对象用于获取或创建一个连接，

### Retrofit

### ARouter

在编译时使用annonationProcesser扫描ARouter的注解，生成IRouterRoot,IRouterGroup，IProviderGroup类，包含RouteMethod

autoRegister插件会扫描生成的类，然后将其的对象构造方法和注册到ARouterLogisticsCenter的代码通过ASM字节码修改框架添加到LogisticsCenter中。

ARouter初始化时会将这个类添加到LogisticsCenter中。

### Tinker

#### 生成补丁包过程

##### ApkDecoder

先使用ManifestDecoder遍历newApk和oldApk中的AndroidManifest.xml文件，校验是否发生了无法应用的修改。判断是否有组件发生变化。

然后遍历newApk的文件树，如果满足dexFilePattern，使用dexDiffDecoder处理，如果满足soFilePattern使用BsDiffDecoder处理，如果满足resFilePattern使用resDiffDecoder

##### DexDiffDecoder

先检查dex文件中是否修改了不可修改的类。

如果oldFile不存在，直接拷贝dex文件到patch结果目录下

如果oldFile存在：

当使用了加固模式的话，比对oldDex和newDex，只获取新增的类和修改过的类,通过DexBuilder写入到dex文件中

当不使用加固模式的话，使用DexApply差分算法获取patch补丁包。

##### BsDiffDecoder

使用bsDiff获取补丁

##### ResDiffDecoder

新增的直接拷贝到结果目录

删除的只添加记录

发生修改的话，判断newFile的大小是否大于100K，大于使用BsDiff，否则直接拷贝到result目录

#### 应用补丁包过程

Dex应用过程:

判断oldDexCrc是否为0，是0表示是新dex，直接解压到输出目录

再判断newDexCrc是否为0，是0表示没有使用差分算法，直接解压到输出目录

都不为0，使用差分算法合成补丁包，然后加到输出目录

最后将dex目录中所有文件合并压缩到tinker_classN.apk中

最后使用dex2oat优化tinker_classN.apk文件输出oat文件

So应用过程:

判断patchFileMd5的值是否为0，是0表示是新增so文件，直接解压到输出目录

不为0，则使用Bsdiff合成补丁生成so文件，输出到so目录

res应用过程：

解压新增文件，添加到resources.apk中

如果是大文件，使用Bsdiff合成补丁后添加到resources.apk中

然后将原始Apk不在补丁目录的的资源也添加到resources.apk中

#### 加载补丁过程

dex加载过程：

使用了加固模式：调用DexPathList的makePathElements方法将新添加的dexFile和dex2oat生成的目录当做参数生成DexElements数组，将该数据添加DexPathList的dexElements数组的头部。

没有使用加固模式：创建DelegateLastClassLoader，将新添加的dexFile和oat文件添加作为参数生成对象,并将oldClassLoader设置为parent，然后修改Application.mBase对象中的classLoader，mBase的PackageInfo的classLoader对象等。

so加载过程

so文件加载阶段只是检查文件是否存在和md5值匹配

如果要加载so文件的话，使用System.load()加载路径下的so文件

res加载过程

将LoadedApk的resDir路径修改为resources.apk补丁的路径

将Resources的mAssets替换成新的使用了resource.apk的AssetsManager对象

#### activity启动hook

先在AndroidManifest中声明占位activity。

当应用补丁时，将新加的activity的信息添加到meta文件记录

获取ServiceManager的原始Binder对象AMS和PMS，然后使用动态代理修改queryLocalInterface方法，使通过queryLocalInterface调用的AWM和PMS是重写的动态代理类。

然后将AMS和PMS的新代理类应用到ServiceManager和ActivityThread中。

PMS代理类主要代理了getActivityInfo和resoverIntent方法

AMS代理类主要代理了启动Activity相关的方法，如果intent执行的activity找不到，去新增的IncrementComponentManager中查找获取activity替换

Intent中添加一个INTENT_EXTRA_OLD_COMPONENT新activity的对象

然后再hookInstrumentation，在newActivity方法中解析INTENT_EXTRA_OLD_COMPONENT并实例化activity对象。

### Gson

gson内部有一个TypeAdapterFactory的数组，改工厂类用于实例化用于解析不同的TypeToken对应的TypeAdapter类。

##### 反射





