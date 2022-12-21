#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sys/mman.h>

#define MAX_SIZE 100000000
#define SBRK_FAIL (void*)(-1)
#define LARGE_ENOUGH 128
#define KB 1024
#define MMAP_SIZE (128*KB) // 128kb
#define X64_BIT_IN_BYTES 8

struct MallocMetadata{
    size_t size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
    explicit MallocMetadata(size_t size_in, bool is_free_in = false, MallocMetadata* next_in = nullptr
            , MallocMetadata* prev_in = nullptr) : size(size_in), is_free(is_free_in)
            , next(next_in), prev(prev_in) {}
};

//dummy head of blocks which have been allocated using sbrk()
static MallocMetadata metaDataHead(0);

//dummy head of blocks which have been allocated using mmap()
static MallocMetadata mmapDataHead(0);


static bool check_if_splittable(MallocMetadata* pmeta, size_t size){
    return pmeta->size >= LARGE_ENOUGH + size + sizeof(MallocMetadata);
}

/*
 * this function checks if split is possible and if its then
 * it splits the block into 2 separate blocks each with Metadata of its own
 * in either case it returns pmeta, if non-free block was sent nullptr is returned.
 */
static void* splitter(void* p, size_t size){
    //for ease of use
    auto* pmeta = (MallocMetadata*)p;
    //we can't split blocks that aren't free
    if (!pmeta->is_free){
        return nullptr;
    }
    //if we can't split the block nothing more to do and we return pmeta
    if(!check_if_splittable(pmeta, size)){
        return pmeta;
    }

    //if the block is splittable we will create new MallocMetadata for it in the beggining
    //of the new splitted block
    void* new_p = (void*)((char*)p+sizeof(MallocMetadata)+size);
    //for ease of use
    auto* new_node = (MallocMetadata*)new_p;
    *new_node = MallocMetadata(pmeta->size - sizeof(MallocMetadata) - size);
    // |--pmeta--|<->|--next--| => |--pmeta--|<-|--new_node--|  (from pmeta)->|--next--|
    new_node->prev = pmeta;
    // |--pmeta--|<-|--new_node--|  (from pmeta)->|--next--| => |--pmeta--|<-|--new_node--|->|--next--|
    new_node->next = pmeta->next;
    // |--pmeta--|<-|--new_node--|->|--next--| => |--pmeta--|<-|--new_node--|<->|--next--|
    if(new_node->next != nullptr){
        new_node->next->prev = new_node;
    }
    // |--pmeta--|<-|--new_node--|<->|--next--| => |--pmeta--|<->|--new_node--|<->|--next--|
    pmeta->next = new_node;


    pmeta->size = size;
    pmeta->next->is_free = true;
    return pmeta;
}

static MallocMetadata* metaDataMergerNext(MallocMetadata* p){
    //in case there's a next block and it's free we will simply merge it into block p
    //and remove the p->next block
    if(p->next != nullptr && p->next->is_free){
        //we're freeing both the data segment (size) and also the metadata segment
        // |-p-|<->|-next-|<->|-next-|  => |-p-|-next-||<->|-next-|<->|-next-|
        p->size += sizeof(MallocMetadata) + p->next->size;
        if(p->next->next != nullptr){
            //|-p-|-next-||<->|-next-|
            p->next->next->prev = p;
        }
        //|-p-|-next-||->|-next-|
        p->next = p->next->next;
    }
    return p;
}

static MallocMetadata* metaDataMergerPrev(MallocMetadata* p){
    if(p->prev != &metaDataHead && p->prev->is_free){
        //we want to merge p into p->prev!
        p->prev->size += sizeof(MallocMetadata) + p->size;

        p->prev->next = p->next;
        if(p->next != nullptr){
            p->next->prev = p->prev;
        }
        //at this point p isn't referenced by any block and we can release the pointer
        p = p->prev;
    }
    return p;
}

/*
 * this function checks if next or prev blocks are free and
 * if they're then we will merge them into our block and return the pointer
 * to the beginning of the metadata of the new big block.
 * if the current block isn't free then we'll return nullptr.
 */
static void* metaDataMerger(MallocMetadata* p){
    //if the block isn't free we can't merge it with other blocks
    if(!p->is_free){
        return nullptr;
    }

    p = metaDataMergerNext(p);

    return metaDataMergerPrev(p);
}

/*
 * receives a pointer to the top block metadata and enlarging it to to size of "size".
 * will return nullptr if metaDataHead/nullptr was sent or if pmeta block isn't free.
 */
