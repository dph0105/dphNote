#include <iostream>
#include "android/utils/RefBase.h"
#include "android/RefBase.cpp"
using namespace android;

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

    // void  onFirstRef(){};

    // void  onLastStrongRef(const void* id){};

    // bool  onIncStrongAttempted(uint32_t flags, const void* id){
    //     return true;
    // };

    // void  onLastWeakRef(const void* id){};
};

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
