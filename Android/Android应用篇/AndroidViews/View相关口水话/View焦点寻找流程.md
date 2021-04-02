#### 1.View#focusSearch

简答说明一下寻找焦点的流程，首先通过按键事件来到View的focusSearch方法

```java
    public View focusSearch(@FocusRealDirection int direction) {
        if (mParent != null) {
            return mParent.focusSearch(this, direction);
        } else {
            return null;
        }
    }
```

View中的focusSearch方法就是调用父View的focusSearch方法，父View必定是一个ViewGroup。若没有父View，则直接返回null，焦点就丢失了。

```java
public View focusSearch(View focused, int direction) {
    if (isRootNamespace()) {//判断是否在根视图，根视图就是DecorView
        return FocusFinder.getInstance().findNextFocus(this, focused, direction);//通过FocusFinder来寻找下一个获取焦点的View
    } else if (mParent != null) {
        return mParent.focusSearch(focused, direction);//不是根视图则继续调用 parentView的focesSearch
    }
    return null;
}
```

ViewGroup中的focusSearch需要判断是否是根View，即DecorView，若是，则开始寻找，若不是，则继续向上调用focesSearch。

#### 2.FocusFinder#findNextFocus

```java
    public final View findNextFocus(ViewGroup root, View focused, int direction) {
        return findNextFocus(root, focused, null, direction);
    }

   private View findNextFocus(ViewGroup root, View focused, Rect focusedRect, int direction) {
        View next = null;
        ViewGroup effectiveRoot = getEffectiveRoot(root, focused);
        if (focused != null) {
            //如果一个view设置指定方向的下一个focusView如focusDownId/focusLeftId等这些，会进入该方法
            //这里若指定的view，focusable为null，则next为null
            next = findNextUserSpecifiedFocus(effectiveRoot, focused, direction);
        }
        if (next != null) {
            return next;
        }
        //上面的没找到，就调用View的addFocusables方法，一层一层的将所有的可获取焦点的View加入到焦点List focusables中
        ArrayList<View> focusables = mTempList;
        try {
            focusables.clear();
            effectiveRoot.addFocusables(focusables, direction);
            if (!focusables.isEmpty()) {
                //根据焦点移动方向，在焦点列表中找到下一个焦点View
                next = findNextFocus(effectiveRoot, focused, focusedRect, direction, focusables);
            }
        } finally {
            //完成任务，清空列表
            focusables.clear();
        }
        return next;
    }

```

#### 3.View#addFocusables

addFocusables也分为View和ViewGroup中的

##### View中的addFocusables

```java
public void addFocusables(ArrayList<View> views, @FocusDirection int direction) {
    addFocusables(views, direction, isInTouchMode() ? FOCUSABLES_TOUCH_MODE : FOCUSABLES_ALL);
}

    public void addFocusables(ArrayList<View> views, @FocusDirection int direction,
            @FocusableMode int focusableMode) {
        if (views == null) {
            return;
        }
        if (!canTakeFocus()) {//判断View是否可获取焦点，我们比较熟悉的就是Focusable这个属性
            return;
        }
        //判断焦点模式，有按键和触屏这样的
        if ((focusableMode & FOCUSABLES_TOUCH_MODE) == FOCUSABLES_TOUCH_MODE
                && !isFocusableInTouchMode()) {
            return;
        }
        //加入焦点列表中
        views.add(this);
    }

```



##### ViewGroup中的addFocusables

