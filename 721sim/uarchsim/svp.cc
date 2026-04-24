#include "svp.h"

// Constructor definition
SVP_VPQ::SVP_VPQ(uint64_t vpq_size, uint64_t index_bits, uint64_t tag_bits, uint64_t conf) 
    : SVP(1 << index_bits), VPQ(vpq_size), vpq_head{0}, vpq_tail{0}, vpq_count{0}, tag_bits{tag_bits}, 
      conf_max{conf}, index_bits{index_bits} {
        // Initialize all SVP entries
        for (auto &entry: SVP) {
        entry.valid = false;
        entry.tag = 0;
        entry.stride_conf = 0;
        entry.ret_val = 0;
        entry.stride = 0;
        entry.lv_conf = 0;
        entry.inst = 0;
}

        // Initialize all VPQ entries
        for (auto &entry: VPQ) {
            entry.valid = false;
            entry.PC_index = 0;
            entry.PC_tag = 0;
            entry.value = 0;
        }

        // Initialize all statistic counts
        vpmeas_ineligible = 0;
        vpmeas_eligible = 0;
        vpmeas_miss = 0;
        vpmeas_conf_corr = 0;
        vpmeas_conf_incorr = 0;
        vpmeas_unconf_corr = 0;
        vpmeas_unconf_incorr = 0;
}

void SVP_VPQ::svp_vpq_config(FILE *fp) {
    fprintf(fp, "\n=== VALUE PREDICTOR ============================================================\n\n");

    fprintf(fp, "VP-eligible configuration:\n");
    fprintf(fp, "   predINTALU = %d\n", (predINTALU ? 1 : 0));
    fprintf(fp, "   predFPALU  = %d\n", (predFPALU ? 1 : 0));
    fprintf(fp, "   predLOAD   = %d\n", (predLOAD ? 1 : 0));

    if (VP_PERFECT) {
        fprintf(fp, "\nVALUE PREDICTOR = perfect\n");
    }
    else if (VP_SVP) {
        fprintf(fp, "\nVALUE PREDICTOR = stride (Project 4 spec. implementation)\n");
        fprintf(fp, "   VPQsize         = %u\n", VPQ.size());
        fprintf(fp, "   oracleconf      = %d (%s confidence)\n",
                (ORACLE_CONF ? 1 : 0),
                (ORACLE_CONF ? "oracle" : "real"));
        fprintf(fp, "   # index bits    = %u\n", index_bits);
        fprintf(fp, "   # tag bits      = %u\n", tag_bits);
        fprintf(fp, "   confmax         = %u\n", conf_max);
    }

    fprintf(fp, "\nCOST ACCOUNTING\n");

    if (VP_PERFECT) {
        fprintf(fp, "  Impossible.\n");
    }
    else if (VP_SVP) {
        uint64_t num_svp_entries = (1ULL << index_bits);
        uint64_t conf_bits = bits_to_encode(conf_max + 1);
        uint64_t instance_bits = bits_to_encode((uint64_t)VPQ.size());
        uint64_t bits_per_entry = tag_bits + conf_bits + 64 + 64 + instance_bits;
        uint64_t total_bits = num_svp_entries * bits_per_entry;
        double total_bytes = (double)total_bits / 8.0;

        fprintf(fp, "   One SVP entry:\n");
        fprintf(fp, "      tag           : %3u bits  // num_tag_bits\n", tag_bits);
        fprintf(fp, "      conf          : %3" PRIu64 " bits  // formula: (uint64_t)ceil(log2((double)(confmax+1)))\n", conf_bits);
        fprintf(fp, "      retired_value :  64 bits  // RISCV64 integer size.\n");
        fprintf(fp, "      stride        :  64 bits  // RISCV64 integer size. Competition opportunity: truncate stride to far fewer bits based on stride distribution of stride-predictable instructions.\n");
        fprintf(fp, "      instance ctr  : %3" PRIu64 " bits  // formula: (uint64_t)ceil(log2((double)VPQsize))\n", instance_bits);
        fprintf(fp, "      -------------------------\n");
        fprintf(fp, "      bits/SVP entry: %" PRIu64 " bits\n", bits_per_entry);
        fprintf(fp, "   Total storage cost (bits) = (%" PRIu64 " SVP entries x %" PRIu64 " bits/SVP entry) = %" PRIu64 " bits\n",
                num_svp_entries, bits_per_entry, total_bits);
        fprintf(fp, "   Total storage cost (bytes) = %.2f B (%.2f KB)\n",
                total_bytes, total_bytes / 1024.0);
    }
}

