#include "nvhtx.h"
#include "nvheap.h"
#include "nvhutils.h"
#include <cstdlib>


// TODO: What if these functions don't work atomically? Will it create a problem...check.
// TODO: Add checks to see that the addresses sent are inside the heap

// Transaction Status
tx_status::tx_status() {
    running = 0;
    count = 0;
}

tx_status::tx_status(uint32_t running, uint32_t count) {
    self.running = running;
    self.count = count;
}

void tx_status::retrieve_cur_status() {
    tx_status* cur = nvh_tx_address;
    running = cur->running;
    count = cur->count;
}

void tx_status::set_cur_status() {
    tx_status* cur = nvh_tx_address;
    cur->running = running;
    cur->count = count;
}


// Transaction Object
tx_obj::tx_obj() {
    type = USER_WRITE;
    offset = 0;
    size = 0;
}

tx_obj::tx_obj(uint type, int64_t offset, uint size) {
    self.type = type;
    self.offset = offset;
    self.size = size;
    if(type == USER_WRITE) {
        void* address = nvh_base_addr + offset;
        memcpy((void*) &self.buf, address, size);
    }
}

tx_obj::tx_obj(uint type, void* address, uint size) {
    self.type = type;
    self.offset = (int64_t) address - nvh_base_addr;
    self.size = size;
    if(type == USER_WRITE)
        memcpy((void*) &self.buf, address, size);
}

void tx_obj::undo() {
    void* address = (void*) (nvh_base_addr + offset);
    if(type == USER_WRITE)
        memcpy(address, (void*) buf, size);
    else if(type == MALLOC_CALL)
        nvh_free(address, size);
}

void tx_obj::write_to_heap() {
    tx_status stat;
    stat.retrieve_cur_status();
    void* address = nvh_tx_address + sizeof(tx_status) + stat->count*sizeof(tx_obj);
    memcpy(address, (void*) this, sizeof(tx_obj));
    stat->count++;
    stat.set_cur_status();
}


// To Begin a transaction
void tx_begin() {
    nvh_tx_address = nvh_base_addr + NVH_LENGTH;
    tx_status stat(1, 0);
    memcpy(nvh_tx_address, &stat, sizeof(tx_status));
}

void tx_add(NVptr ptr, uint size, uint flags=ONLY_IN_TX) {
    tx_status stat;
    stat.retrieve_cur_status();
    if(!stat.running) {
        if(flags == ALLOW_NO_TX)
            return;
        else {
            print_err("tx_add_direct", "Transaction Object cannot be added outside a transaction");
            exit(1);
        }
    }
    // Calculate buffer here
    tx_obj to(USER_WRITE, ptr.offset, size);
    to.write_to_heap();
}

void tx_add_direct(void* address, uint size, uint flags=ONLY_IN_TX) {
    tx_add((int64_t) (address - nvh_base_addr), size, flags)
}

void tx_commit() {
    tx_status stat;
    memcpy(nvh_tx_address, (void*) &stat, sizeof(tx_status));
}


void tx_malloc(void* address, uint size) {
    tx_status stat;
    stat.retrieve_cur_status();
    if(!stat.running)
        return;
    tx_obj to(MALLOC_CALL, address, size);
    to.write_to_heap();
}

void tx_fix() {
    tx_status stat;
    stat.retrieve_cur_status();
    if(stat.running) {
        cout << "Undo-ing broken transactions..." << endl;
        void* address = nvh_base_addr + NVH_LENGTH + sizeof(tx_status);
        for(int i = 0; i < stat.count; i++, address += sizeof(tx_obj)) {
            tx_obj* to = address;
            to.undo();
        }
        stat.count = 0;
        stat.running = 0;
        stat.set_cur_status();
        cout << "Fixed broken transactions" << endl;
        return;
    }
    cout << "No broken transactions found" << endl;
}