#include "nvheap.h"
#include "nvhutils.h"
#include "nvhtx.h"
#include <libpmem.h>
using namespace std;

void * nvh_base_addr = NULL;    // Base Virtual address returned after mapping
int debug_mode;

int nvh_init (const char * file, const char * nvh_name) {
    int fd;
    uint64_t pname_h;           // Hash of nvh_name will be read to here from fd
    uint64_t pname_hh;          // Hash to hash of nvh_name will be read in this from fd
    char pname_h_str[32];       // string correspond to pname_h
    debug_mode = 0;             // Setting debug mode to 0 = no dubug output

    if (!nvh_name || !strlen(nvh_name)) {
        print_err("nvh_init", "Blank nvh_name given");
        return -1;
    }

    if (!file || !strlen(file)) {
        print_err("nvh_init", "Blank file name given");
        return -1;
    }

    if ((fd = open(file, O_RDONLY)) < 0) {  // Unable to open the file
        if ((fd = open(file, O_RDWR|O_CREAT, 0644)) < 0) {  // Trying to create file
            print_err ("nvh_init", "Invalid file name given");      // Opaining and creating file failed
            return -1;
        }
        else {
            // File open failed but create success.
            print_err("nvh_init", "Creating NV-Heap");  // DEBUG, REMOVE
            close (fd);
            return (nvh_create(file, nvh_name));
        }
    }

    // reading name_hash and hash of name_hash from file
    if ((read(fd, &pname_h, sizeof(uint64_t)) < sizeof(uint64_t)) || (read (fd, &pname_hh, sizeof(uint64_t)) < sizeof(uint64_t))) {
        // Empty file. Let's call nvh_create;
        print_err("nvh_init", "Creating NV-Heap");  // DEBUG, REMOVE
        close(fd);
        return nvh_create(file, nvh_name);
    }

    u64itostr(pname_h, pname_h_str);    // Converting nvh_name hash int to string
    // Both hash are valid

    if (pname_h != hash64(nvh_name)) {
        print_err("nvh_init", "Wrong NVH-Name Given. Destroying old NV-Heap and Creating new NV-Heap");
        close(fd);
        return nvh_create(file, nvh_name);
    }
    if (pname_h == hash64(nvh_name) && pname_hh == hash64(pname_h_str)) {
        print_err("nvh_init", "Loading NV-Heap");
        close(fd);
        return nvh_open (file);
    }

    // cerr << pname_hh << " : " << hash64(pname_h_str) << endl;    // DEBUG, REMOVE

    // hash(Name_hash) stored in NVH is not same with hash_hash stored in NVH
    if (pname_hh != hash64(pname_h_str)) {
        print_err("nvh_init", "Corrupted file. Destroying Old NV-Heap. Creating new NV-Heap");
        close(fd);
        return nvh_create (file, nvh_name);
    }

    // Default case
    // cerr << "Default\n";     // DEBUG, REMOVE
    return -1;
}

//********-----convert it to int with 0 and -1 output, program don't need pmem base
int nvh_create(const char *file, const char * nvh_name) {
    // cerr << "In nvh_create\n";   // DEBUG, REMOVE
    size_t len = TOTAL_SIZE;   // Length of one unit of pmem in byte
    int flags = 0;                              // Flag for nvh_map_file, not in use, REMOVE
    mode_t mode = 0644;                         // File access or creation permissions
    size_t mapped_len;                          // Actual mapped length will be stored here
    int is_pmem;                                // pmem or not will be stored here
    char pname_h_str[32];                       // A string to store pmem hash after int to str conv
    struct nvh_length nvh_len;                  // A 8-byte structure to hold pmem length
    if (truncate(file, (off_t)len) < 0) {
        print_err ("nvh_create", "Unable to set the file to proper length");
    }

    // nvh_base_addr is global defined and initialised to NULL in pmem.h
    nvh_base_addr = (char *)pmem_map_file(file, len, PMEM_FILE_CREATE, mode, &mapped_len, &is_pmem);

    // perror("nvh_map_file");
    if (!nvh_base_addr) {
        print_err("nvh_create", "nvh_map_file returned NULL");
        return -1;
    }

    nvh_tx_address = (void*) ((char*) nvh_base_addr + NVH_LENGTH);

    memset (nvh_base_addr, 0, TOTAL_SIZE);      // Filling the new file with 0
    nvh_persist ();                             // Persisting write

    *(uint64_t *)nvh_base_addr = hash64(nvh_name);          // Hash of pmem name 8-byte
    u64itostr(hash64(nvh_name), pname_h_str);
    *((uint64_t *)nvh_base_addr + 1) = hash64(pname_h_str); // Hash-hash of pmem name 8-byte
    nvh_len.length = (uint64_t)len;
    *((uint64_t *)nvh_base_addr + 2) = *(uint64_t *)(&nvh_len); // Current length 8-byte

    // Marking header as occupied Start = 24 for hash, hash_h, length
    // Header 1024 Byte = 0-127 bit in bitmap. 128th bit for root pointer
    set_bit_range(((uint64_t *)nvh_base_addr), 24 * sizeof(uint64_t), 24 * sizeof(uint64_t) + 128 , 1);
    *((int64_t *)nvh_base_addr + 128) = -1;
    // Marking root pointer as null

    nvh_persist ();
    return 0;
}