void SVP_VPQ::svp_vpq_stats(FILE *fp, uint64_t commit_count) {
   double total = (commit_count ? (double) commit_count : 1.0);

   fprintf(fp, "VPU MEASUREMENTS-----------------------------------\n");
   fprintf(fp, "vpmeas_ineligible         : %10" PRIu64 " (%6.2f%%) // Not eligible for value prediction.\n",
           vpmeas_ineligible, 100.0 * (double) vpmeas_ineligible / total);
   fprintf(fp, "vpmeas_eligible           : %10" PRIu64 " (%6.2f%%) // Eligible for value prediction.\n",
           vpmeas_eligible, 100.0 * (double) vpmeas_eligible / total);
   fprintf(fp, "   vpmeas_miss            : %10" PRIu64 " (%6.2f%%) // VPU was unable to generate a value prediction (e.g., SVP miss).\n",
           vpmeas_miss, 100.0 * (double) vpmeas_miss / total);
   fprintf(fp, "   vpmeas_conf_corr       : %10" PRIu64 " (%6.2f%%) // VPU generated a confident and correct value prediction.\n",
           vpmeas_conf_corr, 100.0 * (double) vpmeas_conf_corr / total);
   fprintf(fp, "   vpmeas_conf_incorr     : %10" PRIu64 " (%6.2f%%) // VPU generated a confident and incorrect value prediction. (MISPREDICTION)\n",
           vpmeas_conf_incorr, 100.0 * (double) vpmeas_conf_incorr / total);
   fprintf(fp, "   vpmeas_unconf_corr     : %10" PRIu64 " (%6.2f%%) // VPU generated an unconfident and correct value prediction. (LOST OPPORTUNITY)\n",
           vpmeas_unconf_corr, 100.0 * (double) vpmeas_unconf_corr / total);
   fprintf(fp, "   vpmeas_unconf_incorr   : %10" PRIu64 " (%6.2f%%) // VPU generated an unconfident and incorrect value prediction.\n",
           vpmeas_unconf_incorr, 100.0 * (double) vpmeas_unconf_incorr / total);
}

// Helper function: How many bits are needed to encode n possibilities
uint64_t SVP_VPQ::bits_to_encode(uint64_t n) {
    if (n <= 1) {
        return 0;
    }
    uint64_t bits = 0;
    uint64_t value = n - 1;

    while (value > 0) {
        bits++;
        value >>= 1;
    }
    return bits;
}

void SVP_VPQ::inc_counters(const payload_t *pay) {
    if (!pay->vp_eligible) {
        vpmeas_ineligible++;
        return;
    }

    vpmeas_eligible++;

    if (!pay->vp_predicted) {
        vpmeas_miss++;
        return;
    }

    bool correct = (pay->vp_value == pay->C_value.dw);

    if (pay->vp_confident) {
        if (correct) {
            vpmeas_conf_corr++;
        }
        else {
            vpmeas_conf_incorr++;
        }
    }
    else {
        if (correct) {
            vpmeas_unconf_corr++;
        }
        else {
            vpmeas_unconf_incorr++;
        }
    }
}

// Helper function to calculate PC_index from PC
uint64_t SVP_VPQ::get_index(uint64_t PC) {
    PC = PC >> 2;       // get rid of 0's

    uint64_t index_mask = (1ULL << index_bits) - 1;
    uint64_t index = PC & index_mask;

    return index;
}

// Helper function to calculate PC_tag from PC
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
    if (tag_bits == 0) return true;
    return ((SVP[PC_index].valid) && (SVP[PC_index].tag == tag));
}

// Generate value prediction if hit in SVP
void SVP_VPQ::svp_hit(payload_t* instr, uint64_t index, bool oracle_mode, int64_t oracle_val) {
    auto &entry = SVP[index];

    // Count this newly-renamed dynamic instance
    entry.inst++;

    int64_t stride_pred = (int64_t)entry.ret_val + entry.inst * entry.stride;
    int64_t lv_pred     = (int64_t)entry.ret_val;

    bool use_stride = (entry.stride_conf >= entry.lv_conf);
    int64_t pred    = use_stride ? stride_pred : lv_pred;

    instr->vp_value = pred;
    instr->vp_predicted = true;

    if (oracle_mode) {
        instr->vp_confident = (pred == oracle_val);
    } else {
        uint64_t chosen_conf = use_stride ? entry.stride_conf : entry.lv_conf;
        instr->vp_confident = (chosen_conf == conf_max);
    }
}

