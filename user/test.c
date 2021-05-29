#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define PGSIZE 4096 // bytes per page

//fork + correct initializations

struct pgtest{
    int id;
    int accessed;
    int should_be_selected;
};

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
void test_unvalid_access(void){
    char *memo = malloc(PGSIZE);
    printf("memo is %p\n",memo);
    memo[2*PGSIZE]='l';
}

//tries to access a page that is not belong to it
void testtt(void){
    char *memo = malloc(PGSIZE*16);
    int i,j;
    for(i = 0 ; i < 16 ; i++){
        printf("page %d at %p\n",i,&memo[PGSIZE*i]);
    }
    for(j = 0 ; j < 10 ; j++){
        for(i = 0 ; i < 16 ; i++){
            memo[i*PGSIZE] = '1';
            memo[5*PGSIZE] = '2';
            memo[6*PGSIZE] = '2';
            memo[7*PGSIZE] = '2';

        }
        printf("\n-------------------loop %d----------------------\n",j);
    }
}

//only one page
void test_one_page(void){
    char *memo = malloc(PGSIZE);
    printf("memo is %p\n",memo);
    *memo='l';
}

//proc uses more than 16 pages in ram, test if selection algo is applied
void test_apply_selection(void){
    char *memo = malloc(MAX_PSYC_PAGES * PGSIZE);
    char *another_page = malloc(PGSIZE);
    printf("memo is %p\n another page is %p\n",memo,another_page);
    int i;
    for(i=0; i<16; i++){
        printf("accessing %d, address: %p\n",i, &memo[i*PGSIZE]);
        *memo ='l';
    }
    *another_page = 'l';

}

//selection of right page for each algorithm
// allocates 16 pages, uses again all pages except page4, upon new page, selected = page 4
void test_correct_selection(void){
    char* memo = malloc(MAX_PSYC_PAGES * PGSIZE);
    int i;
    for(i=0; i<16; i++){
        memo[i*PGSIZE] = 'l';
    }
    for(i=0; i<16; i++){
        if(i != 3){
            memo[i*PGSIZE]='l';
        }
    }
    char *another_page = malloc(PGSIZE);
    *another_page = 'l';

}

struct test {
    void (*f)(void);
    char *s;
  } tests[] = {
    // {test_apply_selection, "test_apply_selection"},
    // {test_one_page, "test_one_page"},
    {testtt,"testtt"},
    // {test_unvalid_access, "test_unvalid_access"},
    // {test_correct_selection, "test_correct_selection"},
    { 0, 0}, 
  };

int main(void){
    for (struct test *t = tests; t->s != 0; t++) {
        printf("----------- test - %s -----------\n", t->s);
        t->f();
    }

    exit(0);
}
