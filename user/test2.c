#include "kernel/types.h"
#include "user.h"

#define CHILD_NUM 2
#define PGSIZE 4096
#define MEM_SIZE 10000
#define SZ 1200

#define PRINT_TEST_START(TEST_NAME)   printf("\n----------------------\nstarting test - %s\n----------------------\n",TEST_NAME);
#define PRINT_TEST_END(TEST_NAME)   printf("\nfinished test - %s\n----------------------\n",TEST_NAME);


void child_test(){
    PRINT_TEST_START("child test");
    if(!fork()){
        int children[CHILD_NUM];
        int* arr[CHILD_NUM];
        int ind=0;
        for(int i=0;i<CHILD_NUM;++i){
            arr[i] = (int*)(malloc(MEM_SIZE*sizeof(int)));
            children[i] = fork();
            ind = children[i]?ind:i+1;
            for(int j=0;j<MEM_SIZE;++j){
                arr[i][j] = children[i];
            }
        }
        for(int i=ind;i<CHILD_NUM;++i){
        wait(0);
        }
        for(int i=0;i<CHILD_NUM;++i){
            int sum = 0;
            for(int j=0;j<MEM_SIZE;++j){
                sum = sum + arr[i][j];
            }
            sum = sum/MEM_SIZE;
            free(arr[i]);
            printf("arr[%d] avg = %d\n",i,sum);
        }
        exit(0);
    }else{
        wait(0);
    }
    PRINT_TEST_END("child test");
}

void alloc_dealloc_test(){
    PRINT_TEST_START("alloc dealloc test");
    printf("alloc dealloc test\n");
    if(!fork()){
        int* arr = (int*)(sbrk(PGSIZE*20));
        for(int i=0;i<PGSIZE*20/sizeof(int);++i){arr[i]=0;}
        sbrk(-PGSIZE*20);
        printf("dealloc complete\n");
        arr = (int*)(sbrk(PGSIZE*20));
        for(int i=0;i<PGSIZE*20/sizeof(int);++i){arr[i]=2;}
        for(int i=0;i<PGSIZE*20/sizeof(int);++i){
            if(i%PGSIZE==0){
                printf("arr[%d]=%d\n",i,arr[i]);
            }
        }
        sbrk(-PGSIZE*20);
        exit(0);
    }else{
        wait(0);
    }
    PRINT_TEST_END("alloc dealloc test");
}

void advance_alloc_dealloc_test(){
    PRINT_TEST_START("advanced alloc dealloc test");
    if(!fork()){
        int* arr = (int*)(sbrk(PGSIZE*20));
        for(int i=0;i<PGSIZE*20/sizeof(int);++i){arr[i]=0;}
        int pid=fork();
        if(!pid){
            sbrk(-PGSIZE*20);
            printf("dealloc complete\n");
            printf("should cause segmentation fault\n");
            for(int i=0;i<PGSIZE*20/sizeof(int);++i){
              arr[i]=1;
            }
            printf("test failed\n");
            exit(0);
        }
        wait(0);
        int sum=0;
        for(int i=0;i<PGSIZE*20/sizeof(int);++i){sum=sum+arr[i];}
        sbrk(-PGSIZE*20);
        int father=1;
        char* bytes;
        char* origStart = sbrk(0);
        int count=0;
        int max_size=0;
        for(int i=0;i<20;++i){
            father = father && fork()>0;
            if(!father){break;}
            if(father){
                bytes = (char*)(sbrk(PGSIZE));
                max_size = max_size+PGSIZE;
                for(int i=0;i<PGSIZE;++i){bytes[i]=1;}
            }
        }
        for(int i=0;i<max_size;++i){
            count = count + origStart[i];
        }
        printf("count:%d\n",count);
        if(father){
            for(int i=0;i<20;++i){wait(0);}
        }
        exit(0);
    }else{
        wait(0);
    }
    PRINT_TEST_END("advanced alloc dealloc test");
}

void exec_test(){
    PRINT_TEST_START("exec test");
    if(!fork()){
        printf("allocating pages\n");
        int* arr = (int*)(malloc(sizeof(int)*5*PGSIZE));
        for(int i=0;i<5*PGSIZE;i=i+PGSIZE){
            arr[i]=i/PGSIZE;
        }
        printf("forking\n");
        int pid = fork();
        if(!pid){
            char* argv[] = {"myMemTest","exectest",0};
            exec(argv[0],argv);
        }else{
            wait(0);
        }
        exit(0);
    }else{
        wait(0);
    }
    PRINT_TEST_END("exec test");
}

void exec_test_child(){
    printf("child allocating pages\n");
    int* arr = (int*)(malloc(sizeof(int)*5*PGSIZE));
    for(int i=0;i<5*PGSIZE;i=i+PGSIZE){
      arr[i]=i/PGSIZE;
    }
    printf("child exiting\n");
}

void priority_test(){
    PRINT_TEST_START("priority test");
    if(!fork()){
        int* arr = (int*)(malloc(sizeof(int)*PGSIZE*6));
        for(int i=0;i<PGSIZE/sizeof(int);++i){
            int accessed_index = i+((i%2==0)?0:i%(6*sizeof(int)))*(PGSIZE/sizeof(int));
            arr[accessed_index]=1;
            if(i%10==0){sleep(1);}
        }
        for(int i=0;i<6*sizeof(int);++i){
            int sum=0;
            for(int j=0;j<PGSIZE/sizeof(int);++j){
                sum = sum + arr[i*PGSIZE/sizeof(int)+j];
            }
            printf("sum %d = %d\n",i,sum);
        }
        exit(0);
    }else{
        wait(0);
    }
    PRINT_TEST_END("priority test");
}

void fork_test(){
    PRINT_TEST_START("fork test");
    if(!fork()){
        char* arr = (char*)(malloc(sizeof(char)*PGSIZE*24));
        for(int i=PGSIZE*1;i<PGSIZE*2;++i){
            arr[i]=1;
        }
        printf("Creating first child\n");
        if(!fork()){
            for(int i=PGSIZE*1;i<PGSIZE*2;++i){
                arr[i]=1;
            }
            exit(0);
        }else{
            wait(0);
        }
        printf("Creating second child\n");
        if(!fork()){
            for(int i=PGSIZE*3;i<PGSIZE*4;++i){
                arr[i]=1;
            }
            exit(0);
        }else{
            wait(0);
        }
        exit(0);
    }else{
        wait(0);
    }
    PRINT_TEST_END("fork test");
}

int main(int argc, char** argv){
    if(argc>=1){
      if(strcmp(argv[1],"exectest")==0){
        exec_test_child();
        exit(0);
      }
    }
    fork_test();
    priority_test();
    exec_test();
    alloc_dealloc_test();
    advance_alloc_dealloc_test();
    child_test();
    exit(0);
}