// Allocate entry in VPQ, returns entry number
uint64_t SVP_VPQ::vpq_allocate(uint64_t index, uint64_t tag, uint64_t branch_mask) {
    assert(vpq_count < VPQ.size());     // assert check for safety
    
    uint64_t idx = vpq_tail;
    assert(!VPQ[idx].valid);            // assert check for safety

    VPQ[idx].PC_index = index;
    VPQ[idx].PC_tag = tag;
    VPQ[idx].valid = true;
    VPQ[idx].branch_mask = branch_mask;

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
    assert(vpq_count > 0);
    assert(VPQ[vpq_head].valid);

    vpq_entry entry = VPQ[vpq_head];
    VPQ[vpq_head].valid = false;        // invalidate VPQ entry
    vpq_head = (vpq_head + 1) % VPQ.size();
    vpq_count--;                        // one less VPQ entry
    return entry;
}


// If SVP tag hit, train SVP entry, use value, decrement instance counter
void SVP_VPQ::train_svp(uint64_t value, uint64_t index) {
    assert(index < SVP.size());

    auto &entry = SVP[index];

    int64_t old_ret_val = (int64_t)entry.ret_val;
    int64_t new_stride  = (int64_t)value - old_ret_val;

    //
    // Train stride predictor
    if (new_stride == entry.stride) {
        if (entry.stride_conf < conf_max) {
            entry.stride_conf++;
        }
    } else {
        entry.stride = new_stride;
        entry.stride_conf = 0;
    }

 
    // Train last-value predictor
    if ((int64_t)value == old_ret_val) {
        if (entry.lv_conf < conf_max) {
            entry.lv_conf++;
        }
    } else {
        entry.lv_conf = 0;
    }

    entry.valid = true;
    entry.ret_val = value;

    if (entry.inst > 0) {
        entry.inst--;
    }
}
// If SVP tag miss, replace entry
void SVP_VPQ::install_svp(uint64_t tag, uint64_t value, uint64_t index) {
    auto &entry = SVP[index];

    assert(!entry.valid || tag != entry.tag);

    entry.valid = true;
    entry.tag = tag;

    entry.ret_val = value;

    //initialization
    entry.stride = 0;
    entry.stride_conf = 0;
    entry.lv_conf = 0;

    entry.inst = walk_VPQ(index, tag);
}

// Helper function for VPQ rollback
void SVP_VPQ::clear_mask_bits(uint64_t branch_ID) {
    // Branch mask clear bit = 1 << branch_ID
    uint64_t mask_bit = (1UL << branch_ID);

    // Clear branch mask bit for all valid VPQ entries
    for (auto &entry: VPQ) {
        if (entry.valid) {
            entry.branch_mask &= ~mask_bit;
        }
    }
}

// Simulating VPQ rollback and checkpoint integration via storing branch_mask in each VPQ entry
// Rollback via logical AND of branch ID and vpq entries branch mask (rollback until no dependence)
void SVP_VPQ::vpq_rollback(uint64_t branch_ID) {
    uint64_t mask_bit = (1ULL << branch_ID);

    while (vpq_count > 0) {
        // Set index of latest VPQ entry
        uint64_t latest_index = (vpq_tail == 0) ? (VPQ.size() - 1) : (vpq_tail - 1);
        vpq_entry &entry = VPQ[latest_index];

        if ((entry.branch_mask & mask_bit) == 0) {
            break;
        }
        
        // If we hit in the SVP, decrement instance counter
        if (search_svp(entry.PC_index, entry.PC_tag)) {
            if (SVP[entry.PC_index].inst > 0) {
                SVP[entry.PC_index].inst--;
            }
        }

        // Clears VPQ entry
        entry.valid = false;
        entry.PC_index = 0;
        entry.PC_tag = 0;
        entry.value = 0;

        vpq_tail = latest_index;
        vpq_count--;
    }
}

// Flash clear SVP VPQ under complete squash
void SVP_VPQ::flash_clear() {
    // Clear SVP instances
    for (auto &entry: SVP) {
        entry.inst = 0;
    }

    // Clear VPQ
    for (auto &entry: VPQ) {
        entry.valid = false;
        entry.PC_index = 0;
        entry.PC_tag = 0;
        entry.value = 0;
    }

    // Reset VPQ head, tail and valid entry count
    vpq_head = 0;
    vpq_tail = 0;
    vpq_count = 0;
}
