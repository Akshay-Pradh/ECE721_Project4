#include <inttypes.h>
#include <stdio.h>
#include <vector>
#include "payload.h"

typedef struct svp_entry {
    bool valid;
    uint64_t tag;
    uint64_t confidence;
    uint64_t ret_val;
    int64_t stride;
    int64_t inst;
}svp_entry;

typedef struct vpq_entry {
    uint64_t PC_tag;
    uint64_t PC_index;
    uint64_t value;
    bool valid;
}vpq_entry;


class SVP_VPQ {
private:


    // Data structure for VPQ
    std::vector<vpq_entry> VPQ;
    // uint64_t vpq_head;
    // uint64_t vpq_tail;
    uint64_t vpq_count;     // keep track of number of VPQ entries

    // Other private variables
    uint64_t conf_max;
    uint64_t tag_bits;
    uint64_t index_bits;

public:
    // constructor
    SVP_VPQ(uint64_t vpq_size, uint64_t index_bits, uint64_t tag_bits, uint64_t conf);

    // Return index from PC 
    uint64_t get_index(uint64_t PC);

    // Return tag from PC
    uint64_t get_tag(uint64_t PC);

    // Walk VPQ H to T
    uint64_t walk_VPQ(uint64_t index, uint64_t tag);

    // Count num entries in the VPQ
    uint64_t vpq_num_entries();

    // Count num free entries in the VPQ
    uint64_t vpq_free_entries();

    // Search SVP for tag
    bool search_svp(uint64_t PC_index, uint64_t tag);
    
    // Generate value prediction and confidence if hit in SVP
    void svp_hit(payload_t* instr, uint64_t index, bool oracle_mode, int64_t oracle_val);

    // Allocate entry in VPQ, returns entry number
    uint64_t vpq_allocate(uint64_t index, uint64_t tag);

    // Deposit value in VPQ in Writeback
    void vpq_deposit(uint64_t entry, uint64_t val);

    // Pop VPQ head, return entry PC
    vpq_entry vpq_pop_head();

    // If SVP tag hit, train SVP entry, use value, decrement instance counter
    void train_svp(uint64_t value, uint64_t index);

    // If SVP tag miss, replace entry
    void install_svp(uint64_t tag, uint64_t value, uint64_t index);

    uint64_t vpq_head;
    uint64_t vpq_tail;

    // Data structure for SVP
    std::vector<svp_entry> SVP;
};