```java
   @Override
    public void addFocusables(ArrayList<View> views, int direction, int focusableMode) {
        final int focusableCount = views.size();

        final int descendantFocusability = getDescendantFocusability();
        final boolean blockFocusForTouchscreen = shouldBlockFocusForTouchscreen();
        final boolean focusSelf = (isFocusableInTouchMode() || !blockFocusForTouchscreen);
		
        //FOCUS_BLOCK_DESCENDANTS，拦截了子View的获取，我们可以看到这里直接return，没有将子View加入到焦点view列表中
        if (descendantFocusability == FOCUS_BLOCK_DESCENDANTS) {
            if (focusSelf) {//若自身可获取焦点，将自身加入焦点的列表中
                super.addFocusables(views, direction, focusableMode);
            }
            return;
        }

        if (blockFocusForTouchscreen) {
            focusableMode |= FOCUSABLES_TOUCH_MODE;
        }
		//FOCUS_BEFORE_DESCENDANTS，并且自身是可获取焦点的，加入到焦点列表中
        if ((descendantFocusability == FOCUS_BEFORE_DESCENDANTS) && focusSelf) {
            super.addFocusables(views, direction, focusableMode);
        }
		
        
        int count = 0;
        final View[] children = new View[mChildrenCount];
        for (int i = 0; i < mChildrenCount; ++i) {
            View child = mChildren[i];
            if ((child.mViewFlags & VISIBILITY_MASK) == VISIBLE) {
                children[count++] = child;
            }
        }
        FocusFinder.sort(children, 0, count, this, isLayoutRtl());
        //调用所有可获取焦点的子View/子ViewGroup的addFocusables。
        for (int i = 0; i < count; ++i) {
            children[i].addFocusables(views, direction, focusableMode);
        }

 		//设置FOCUS_AFTER_DESCENDANTS时，则最后加入
        if ((descendantFocusability == FOCUS_AFTER_DESCENDANTS) && focusSelf
                && focusableCount == views.size()) {
            super.addFocusables(views, direction, focusableMode);
        }
    }

```



##### RecyclerView的addFocusables

```java
@Override
public void addFocusables(ArrayList<View> views, int direction, int focusableMode) {
    if (mLayout == null || !mLayout.onAddFocusables(this, views, direction, focusableMode)) {
        super.addFocusables(views, direction, focusableMode);
    }
}
```

若RecyclerView有LayoutManager，会调用LayoutManager的onAddFocusables，当返回false时，调用super.addFocusables即ViewGroup的该方法

LayoutManager的onAddFocusables默认返回false，需要自定义实现。



#### 4.findNextFocus

```java
private View findNextFocus(ViewGroup root, View focused, Rect focusedRect,
        int direction, ArrayList<View> focusables) {
    if (focused != null) { //当前焦点View存在的情况，一般都是存在的吧
        if (focusedRect == null) {
            focusedRect = mFocusedRect;
        }
        //设置当前焦点的位置信息，用rect装起来
        focused.getFocusedRect(focusedRect);
        root.offsetDescendantRectToMyCoords(focused, focusedRect);
    } else {//没有焦点View的情况，就是焦点丢失了，这里先不分析了
        。。。。。。
    }

    switch (direction) { //根据方向，寻找下一个焦点View
        case View.FOCUS_FORWARD:
        case View.FOCUS_BACKWARD: //前进和返回，这里也不分析先
            return findNextFocusInRelativeDirection(focusables, root, focused, focusedRect,
                    direction);
        case View.FOCUS_UP:
        case View.FOCUS_DOWN:
        case View.FOCUS_LEFT:
        case View.FOCUS_RIGHT://常见的上下左右方向，进入该方法
            return findNextFocusInAbsoluteDirection(focusables, root, focused,
                    focusedRect, direction);
        default:
            throw new IllegalArgumentException("Unknown direction: " + direction);
    }
}
```





```java
View findNextFocusInAbsoluteDirection(ArrayList<View> focusables, ViewGroup root, View focused,
        Rect focusedRect, int direction) {
    //初始化一个最佳的候选的位置信息
    mBestCandidateRect.set(focusedRect);
    switch(direction) {
        case View.FOCUS_LEFT:
            mBestCandidateRect.offset(focusedRect.width() + 1, 0);
            break;
        case View.FOCUS_RIGHT:
            mBestCandidateRect.offset(-(focusedRect.width() + 1), 0);
            break;
        case View.FOCUS_UP:
            mBestCandidateRect.offset(0, focusedRect.height() + 1);
            break;
        case View.FOCUS_DOWN:
            mBestCandidateRect.offset(0, -(focusedRect.height() + 1));
    }

    View closest = null;

    //进入循环，比较焦点View列表中的View，找到一个最佳的View
    int numFocusables = focusables.size();
    for (int i = 0; i < numFocusables; i++) {
        View focusable = focusables.get(i);

        // only interested in other non-root views
        if (focusable == focused || focusable == root) continue;

        // get focus bounds of other view in same coordinate system
        focusable.getFocusedRect(mOtherRect);
        root.offsetDescendantRectToMyCoords(focusable, mOtherRect);
		//判断这个View是否是最佳候选
        if (isBetterCandidate(direction, focusedRect, mOtherRect, mBestCandidateRect)) {
            mBestCandidateRect.set(mOtherRect);
            closest = focusable;
        }
    }
    //返回最接近的，即最佳的候选
    return closest;
}
```



