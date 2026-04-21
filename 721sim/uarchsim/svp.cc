#include "svp.h"
#include <assert.h>
#include <parameters.h>
#include <payload.h>

// Constructor definition
SVP_VPQ::SVP_VPQ(uint64_t vpq_size, uint64_t index_bits, uint64_t tag_bits, uint64_t conf) 
    : SVP(1 << index_bits), VPQ(vpq_size), vpq_head{0}, vpq_tail{0}, tag_bits{tag_bits}, conf_max{conf}, index_bits{index_bits} {
        // Initialize all SVP entries
        for (auto &entry: SVP) {
            entry.confidence = 0;
            entry.inst = 0;
            entry.stride = 0;
            entry.ret_val = 0;
            entry.tag = 0;
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
    uint64_t count = 0;         // initialize match count to 0
    uint64_t i = vpq_head;      // walk from H to T

    while (i != vpq_tail) {
        if (VPQ[i].PC_index == index && VPQ[i].PC_tag == tag) {
            count++;
        }
        i = (i + 1) % VPQ.size();
    }
    return count;
}

// Search SVP function, if tag match
bool SVP_VPQ::search_svp(uint64_t PC_index, uint64_t tag) {
    bool hit = (tag_bits == 0) || (SVP[PC_index].tag == tag);
    return hit;
}

// Generate value prediction if hit in SVP
void SVP_VPQ::svp_hit(payload_t* instr, uint64_t index, bool oracle_mode, int64_t oracle_val) {
    auto &entry = SVP[index];
    
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

    // Increment instance count
    entry.inst++;
}

// Allocate entry in VPQ, returns entry number
uint64_t SVP_VPQ::vpq_allocate(uint64_t index, uint64_t tag) {
    uint64_t idx = vpq_tail;

    VPQ[idx].PC_index = index;
    VPQ[idx].PC_tag = tag;

    vpq_tail = (vpq_tail + 1) % VPQ.size();

    return idx;
}

// Deposit value in VPQ in Writeback
void SVP_VPQ::vpq_deposit(uint64_t entry, uint64_t val){
    VPQ[entry].value = val;
}

// Pop VPQ head, return entry PC
 vpq_entry SVP_VPQ::vpq_pop_head(){
    vpq_entry entry = VPQ[vpq_head];
    vpq_head = (vpq_head + 1) % VPQ.size();
    return entry;
 }

    // If SVP tag hit, train SVP entry, use value, decrement instance counter
void SVP_VPQ::train_svp(uint64_t value, uint64_t index){
    // Tag hit so entry is straight from SVP
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
        entry.confidence = 0;
        entry.stride = new_stride;
    }

    // Update retired value and decrement the instance counter
    entry.ret_val = value;
    entry.inst--;
}

// If SVP tag miss, replace entry
void SVP_VPQ::install_svp(uint64_t tag, uint64_t value, uint64_t index){

    auto &entry = SVP[index];

    // Safety assert to ensure actually misses
    assert(tag != entry.tag);

    // Overwrite entry with new tag and value
    entry.tag = tag;
    entry.ret_val = value;
    entry.stride = value;                  // from spec
    entry.confidence = 0;
    entry.inst = walk_VPQ(index, tag);     // walk VPQ head to tail
}
