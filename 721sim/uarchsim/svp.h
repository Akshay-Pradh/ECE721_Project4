#include <inttypes.h>
#include <stdio.h>
#include <vector>

typedef struct svp_entry {
    // tag
    // confidence
    // retired_value
    // stride
    // instance
}svp_entry;

typedef struct vpq_entry {
    uint64_t PC_tag;
    uint64_t PC_index;
    uint64_t value;
}vpq_entry;


class SVP_VPQ {
private:
    std::vector<svp_entry> SVP;
    std::vector<vpq_entry> VPQ;
    uint64_t vpq_head;
    uint64_t vpq_tail;
    uint64_t conf_max;
    uint64_t tag_bits;

public:
    // constructor
    SVP_VPQ(uint64_t vpq_size, uint64_t index_bits, uint64_t tag_bits, uint64_t conf);

    // Search SVP for tag
    bool search_svp(uint64_t PC_index, uint64_t tag);
    
    // Generate value prediction and confidence if hit in SVP
    std::pair<bool, uint64_t> svp_hit(uint64_t tag);

    // Allocate entry in VPQ, returns entry number
    uint64_t vpq_allocate(uint64_t index, uint64_t tag);

    // Deposit value in VPQ in Writeback
    void desposit(uint64_t entry, uint64_t val);

    // Pop VPQ head, return entry PC
    vpq_entry vpq_pop_head();

    // If SVP tag hit, train SVP entry, use value, decrement instance counter
    void train_svp(uint64_t value);

    // If SVP tag miss, replace entry
    void install_svp(uint64_t tag, uint64_t value);
};