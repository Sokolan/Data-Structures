#include <stdlib.h>

#define DIVISION 10
//used to store the address of 1st block, not necessary the beginning of memory_pool
int32_t* first_block_ptr;

/*
 * returns pointer to the first free sub-block
 * returns null in-case no free sub-blocks left
 */
int32_t* my_malloc(void){
    //in-case of DIVISION equal to 1 we only need to mark first_block_ptr as used (NULL)
    if(DIVISION == 1){
        int32_t* ret_val = first_block_ptr;
        first_block_ptr = NULL;
        return ret_val;
    }
    //if first_block_ptr is null that means that there are no free sub-blocks left
    if(first_block_ptr = NULL){
        return NULL;
    }
    /*
     * if offset is 0, set first_block_ptr to null to indicate all data been used
     * and return its location
    */
    if(*first_block_ptr == 0){
        int32_t* ret_val = first_block_ptr;
        first_block_ptr = NULL;
        return ret_val;
    }
    int32_t* tmp = first_block_ptr;
    //finding the last non 0 offset as the element after that will be the block which we'll return.
    while(*(tmp+(*tmp)) != 0){
        tmp += *tmp;
    }
    int32_t* ret_val = tmp+(*tmp);
    *tmp = 0;
    return  ret_val;
}


void my_free(int32_t* ptr){
    //in-case of DIVISION equal to 1 we only need to mark first_block_ptr as free
    if(DIVISION == 1){
        first_block_ptr = ptr;
        return;
    }
    //if all blocks were used, use the ptr as the base of offset chain.
    if(first_block_ptr == NULL){
        first_block_ptr = ptr;
        *first_block_ptr = 0;
        return;
    }
    int32_t* tmp = first_block_ptr;
    while(*tmp != 0){
        tmp += *tmp;
    }
    //saving the offset to the new last element and setting its offset to 0.
    *tmp = ptr-tmp;
    *ptr = 0;
}

/*
 * returns pointer to first block in-case of success
 * THIS BLOCK CANNOT BE USED, ONLY BLOCKS USED BY my_malloc.
 * returns NULL in-case of memory allocation failure
 */
int32_t* memory_init(int32_t memory_size){
    first_block_ptr = (int32_t*) malloc(memory_size);
    if(first_block_ptr == NULL){
        return NULL;
    }
    //in-case DIVISION is set to 1 we'll handle it differently
    if(DIVISION == 1){
        return first_block_ptr;
    }
    //save the address to the beginning of the memory pool to return it in the end
    int32_t* ret_val = first_block_ptr;

    //each element will store the offset to next free element.
    int32_t* tmp = first_block_ptr;
    for(int i = 0 ; i < DIVISION-1 ; ++i){
        //the offset to next element is constant
        *tmp = memory_size/DIVISION;
        tmp += memory_size/DIVISION;
    }
    //the last element offset will be 0 as it's the last element.
    *tmp = 0;

    return ret_val;
}