static void* expandTopBlock(MallocMetadata* pmeta, size_t size){
    //we can't enlarge the top block if it isn't free or if nullptr was sent
    //the compare to metaDataHead should always fail since we can't expand the dummy node!
    if(pmeta == nullptr || !pmeta->is_free || pmeta == &metaDataHead){
        return nullptr;
    }
    //we need to add to the top block only the difference between the wanted size
    //and the already available size
    if(sbrk(size-pmeta->size) == SBRK_FAIL){
        return nullptr;
    }

    //updated the new size of the block
    pmeta->size = size;

    return (void*)pmeta;
}


static size_t alignToEight(size_t size){
    if((size % X64_BIT_IN_BYTES) == 0) return 0;
    return X64_BIT_IN_BYTES - (size % X64_BIT_IN_BYTES);
}

/*
 * creates new area for the requested size
 * updates the mmap linked list
 * returns the address after the metadata
 */
static void* smmap(size_t size){
    //creating new area in memory using mmap with extra space for metadata
    void* p = mmap(nullptr, size + sizeof(MallocMetadata), PROT_READ | PROT_WRITE,
            MAP_ANONYMOUS | MAP_SHARED, -1, 0);

    //using the beginning of the memory for saving the metadata
    auto* new_node = (MallocMetadata*)p;
    *new_node = MallocMetadata(size);

    //finding the last node in mmap linked list
    MallocMetadata* pmet = &mmapDataHead;
    while(pmet != nullptr){
        if(pmet->next == nullptr) break;
        pmet = pmet->next;
    }

    //connecting the new_node to the end of the list
    pmet->next = new_node;
    new_node->prev = pmet;

    return new_node+1;
}

static void smunmap(MallocMetadata* pmeta){
    //disconnecting pmeta from mmap linked list
    pmeta->prev->next = pmeta->next;
    if(pmeta->next != nullptr){
        pmeta->next->prev = pmeta->prev;
    }

    munmap((void*)pmeta, pmeta->size + sizeof(MallocMetadata));
}

void* smalloc(size_t size){

    //check for invalid input
    if(size == 0 || size > MAX_SIZE){
        return nullptr;
    }

    size += alignToEight(size);

    MallocMetadata* it;

    if(size >= MMAP_SIZE){
        it = &mmapDataHead;
        return smmap(size);
    }

    it = &metaDataHead;

    //check if the requested size can be fitted in a free'd allocated block
    while(it != nullptr){
        if(it->is_free && it->size >= size){
            splitter((void*)it, size);
            it->is_free = false;
            //returns the address after the metaData.
            return it+1;
        }
        //used for saving the last node for later use to avoid going over the list twice
        if(it->next == nullptr) break;
        it = it->next;
    }

    //it can't be null since it starts by pointing to metaDataHead
    //and the while loop breaks before it is changed to nullptr.
    if(it->is_free && it != &metaDataHead){
        //if we're here then for sure it->size < size ; so we can just enlarge this block
        expandTopBlock(it, size);
        it->is_free = false;
        return it+1;
    }

    void* ret = sbrk(size+sizeof(MallocMetadata));

    if(ret == SBRK_FAIL)
        return nullptr;

    auto* metaData = (MallocMetadata*)ret;
    *metaData = MallocMetadata(size);

    //update the list of allocated areas
    metaData->prev = it;
    it->next = metaData;

    //returns the address after the metaData.
    return metaData+1;
}

void* scalloc(size_t num, size_t size){
    //everything in scalloc is the same as in smalloc so we will use smalloc
    //and then set the necessary bytes to 0.
    void* ret = smalloc(num*size);
    if(ret == nullptr){
        return nullptr;
    }
    //setting the block to zeros
    std::memset(ret, 0 , size*num);
    return ret;
}

void sfree(void* p){
    //if nullptr was sent nothing to do
    if (p == nullptr){
        return;
    }
    //for ease of use
    MallocMetadata* pmeta = ((MallocMetadata*)(p)-1);

    //-1 to move the pointer to the metaData, then marking this block as free
    pmeta->is_free = true;
    if(pmeta->size >= MMAP_SIZE){
         return smunmap(pmeta);
    }
    metaDataMerger(pmeta);
}

