### 扫描蓝牙设备

BluetoothAdapter.startLeScan(callback: LeScanCallback)

回调中返回BluetoothDevice类型的对象

#### 连接蓝牙

BluetoothDevice.connectGatt

连接成功返回BluetoothGatt对象

#### GATT模式

gatt配置文件中会有一系列BluetoothGattService，BluetoothGattService中有一系列BluetoothCharacteristic特性类。

#### 开启特性通知模式

BluetoothGatt.setCharacteristicNotification(BluetoothCharacteristic, Boolean)

#### 向蓝牙设备写数据

通过向BluetoothCharacteristic设置Value值，然后通过BluetoothGatt.writeCharacteristic(BluetoothCharacteristic),发送数据

#### 接受蓝牙设备回传的数据

开启了特性通知属性的话，当特性数据发生变化时，会回调onCharacteristicRead方法，方法中可读取特性数据获取内容。

