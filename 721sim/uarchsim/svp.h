#include <inttypes.h>
#include <assert.h>
#include <stdio.h>
#include <vector>
#include "parameters.h"
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
    uint64_t branch_mask;
    bool valid;
}vpq_entry;


class SVP_VPQ {
private:
    // Data structure for SVP
    std::vector<svp_entry> SVP;

    // Data structure for VPQ
    std::vector<vpq_entry> VPQ;
    uint64_t vpq_head;
    uint64_t vpq_tail;
    uint64_t vpq_count;     // keep track of number of VPQ entries

    // Other private variables
    uint64_t conf_max;
    uint64_t tag_bits;
    uint64_t index_bits;

    // Statistics variables
    uint64_t vpmeas_ineligible;
    uint64_t vpmeas_eligible;
    uint64_t vpmeas_miss;
    uint64_t vpmeas_conf_corr;
    uint64_t vpmeas_conf_incorr;
    uint64_t vpmeas_unconf_corr;
    uint64_t vpmeas_unconf_incorr;

public:
    // constructor
    SVP_VPQ(uint64_t vpq_size, uint64_t index_bits, uint64_t tag_bits, uint64_t conf);
    
    // Dump SVP VPQ config + cost accounting data
    void svp_vpq_config(FILE *fp);

    // Dump SVP VPQ run stats:
    // vpmeas_ineligible, vpmeas_eligible
    // vpmeas_miss, vpmeas_conf_corr, vpmeas_conf_incorr, vpmeas_unconf_corr, vpmeas_unconf_incorr
    void svp_vpq_stats(FILE *fp, uint64_t commit_count);

    // Helper function for svp_vpq_config function
    uint64_t bits_to_encode(uint64_t n);

    void inc_counters(const payload_t *pay);

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

    // Return reference to head entry data from VPQ
    vpq_entry vpq_index_head();

    // Search SVP for tag
    bool search_svp(uint64_t PC_index, uint64_t tag);
    
    // Generate value prediction and confidence if hit in SVP
    void svp_hit(payload_t* instr, uint64_t index, bool oracle_mode, bool oracle_valid, int64_t oracle_val);

    // Allocate entry in VPQ, returns entry number
    uint64_t vpq_allocate(uint64_t index, uint64_t tag, uint64_t branch_mask);

    // Deposit value in VPQ in Writeback
    void vpq_deposit(uint64_t entry, uint64_t val);

    // Pop VPQ head, return entry PC
    vpq_entry vpq_pop_head();

    // If SVP tag hit, train SVP entry, use value, decrement instance counter
    void train_svp(uint64_t value, uint64_t index);

    // If SVP tag miss, replace entry
    void install_svp(uint64_t tag, uint64_t value, uint64_t index);

    // Rollback recovery for VPQ
    void vpq_rollback(uint64_t branch_ID);

    // Clearing vpq branch mask bits
    void clear_mask_bits(uint64_t branch_ID);

    // Flash clear instances in SVP and VPQ
    void flash_clear();
};
