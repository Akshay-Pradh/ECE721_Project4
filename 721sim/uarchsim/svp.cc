#include "svp.h"
#include <assert.h>
#include <parameters.h>
#include <payload.h>

// Constructor definition
SVP_VPQ::SVP_VPQ(uint64_t vpq_size, uint64_t index_bits, uint64_t tag_bits, uint64_t conf) 
    : SVP(1 << index_bits), VPQ(vpq_size), vpq_head{0}, vpq_tail{0}, vpq_count{0}, tag_bits{tag_bits}, 
      conf_max{conf}, index_bits{index_bits} {
        // Initialize all SVP entries
        for (auto &entry: SVP) {
            entry.valid = false;
            entry.tag = 0;
            entry.confidence = 0;
            entry.ret_val = 0;
            entry.stride = 0;
            entry.inst = 0;
        }

        // Initialize all VPQ entries
        for (auto &entry: VPQ) {
            entry.valid = false;
            entry.PC_index = 0;
            entry.PC_tag = 0;
            entry.value = 0;
        }
}

uint64_t SVP_VPQ::get_index(uint64_t PC) {
    PC = PC >> 2;       // get rid of 0's

    uint64_t index_mask = (1ULL << index_bits) - 1;
    uint64_t index = PC & index_mask;

    return index;
}

uint64_t SVP_VPQ::get_tag(uint64_t PC) {
    PC = PC >> 2;           // get rid of 0's
    PC = PC >> index_bits;  // shift out the index

    uint64_t tag_mask = (1ULL << tag_bits) - 1;
    uint64_t tag = PC & tag_mask;

    return tag;
}

uint64_t SVP_VPQ::walk_VPQ(uint64_t index, uint64_t tag) {
    uint64_t count = 0;
    uint64_t i = vpq_head;

    for (uint64_t j = 0; j < vpq_count; j++) {
        if (VPQ[i].valid && VPQ[i].PC_index == index && VPQ[i].PC_tag == tag) {
            count++;
        }
        i = (i + 1) % VPQ.size();
    }

    return count;
}

uint64_t SVP_VPQ::vpq_num_entries() {
    return vpq_count;
}

uint64_t SVP_VPQ::vpq_free_entries() {
    return VPQ.size() - vpq_count;
}

vpq_entry SVP_VPQ::vpq_index_head() {
    return VPQ[vpq_head];
}

// Search SVP function, if tag match
bool SVP_VPQ::search_svp(uint64_t PC_index, uint64_t tag) {
    if (!SVP[PC_index].valid) return false;
    return (tag_bits == 0) || (SVP[PC_index].tag == tag);
}

// Generate value prediction if hit in SVP
void SVP_VPQ::svp_hit(payload_t* instr, uint64_t index, bool oracle_mode, int64_t oracle_val) {

    auto &entry = SVP[index];

    // Increment instance count
    entry.inst++;
    
    // Prediction generation
    int64_t pred = entry.ret_val + entry.inst * entry.stride;

    // Update dynamic instr 
    instr->vp_value = pred;
    instr->vp_predicted = true;
    
    // Oracle mode vs Normal Mode (+ recovery)
    if (oracle_mode) {
        instr->vp_confident = (pred == oracle_val);
    }
    else {
        instr->vp_confident = (entry.confidence == conf_max);
    }
}

// Allocate entry in VPQ, returns entry number
uint64_t SVP_VPQ::vpq_allocate(uint64_t index, uint64_t tag) {
    assert(vpq_count < VPQ.size());     // assert check for safety
    
    uint64_t idx = vpq_tail;
    assert(!VPQ[idx].valid);            // assert check for safety

    VPQ[idx].PC_index = index;
    VPQ[idx].PC_tag = tag;
    VPQ[idx].valid = true;

    vpq_tail = (vpq_tail + 1) % VPQ.size();
    vpq_count++;    // one more VPQ entry;

    return idx;
}

// Deposit value in VPQ in Writeback
void SVP_VPQ::vpq_deposit(uint64_t entry, uint64_t val) {
    VPQ[entry].value = val;
}

// Pop VPQ head, return entry PC
vpq_entry SVP_VPQ::vpq_pop_head() {
    vpq_entry entry = VPQ[vpq_head];
    VPQ[vpq_head].valid = false;        // invalidate VPQ entry
    vpq_head = (vpq_head + 1) % VPQ.size();
    vpq_count--;                        // one less VPQ entry
    return entry;
}


// If SVP tag hit, train SVP entry, use value, decrement instance counter
void SVP_VPQ::train_svp(uint64_t value, uint64_t index){

    assert(vpq_count > 0);
    assert(index < SVP.size());

    // Entry in SVP indexed by PC_index
    auto &entry = SVP[index];

    // Calculate new delta (stride)
    int64_t new_stride = (int64_t)value - (int64_t)entry.ret_val;

    // Update confidence based on if new delta is the same as the last
    if (new_stride == entry.stride) {
        // If delta matches, increment confidence (saturate at max)
        if (entry.confidence < conf_max) {
            entry.confidence++;
        }
    } else {
        // Stride mismatch, reset confidence and update stride
        entry.stride = new_stride;
        entry.confidence = 0;
        entry.valid = true;
    }

    // Update retired value and decrement the instance counter
    entry.ret_val = value;
    
    if (entry.inst > 0) entry.inst--;
}

// If SVP tag miss, replace entry
void SVP_VPQ::install_svp(uint64_t tag, uint64_t value, uint64_t index){
    // Entry in SVP indexed by PC_index
    auto &entry = SVP[index];

    // Safety assert to ensure actually misses
    assert(!entry.valid || tag != entry.tag);

    // Overwrite entry with new tag and value
    entry.valid = true;
    entry.tag = tag;
    entry.ret_val = value;
    entry.stride = value;                  // from spec
    entry.confidence = 0;
    entry.inst = walk_VPQ(index, tag);     // walk VPQ head to tail
}
