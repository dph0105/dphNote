#### Animation

在View.draw方法进行绘制之前，会调用Animation的getTransform方法获取动画的变换矩阵，透明度，clipRect等信息，然后将这些信息应用到Canvas中。

#### Animator

通过修改属性来控制动画