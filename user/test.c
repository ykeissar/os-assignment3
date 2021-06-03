#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define PGSIZE 4096

/*
    struct page_access_info{
    uint64 page_address;             // Indetfire for pte
    uint access_counter;             //Indicates the number of access to the page; 
    uint64 loaded_at;                // Indicates the time the page was loaded to RAM; 
    int in_use ; 
    };

    Test Cases: 
        1- SCFIFO:
        * should return the first page loaded into ram, that its access bit -0 
            1- all pages with access_bit = 0            => should return p->ram_pages[i]
                                                            ram_pages[i]->loaded_at is minimal, at 1st iteration
            2- all pages with access_bit = 1            => should return p->ram_pages[i]
                                                            ram_pages[i]->loaded_at is minimal, at iteration 17
            3- all pages with access_bit = 1            => should return ram_pages[i] with max loaded_at 
                except from the last one
            4- order of saving to ram
                is corresponding to the array           => all 3 above should act the same
            5- order of saving to ram
                is not corresponding to the array       => all 3 above should act the same
            6- access pages 0-14 for 6 times            => should return page min_loaded_time(page i) 0<=i<15
                than access once page 15
        2- NFUA:
        * should return the page that was accessed last while considering the number of accesses
            1- all pages with 6 accesses                => should return the one was accessed first
            2- access pages 0-14 for 6 times            => should return page 15
                than access page 15
        3- LAPA:
        * should return the page that was least accessed, in case of equality, should return the one was accessed earlier
            1- all pages with 6 accesses                => should return the one was accessed first
            2- access pages 0-14 for 6 times            => should return page min_access_time(page_i) 0<=i<15
                than access once page 15

            
*/

//tries to access a page that is not belong to it
int test1(void){
    char *memo = malloc(PGSIZE*16);
    int i,j;
    int NUM_ITER = 20;

    for(j = 0 ; j < NUM_ITER ; j++){
        for(i = 0 ; i < 16 ; i++){
            memo[i*PGSIZE] = (char)j+i;
            memo[5*PGSIZE] = (char)j+i;
            memo[6*PGSIZE] = (char)j+i;
            memo[7*PGSIZE] = (char)j+i;
        }
    }
    uint all_pass = 1;
    for(i = 0 ; i < 16 ; i++){
        if(i >=5 && i<=7){
            all_pass &= (memo[i*PGSIZE] == (char)15+(NUM_ITER-1));
        }
        else{
            all_pass &= (memo[i*PGSIZE] == (char)i+(NUM_ITER-1));
        }
    }
    free(memo);
    return all_pass;
}

int test_fork(void){
    char *memo = malloc(PGSIZE*16);
    int i,j;
    int NUM_ITER = 5;
    int offset = 5;
    int cpid1,cpid2,cret1,cret2;
    uint all_pass = 1;
    int flag = 1;

    // for(i = 0 ; i < 16 ; i++){
    //     printf("page %d at %p\n",i,&memo[PGSIZE*i]);
    // }

    for(j = 0 ; j < NUM_ITER ; j++){
        for(i = 0 ; i < 16 ; i++){
            memo[i*PGSIZE] = (char)j+i;
            memo[5*PGSIZE] = (char)j+i;
            memo[6*PGSIZE] = (char)j+i;
            memo[7*PGSIZE] = (char)j+i;
        }
    }
    // printf("%d started\n",getpid());
    if((cpid1 = fork()) < 0){
        printf("FAILED - 1st fork\n");
        return 0;
    }
    // printf("%d after 1\n",getpid());

    for(j = 0 ; j < NUM_ITER ; j++){
        for(i = 0 ; i < 16 ; i++){
            memo[i*PGSIZE] = (char)j+i+offset;
            memo[5*PGSIZE] = (char)j+i+offset;
            memo[6*PGSIZE] = (char)j+i+offset;
            memo[7*PGSIZE] = (char)j+i+offset;
        }
    }
    
    if((cpid2 = fork()) < 0){
        printf("FAILED - 2nd fork\n");
        return 0;
    }    
    // printf("%d after 2\n",getpid());

    for(j = 0 ; j < NUM_ITER ; j++){
        for(i = 0 ; i < 16 ; i++){
            // if(getpid() == 4)
            //     printf("i:%d, %p <- %d\n",i,&memo[i*PGSIZE],(char)j+i+(offset*2));
            memo[i*PGSIZE] = (char)j+i+(offset*2);
            memo[5*PGSIZE] = (char)j+i+(offset*2);
            memo[6*PGSIZE] = (char)j+i+(offset*2);
            memo[7*PGSIZE] = (char)j+i+(offset*2);
        }
    }

    for(i = 0 ; i < 16 ; i++){
        if(i >=5 && i<=7){
            all_pass &= (memo[i*PGSIZE] == (char)15+(NUM_ITER-1)+(offset*2))|flag;
            // if(getpid() == 4)
            //     printf("i:%d -- %d ?= %d\n",i,memo[i*PGSIZE],(char)(15+(NUM_ITER-1)+(offset*2)));
        }
        else{
            all_pass &= (memo[i*PGSIZE] == (char)(i+(NUM_ITER-1)+(offset*2)))|flag;
            // if(getpid() == 4)
            //     printf("i:%d -- %d ?= %d\n",i,memo[i*PGSIZE],(char)(i+(NUM_ITER-1)+(offset*2)));

        }
    }

    // for tree processes- 
    //    3
    //  5   4
    //        6
    
    // case 3,5
    if(cpid1 != 0){
        // case 3
        if(cpid2 != 0){
            wait(&cret1);
            wait(&cret2);
        }
        // case 5
        else{
            exit(all_pass);
        }
    }
    // case 6,4
    else{
        // case 4
        if(cpid2 != 0){
            wait(&cret2);
            exit(all_pass && cret2);
        }
        // case 6
        else{
            exit(all_pass);
        }
    }
    free(memo);
    // printf("%d got to return ap:%d cret1:%d cret2:%d\n",getpid(),all_pass,cret1,cret2);
    return all_pass && cret1 && cret2;
}

struct test {
    int (*f)(void);
    char *s;
  } tests[] = {
    {test1,"test1"},
    {test_fork, "test_fork"},
    { 0, 0}, 
  };

int main(void){
    int res;
    for (struct test *t = tests; t->s != 0; t++) {
        printf("----------- test - %s -----------\n", t->s);
        res = t->f();
        if(res){
            printf("----------- %s PASSED -----------\n", t->s);
        }
        else{
            printf("----------- %s FAILED -----------\n", t->s);
        }
    }

    exit(0);
}