```java
boolean isBetterCandidate(int direction, Rect source, Rect rect1, Rect rect2) {

    // to be a better candidate, need to at least be a candidate in the first
    // place :)
    if (!isCandidate(source, rect1, direction)) {
        return false;
    }

    // we know that rect1 is a candidate.. if rect2 is not a candidate,
    // rect1 is better
    if (!isCandidate(source, rect2, direction)) {
        return true;
    }

    // if rect1 is better by beam, it wins
    if (beamBeats(direction, source, rect1, rect2)) {
        return true;
    }

    // if rect2 is better, then rect1 cant' be :)
    if (beamBeats(direction, source, rect2, rect1)) {
        return false;
    }

    // otherwise, do fudge-tastic comparison of the major and minor axis
    return (getWeightedDistanceFor(
                    majorAxisDistance(direction, source, rect1),
                    minorAxisDistance(direction, source, rect1))
            < getWeightedDistanceFor(
                    majorAxisDistance(direction, source, rect2),
                    minorAxisDistance(direction, source, rect2)));
}
```





根据方向，**选择判断View是不是一个合适的，比如焦点要向下移动，则View的位置应该在当前焦点View的位置的下方。**

```java
    boolean isCandidate(Rect srcRect, Rect destRect, int direction) {
        switch (direction) {
            case View.FOCUS_LEFT:
                return (srcRect.right > destRect.right || srcRect.left >= destRect.right) 
                        && srcRect.left > destRect.left;
            case View.FOCUS_RIGHT:
                return (srcRect.left < destRect.left || srcRect.right <= destRect.left)
                        && srcRect.right < destRect.right;
            case View.FOCUS_UP:
                return (srcRect.bottom > destRect.bottom || srcRect.top >= destRect.bottom)
                        && srcRect.top > destRect.top;
            case View.FOCUS_DOWN:
                return (srcRect.top < destRect.top || srcRect.bottom <= destRect.top)
                        && srcRect.bottom < destRect.bottom;
        }
        throw new IllegalArgumentException("direction must be one of "
                + "{FOCUS_UP, FOCUS_DOWN, FOCUS_LEFT, FOCUS_RIGHT}.");
    }

```



```java
boolean beamBeats(int direction, Rect source, Rect rect1, Rect rect2) {
    final boolean rect1InSrcBeam = beamsOverlap(direction, source, rect1);
    final boolean rect2InSrcBeam = beamsOverlap(direction, source, rect2);

    if (rect2InSrcBeam || !rect1InSrcBeam) {
        return false;
    }
    if (!isToDirectionOf(direction, source, rect2)) {
        return true;
    }

    // for horizontal directions, being exclusively in beam always wins
    if ((direction == View.FOCUS_LEFT || direction == View.FOCUS_RIGHT)) {
        return true;
    }        
	
    //比较哪个离当前View近
    return (majorAxisDistance(direction, source, rect1)
            < majorAxisDistanceToFarEdge(direction, source, rect2));
}
```



beamsOverlap判断view与当前焦点View是否有方向上的重合，若是左右移动焦点，则判断竖向是否有重叠

若是上下移动，则判断横向是否有重叠

```java
boolean beamsOverlap(int direction, Rect rect1, Rect rect2) {
    switch (direction) {
        case View.FOCUS_LEFT:
        case View.FOCUS_RIGHT:
            return (rect2.bottom > rect1.top) && (rect2.top < rect1.bottom);
        case View.FOCUS_UP:
        case View.FOCUS_DOWN:
            return (rect2.right > rect1.left) && (rect2.left < rect1.right);
    }
    throw new IllegalArgumentException("direction must be one of "
            + "{FOCUS_UP, FOCUS_DOWN, FOCUS_LEFT, FOCUS_RIGHT}.");
}
```