int nvh_open(const char *file) {
    nvh_base_addr = (char *)pmem_map_file(file, TOTAL_SIZE, PMEM_FILE_CREATE, 0644, NULL, NULL);
    nvh_persist ();
    if (!nvh_base_addr)
        return -1;
    else {
        nvh_tx_address = (void*) ((char*) nvh_base_addr + NVH_LENGTH);
        tx_fix();
        return 0;
    }
}

// Return offset of root pointer
void * nvh_get_root () {
    if ((*((int64_t *)nvh_base_addr + 128)) == -1)
        return NULL;
    return NVPtr(*((uint64_t *)nvh_base_addr + HEADER_LEGTH / ALLOC_RATIO_BYTE_BIT)).dptr();
}

// WILL Return 0 on success, -1 on error. Now 1 always
int nvh_set_root (void *address) {
    NVPtr root;
    root = address;
    tx_root();
    (*((int64_t *)nvh_base_addr + 128)) = root.get_offset();
    nvh_persist ();
    return 0;
}

// This checks whether the positionth BIT is 0 or 1 from base.
int test_bit (uint64_t * base, int position) {
    if (base[position/(sizeof(uint64_t) * 8)] & ((uint64_t)1) << position % (sizeof(uint64_t) * 8))
        return 1;
    else return 0;
}

// This write bit from start to end of base. If bit != 1 or 0 return -1, else 0
int set_bit_range (uint64_t *base, int start, int end, int bit) {
    int i;
    if (bit != 0 && bit != 1)
        return -1;
    if (bit) {
        for (i = start; i <= end; i++) {
            base[i / (sizeof(uint64_t) * 8)] |= (((uint64_t)1) << (i % (sizeof(uint64_t) * 8)));
        }
    }
    if (!bit) {
        for (i = start; i <= end; i++) {
            base[i / (sizeof(uint64_t) * 8)] &= ~(((uint64_t)1) << (i % (sizeof(uint64_t) * 8)));
        }
    }
    nvh_persist ();
    return 0;
}

void *nvh_malloc (int size) {
    void * bm_base;         // base address of bitmap
    void *bm_start;         // Start address of working bitmap
    int header_allocation;  // Already allocated size of header in BYTE
    int max_i;              // Maximum offset in BIT
    int start_i, end_i;     // Star and end offset of selected range in BIT
    int round = (sizeof(uint64_t));     // Smallest allocation size
    int alloc_size = (round * (size / round)) + round *(round && (size % round)); // Actual allocn in BYTE

    // cout << "nvh_malloc: alloc_size = " << alloc_size <<endl;

    bm_base = (char *)nvh_base_addr + 3 * sizeof(uint64_t);
    header_allocation = HEADER_LEGTH / ALLOC_RATIO_BYTE_BYTE;
    bm_start = (char *)bm_base + header_allocation;
    max_i = NVH_LENGTH/ALLOC_RATIO_BYTE_BIT - 128 - 1;     // -1 bcz last offset = 4097 and 128 filled

    for (start_i = end_i = 0 ; start_i <= max_i && end_i <= max_i;) {
        if (test_bit((uint64_t *)bm_start, end_i) == 0) {
            end_i ++;
            if ((end_i - start_i)*8 >= alloc_size)  // NOTE: 1bit per byte
            {
                void* address = (void *)((char *)nvh_base_addr + HEADER_LEGTH + start_i * sizeof(uint64_t));
                tx_malloc(address, alloc_size);
                set_bit_range((uint64_t *)bm_start, start_i, end_i - 1, 1);
                return address;
            }
        }
        else {
            if (start_i == end_i){
                start_i ++;
                end_i ++;
            }
            else{
                start_i = end_i;
            }
        }
    }
    print_err("nvh_malloc","returning null");
    nvh_persist ();
    return NULL;
}

// Returns currently used size of the total memory in byte includein header etc
uint64_t nvh_meminfo () {
    void * bm_base = (char *)nvh_base_addr + 3 * sizeof(uint64_t);  // base address of bitmap
    int total_bits = NVH_LENGTH/ALLOC_RATIO_BYTE_BIT;    //Total no of bits in bitmap
    int index;
    uint64_t used_bits = 0;  // Number of used bits
    for (index = 0; index < total_bits; index++) {
        if (test_bit((uint64_t *)bm_base, index) == 0)
            continue;
        used_bits++;
    }
    return (used_bits * ALLOC_RATIO_BYTE_BIT);
}

// Currently always returning 0, Latter it'll check some error etc and return -1 for error
int nvh_free (void * address, int size) {
    tx_free (address, size);
    int alloc_size, round;
    int start_i;

    round = sizeof(uint64_t);
    void * bm_base = (char *)nvh_base_addr + 3 * sizeof(uint64_t);
    alloc_size = (round * (size / round)) + round *(round && (size % round));
    start_i = ((char *)address - (char * )nvh_base_addr)/8;
    bzero(address, size);
    set_bit_range ((uint64_t *)bm_base, start_i, start_i + alloc_size / 8 - 1, 0);
    nvh_persist ();
    return 0;
}

int nvh_close () {
    if (!nvh_base_addr)
        return -1;
    nvh_persist ();
    return pmem_unmap (nvh_base_addr, (size_t) TOTAL_SIZE);
}

void nvh_persist () {
    if (!nvh_base_addr)
        return;
    pmem_persist(nvh_base_addr, NVH_LENGTH);
}
