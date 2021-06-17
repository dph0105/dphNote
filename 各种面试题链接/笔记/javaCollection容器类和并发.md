### HashMap

#### hash计算

将Object的hash值与高16的hash值异或

#### put

根据hash值计算数组的位置

数组没有数据的话，直接NewNode赋值

数据有数据时，判断key是否相同，相同修改值

数据key不同时，判断p的链表结构数量是否小于7个，满足的话，直接添加到链表尾部

当大于7个时，将链表转化为红黑树

#### get

根据hash值计算数组的位置

根据位置获取元素，判断元素是否的key值是否要的key相同，相同返回

不同的话判断Node结构是链表还是红黑树，从他们中遍历查找

#### 线程同步问题

当才用链接结构put元素时，没有同步的话可能会导致2次put操作，有1个put的值丢失。

### ConcurrentHashMap

#### put

当hash值对应的位置没有元素时，使用CAS添加元素

当位置有元素时，使用synchronized同步同步代码块控制Node对象，进行后续添加操作。



### LinkerHashMap

内部维持head和tail双向链表，在数据操作完成后进行链表维护



