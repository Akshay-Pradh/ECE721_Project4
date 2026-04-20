#include "svp.h"
#include <assert.h>
#include <parameters.h>
#include <payload.h>

// Constructor definition
SVP_VPQ::SVP_VPQ(uint64_t vpq_size, uint64_t index_bits, uint64_t tag_bits, uint64_t conf) 
    : SVP(1 << index_bits), VPQ(vpq_size), vpq_head{0}, vpq_tail{0}, tag_bits{tag_bits}, conf_max{conf} {
        // Initialize all SVP entries
        for (auto &entry: SVP) {
            entry.confidence = 0;
            entry.inst = 0;
            entry.stride = 0;
            entry.ret_val = 0;
            entry.tag = 0;
        }
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
    instr->vp_confident = (entry.confidence == conf_max);

    // Increment instance count
    entry.inst++;
}

// Deposit value in VPQ in Writeback
uint64_t SVP_VPQ::vpq_allocate(uint64_t index, uint64_t tag) {
    uint64_t idx = vpq_tail;

    VPQ[idx].PC_index = index;
    VPQ[idx].PC_tag = tag;

    vpq_tail = (vpq_tail + 1) % VPQ.size();

    return idx;
}