void* srealloc(void* oldp, size_t size){
    if (size == 0 || size > MAX_SIZE){
        return nullptr;
    }

    //in case oldp is nullptr we need only to allocate new block of size size
    if (oldp == nullptr){
        return smalloc(size);
    }


    //for ease of use
    MallocMetadata* pmeta = (MallocMetadata*)(oldp)-1;

    //check if the block was allocated using mmap and if was then need to delete the block
    //and allocate new one using mmap
    if(pmeta->size >= MMAP_SIZE){
        void* newp = smmap(size);
        size_t size_to_copy;
        size >= pmeta->size ? size_to_copy = pmeta->size : size_to_copy = size;
        newp = std::memmove(newp, oldp, size_to_copy);
        smunmap(pmeta);
        return newp;
    }


    //checks if the wanted new size is smaller than than older size then no need to do nothing
    //as the old block is large enough
    if(pmeta->size >= size){
        //just to make splitter to accept this block
        pmeta->is_free = true;
        pmeta = (MallocMetadata*)splitter(pmeta, size);
        pmeta->is_free = false;
        return oldp;
    }


    //if the block is "wilderness" we need only to enlarge it using sbrk()
    if(pmeta->next == nullptr){
        sbrk(size - pmeta->size);
        pmeta->size = size;
        return oldp;
    }

    //checking if merging with prev block is enough
    if(pmeta->prev != &metaDataHead && pmeta->prev->is_free){
        if(pmeta->size + pmeta->prev->size + sizeof(MallocMetadata) >= size){
            pmeta = metaDataMergerPrev(pmeta);
            //in case we merged with block which is too big
            auto* newp = (MallocMetadata*)std::memmove(pmeta+1, oldp, size);
            newp = (MallocMetadata*)splitter(newp-1, size);
            return newp+1;
        }
    }

    //checking if merging with next block is enough
    if(pmeta->next != nullptr && pmeta->next->is_free){
        if(pmeta->size + pmeta->next->size + sizeof(MallocMetadata) >= size){
            pmeta = metaDataMergerNext(pmeta);
            //in case we merged with block which is too big
            splitter(pmeta, size);
            pmeta->is_free = false;
            return pmeta+1;
        }
    }

    //checking if merging with both next and prev is enough
    if(pmeta->prev != &metaDataHead && pmeta->prev->is_free
        && pmeta->next != nullptr && pmeta->next->is_free
        && pmeta->size + pmeta->prev->size + pmeta->next->size + (2*sizeof(MallocMetadata)) >= size){
        pmeta = metaDataMergerPrev(pmeta);
        pmeta = metaDataMergerNext(pmeta);
        //in case we merged with block which is too big
        auto* newp = (MallocMetadata*)std::memmove(pmeta+1, oldp, size);
        newp = (MallocMetadata*)splitter(newp-1, size);
        pmeta->is_free = false;
        return newp+1;
    }

    void* newp = smalloc(size);

    //if newp is nullptr then sbrk failed so we will return nullptr and not freeing the oldp
    if (newp == nullptr){
        return nullptr;
    }

    newp = std::memmove(newp, oldp, pmeta->size);
    sfree(oldp);

    return newp;
}

size_t _num_free_blocks(){
    size_t freeBlocks = 0;
    //first node is a dummy
    MallocMetadata* it = metaDataHead.next;

    while(it != nullptr){
        //counting only free blocks
        if(it->is_free){
            ++freeBlocks;
        }
        it = it->next;
    }
    return freeBlocks;
}

size_t _num_free_bytes(){
    size_t freeBytes = 0;
    //first node is dummy
    MallocMetadata* it = metaDataHead.next;

    while(it != nullptr){
        //counting only bytes of free bytes
        if(it->is_free){
            freeBytes += it->size;
        }
        it = it->next;
    }

    return freeBytes;
}

size_t _num_allocated_blocks(){
    size_t allocatedBlocks = 0;
    //first node is dummy
    MallocMetadata* it = metaDataHead.next;

    while(it != nullptr){
        //counting all blocks free and used
        ++allocatedBlocks;
        it = it->next;
    }

    //same thing for the mmap linked list
    it = mmapDataHead.next;
    while(it != nullptr){
        ++allocatedBlocks;
        it = it->next;
    }

    return allocatedBlocks;
}

size_t _num_allocated_bytes(){
    size_t freeBytes = 0;
    //first node is dummy
    MallocMetadata* it = metaDataHead.next;

    while(it != nullptr){
        //counting all sizes free and used
        freeBytes += it->size;
        it = it->next;
    }

    //same thing for mmap linked list
    it = mmapDataHead.next;
    while(it != nullptr){
        freeBytes += it->size;
        it = it->next;
    }

    return freeBytes;
}

size_t _num_meta_data_bytes(){
    return _num_allocated_blocks() * sizeof(MallocMetadata);
}

size_t _size_meta_data(){
    return sizeof(MallocMetadata);
}
