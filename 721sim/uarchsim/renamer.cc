#include "renamer.h"
#include <cassert>
#include <stdio.h>

/* ====================  Free List  ==================== */

/* Constructor */
free_list::free_list(uint64_t size) : head{0}, tail{0}, head_phase{0}, tail_phase{1} {
    mapping.resize(size);   // Dynamically size the Free List
    free_list_size = size;
}

/* Function Definitions */
void free_list::write_entry(uint64_t index, uint64_t value) {
    mapping[index] = value;
}

uint64_t free_list::read_entry(uint64_t index) {
    return mapping[index];
}

bool free_list::is_empty() {
    return (head == tail && head_phase == tail_phase);
}

bool free_list::is_full() {
    return (head == tail && head_phase != tail_phase);
}

bool free_list::has_space(uint64_t n_registers) {
    uint64_t available;

    if (head == tail) {
        if (head_phase == tail_phase)
            available = 0;
        else
            available = free_list_size;
    }
    else if (tail > head) {
        available = tail - head;
    }
    else {
        available = free_list_size - (head - tail);
    }

    return available >= n_registers;
}

uint64_t free_list::get_head() {
    return head;
}

bool free_list::get_head_phase() {
    return head_phase;
}

uint64_t free_list::get_tail() {
    return tail;
}

bool free_list::get_tail_phase() {
    return tail_phase;
}

uint64_t free_list::get_free(void) {
    /* Asssert we have enough Free Registers */
    assert(!(head == tail && head_phase == tail_phase));

    uint64_t free_reg = mapping[head];

    /* Head pointer: Wrap around logic */
    if (head == free_list_size - 1) {
        head = 0;
        head_phase = !head_phase;
    }
    else head++;
    return free_reg;
}

void free_list::push_free(uint64_t phys_reg) {
    /* Assert mapping is not full */
    assert(!(head == tail && head_phase != tail_phase));

    mapping[tail] = phys_reg;
    
    /* Tail pointer: Wrap around logic */
    if (tail == free_list_size - 1) {
        tail = 0;
        tail_phase = !tail_phase;
    }
    else tail++;
}

void free_list::reset() {
    /* Recovery reset for Free List (roll back head to tail) */
    head = tail;
    head_phase = !tail_phase;
}

void free_list::set_head(uint64_t new_head) {
    head = new_head;
}

void free_list::set_head_phase(bool new_phase) {
    head_phase = new_phase;
}

/* ====================  Active List  ==================== */

/* Constructor*/
active_list::active_list(uint64_t size) : head{0}, tail{0}, head_phase{0}, tail_phase{0} {
    entries.resize(size);       // Dynamically size the Active List
    active_list_size = size;
}

bool active_list::has_space(uint64_t n_registers) {
    uint64_t occupancy;

    if (head == tail) {
        if (head_phase == tail_phase) occupancy = 0;
        else occupancy = active_list_size;
    }
    else if (tail > head) {
        occupancy = tail - head;
    }
    else {
        occupancy = active_list_size - (head - tail);
    }

    return (active_list_size - occupancy) >= n_registers;
}

uint64_t active_list::push_instruction(active_list_entry entry) {
    /* Assert Active List not full */
    assert(!(head == tail && head_phase != tail_phase));

    uint64_t index = tail;
    entries[tail] = entry;
    if (tail == active_list_size - 1) {
        tail = 0;
        tail_phase = !tail_phase;
    }
    else tail++;

    return index;
}

void active_list::set_complete(uint64_t AL_index) {
    entries[AL_index].completed_bit = true;
}

void active_list::set_exception(uint64_t AL_index) {
    entries[AL_index].exception_bit = true;
}

void active_list::set_load_violation(uint64_t AL_index) {
    entries[AL_index].load_violation_bit = true;
}

void active_list::set_value_misprediction(uint64_t AL_index) {
    entries[AL_index].val_mispred_bit = true;
}

void active_list::set_branch_misprediction(uint64_t AL_index) {
    entries[AL_index].branch_mispred_bit = true;
}

bool active_list::get_exception(uint64_t AL_index) {
    return entries[AL_index].exception_bit;
}

bool active_list::is_empty() {
    return (head == tail && head_phase == tail_phase);
}

active_list_entry active_list::get_head_instr() {
    return entries[head];
}

uint64_t active_list::get_head() {
    return head;
}

bool active_list::get_head_phase() {
    return head_phase;
}

uint64_t active_list::get_tail() {
    return tail;
}

bool active_list::get_tail_phase() {
    return tail_phase;
}

void active_list::advance_head() {
    /* Assert Active List is not empty*/
    assert(!(head == tail && head_phase == tail_phase));

    if (head == active_list_size - 1) {
        head = 0;
        head_phase = !head_phase;
    }
    else head++;
}

void active_list::restore_tail(uint64_t index) {
    assert(index < active_list_size);
    tail = index;
}

void active_list::restore_tail_phase(bool phase) {
    tail_phase = phase;
}

void active_list::reset() {
    /* Recovery reset for Active List (head and tail must be equal)*/
    tail = head;
    tail_phase = head_phase;
}


/* ====================  Renamer  ==================== */

/* Constructor*/
renamer::renamer(uint64_t n_log_regs, 
                 uint64_t n_phys_regs, 
                 uint64_t n_branches, 
                 uint64_t n_active) 
        : FreeList(n_phys_regs - n_log_regs),   // allocate space for the primary data structures.
          ActiveList(n_active), 
          RMT(n_log_regs), 
          AMT(n_log_regs), 
          PRF(n_phys_regs),
          checkpoints(n_branches) { 

    /* Assertions*/ 
    assert(n_phys_regs > n_log_regs);               // number of physical registers > number logical registers.
    assert(n_branches >= 1 && n_branches <= 64);    // 1 <= n_branches <= 64.
    assert(n_active > 0);                           // n_active > 0.

    /* Initialize private variables */
    num_branches = n_branches;

	// Initialize the data structures based on the knowledge
	// that the pipeline is intially empty (no in-flight instructions yet).

    /* Initialize RMT and AMT entries (identical) */
    for (uint64_t i = 0; i < n_log_regs; i++) {
        RMT[i] = i;
        AMT[i] = i;
    }

    /* Initialize the Free List */
    uint64_t size = n_phys_regs - n_log_regs;
    uint64_t start_reg = n_log_regs;
    for (uint64_t i = 0; i < size; i++) {       // entries start from index = n_log_regs to 
        FreeList.write_entry(i, start_reg);
        start_reg++;
    }

    /* Active List begins empty, no initialization necessary */

    /* Initialize GBM */
    GBM = 0;

    /* Initialize PRF ready bit array */
    PRF_ready_array.resize(n_phys_regs);

    for (uint64_t i = 0; i < n_phys_regs; i++) {
        PRF_ready_array[i] = true;
    }

    /* Initialize the Checkpoints */
    checkpoints.resize(n_branches);

    for (uint64_t i = 0; i < n_branches; i++) {
        checkpoints[i].GBM = 0;
        checkpoints[i].shadow_RMT.resize(n_log_regs);
    }
}

/////////////////////////////////////////////////////////////////////
// The Rename Stage must stall if there aren't enough free physical
// registers available for renaming all logical destination registers
// in the current rename bundle.
//
// Inputs:
// 1. bundle_dst: number of logical destination registers in
//    current rename bundle
//
// Return value:
// Return "true" (stall) if there aren't enough free physical
// registers to allocate to all of the logical destination registers
// in the current rename bundle.
/////////////////////////////////////////////////////////////////////

bool renamer::stall_reg(uint64_t bundle_dst) {
    return !FreeList.has_space(bundle_dst);     // Free list doesn't have space => stall 
}

/////////////////////////////////////////////////////////////////////
// The Rename Stage must stall if there aren't enough free
// checkpoints for all branches in the current rename bundle.
//
// Inputs:
// 1. bundle_branch: number of branches in current rename bundle
//
// Return value:
// Return "true" (stall) if there aren't enough free checkpoints
// for all branches in the current rename bundle.
/////////////////////////////////////////////////////////////////////
bool renamer::stall_branch(uint64_t bundle_branch) {
    uint64_t used = 0;
    for (uint64_t i = 0; i < num_branches; i++) {
        if (GBM & (1ULL << i)) {
            used++;     // every 1 bit in the GBM signals a used checkpoint
        }
    }
    uint64_t free = num_branches - used;    // free checkpoints = num_branches - used checkpoints
    return free < bundle_branch;
}

/////////////////////////////////////////////////////////////////////
// This function is used to get the branch mask for an instruction.
/////////////////////////////////////////////////////////////////////
uint64_t renamer::get_branch_mask() {
    return GBM;
}

/////////////////////////////////////////////////////////////////////
// This function is used to rename a single source register.
//
// Inputs:
// 1. log_reg: the logical register to rename
//
// Return value: physical register name
/////////////////////////////////////////////////////////////////////
uint64_t renamer::rename_rsrc(uint64_t log_reg) {
    return RMT[log_reg];
}

/////////////////////////////////////////////////////////////////////
// This function is used to rename a single destination register.
//
// Inputs:
// 1. log_reg: the logical register to rename
//
// Return value: physical register name
/////////////////////////////////////////////////////////////////////
uint64_t renamer::rename_rdst(uint64_t log_reg) {
    /* 1. Allocate a new physical register */
    uint64_t new_phys = FreeList.get_free();

    /* 2. Update the RMT */
    RMT[log_reg] = new_phys;

    /* 3. Mark new physical register as not ready */
    PRF_ready_array[new_phys] = false;

    return new_phys;
}

/////////////////////////////////////////////////////////////////////
// This function creates a new branch checkpoint.
//
// Inputs: none.
//
// Output:
// 1. The function returns the branch's ID. When the branch resolves,
//    its ID is passed back to the renamer via "resolve()" below.
//
// Tips:
//
// Allocating resources for the branch (a GBM bit and a checkpoint):
// * Find a free bit -- i.e., a '0' bit -- in the GBM. Assert that
//   a free bit exists: it is the user's responsibility to avoid
//   a structural hazard by calling stall_branch() in advance.
// * Set the bit to '1' since it is now in use by the new branch.
// * The position of this bit in the GBM is the branch's ID.
// * Use the branch checkpoint that corresponds to this bit.
// 
// The branch checkpoint should contain the following:
// 1. Shadow Map Table (checkpointed Rename Map Table)
// 2. checkpointed Free List head pointer and its phase bit
// 3. checkpointed GBM
/////////////////////////////////////////////////////////////////////
uint64_t renamer::checkpoint() {
    /* 1. Find a free bit */
    uint64_t index = 0;

    while (index < num_branches) {
        if ((GBM & (1ULL << index)) == 0) break;
        index++;
    }

    assert(index != num_branches);              // Assert free bit exists

    uint64_t branch_id = index;                 // Branch ID
    uint64_t branch_mask = 1ULL << branch_id;   // Branch Mask

    /* 2. Set bit in GBM */
    GBM |= branch_mask;                         // Set the bit in GBM to '1'

    /* 3. Create checkpoint */
    checkpoints[branch_id].GBM = GBM;
    checkpoints[branch_id].head = FreeList.get_head();
    checkpoints[branch_id].head_phase = FreeList.get_head_phase();
    checkpoints[branch_id].shadow_RMT = RMT;

    return branch_id;
}

//////////////////////////////////////////
// Functions related to Dispatch Stage. //
////////////////////////////////////////// 

/////////////////////////////////////////////////////////////////////
// The Dispatch Stage must stall if there are not enough free
// entries in the Active List for all instructions in the current
// dispatch bundle.
//
// Inputs:
// 1. bundle_inst: number of instructions in current dispatch bundle
//
// Return value:
// Return "true" (stall) if the Active List does not have enough
// space for all instructions in the dispatch bundle.
/////////////////////////////////////////////////////////////////////
bool renamer::stall_dispatch(uint64_t bundle_inst) {
    return !ActiveList.has_space(bundle_inst);
}

/////////////////////////////////////////////////////////////////////
// This function dispatches a single instruction into the Active
// List.
//
// Inputs:
// 1. dest_valid: If 'true', the instr. has a destination register,
//    otherwise it does not. If it does not, then the log_reg and
//    phys_reg inputs should be ignored.
// 2. log_reg: Logical register number of the instruction's
//    destination.
// 3. phys_reg: Physical register number of the instruction's
//    destination.
// 4. load: If 'true', the instr. is a load, otherwise it isn't.
// 5. store: If 'true', the instr. is a store, otherwise it isn't.
// 6. branch: If 'true', the instr. is a branch, otherwise it isn't.
// 7. amo: If 'true', this is an atomic memory operation.
// 8. csr: If 'true', this is a system instruction.
// 9. PC: Program counter of the instruction.
//
// Return value:
// Return the instruction's index in the Active List.
//
// Tips:
//
// Before dispatching the instruction into the Active List, assert
// that the Active List isn't full: it is the user's responsibility
// to avoid a structural hazard by calling stall_dispatch()
// in advance.
/////////////////////////////////////////////////////////////////////
uint64_t renamer::dispatch_inst(bool dest_valid,
                        uint64_t log_reg,
                        uint64_t phys_reg,
                        bool load,
                        bool store,
                        bool branch,
                        bool amo,
                        bool csr,
                        uint64_t PC) {
    /* 1. Checking if dst valid */
    active_list_entry entry;
    if (dest_valid) {
        entry.dst_flag = true;
        entry.logical_reg_num = log_reg;
        entry.physical_reg_num = phys_reg;
    }
    else entry.dst_flag = false;

    /* 2. Initialize all flags to false */
    entry.completed_bit = false;
    entry.exception_bit = false;
    entry.load_violation_bit = false;
    entry.branch_mispred_bit = false;
    entry.val_mispred_bit = false;

    /* 3. Set instruction flags */
    entry.load_flag = load;
    entry.store_flag = store;
    entry.branch_flag = branch;

    entry.amo_flag = amo;
    entry.csr_flag = csr;
    
    /* 4. Set instruction PC */
    entry.PC  = PC;

    /* 5. push entry into the active list */
    assert(ActiveList.has_space(1));
    return ActiveList.push_instruction(entry);
}

/////////////////////////////////////////////////////////////////////
// Test the ready bit of the indicated physical register.
// Returns 'true' if ready.
/////////////////////////////////////////////////////////////////////
bool renamer::is_ready(uint64_t phys_reg) {
    return PRF_ready_array[phys_reg];
}

/////////////////////////////////////////////////////////////////////
// Clear the ready bit of the indicated physical register.
/////////////////////////////////////////////////////////////////////
void renamer::clear_ready(uint64_t phys_reg) {
    PRF_ready_array[phys_reg] = false;
}

/////////////////////////////////////////////////////////////////////
// Return the contents (value) of the indicated physical register.
/////////////////////////////////////////////////////////////////////
uint64_t renamer::read(uint64_t phys_reg) {
    return PRF[phys_reg];
}

/////////////////////////////////////////////////////////////////////
// Set the ready bit of the indicated physical register.
/////////////////////////////////////////////////////////////////////
void renamer::set_ready(uint64_t phys_reg) {
    PRF_ready_array[phys_reg] = true;
}

//////////////////////////////////////////
// Functions related to Writeback Stage.//
//////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
// Write a value into the indicated physical register.
/////////////////////////////////////////////////////////////////////
void renamer::write(uint64_t phys_reg, uint64_t value){
    PRF[phys_reg] = value;
}

/////////////////////////////////////////////////////////////////////
// Set the completed bit of the indicated entry in the Active List.
/////////////////////////////////////////////////////////////////////
void renamer::set_complete(uint64_t AL_index) {
    ActiveList.set_complete(AL_index);
}

/////////////////////////////////////////////////////////////////////
// This function is for handling branch resolution.
//
// Inputs:
// 1. AL_index: Index of the branch in the Active List.
// 2. branch_ID: This uniquely identifies the branch and the
//    checkpoint in question.  It was originally provided
//    by the checkpoint function.
// 3. correct: 'true' indicates the branch was correctly
//    predicted, 'false' indicates it was mispredicted
//    and recovery is required.
//
// Outputs: none.
//
// Tips:
//
// While recovery is not needed in the case of a correct branch,
// some actions are still required with respect to the GBM and
// all checkpointed GBMs:
// * Remember to clear the branch's bit in the GBM.
// * Remember to clear the branch's bit in all checkpointed GBMs.
//
// In the case of a misprediction:
// * Restore the GBM from the branch's checkpoint. Also make sure the
//   mispredicted branch's bit is cleared in the restored GBM,
//   since it is now resolved and its bit and checkpoint are freed.
// * You don't have to worry about explicitly freeing the GBM bits
//   and checkpoints of branches that are after the mispredicted
//   branch in program order. The mere act of restoring the GBM
//   from the checkpoint achieves this feat.
// * Restore the RMT using the branch's checkpoint.
// * Restore the Free List head pointer and its phase bit,
//   using the branch's checkpoint.
// * Restore the Active List tail pointer and its phase bit
//   corresponding to the entry after the branch's entry.
//   Hints:
//   You can infer the restored tail pointer from the branch's
//   AL_index. You can infer the restored phase bit, using
//   the phase bit of the Active List head pointer, where
//   the restored Active List tail pointer is with respect to
//   the Active List head pointer, and the knowledge that the
//   Active List can't be empty at this moment (because the
//   mispredicted branch is still in the Active List).
// * Do NOT set the branch misprediction bit in the Active List.
//   (Doing so would cause a second, full squash when the branch
//   reaches the head of the Active List. We don’t want or need
//   that because we immediately recover within this function.)
/////////////////////////////////////////////////////////////////////
void renamer::resolve(uint64_t AL_index,
            uint64_t branch_ID,
            bool correct) {
    
    uint64_t branch_mask = (1ULL << branch_ID);

    /* Correct Branch Prediction */
    if (correct) {
        /* 1. Clear branch bit in GBM */
        GBM  &= ~branch_mask;

        /* 2. Clear branch bit in all checkpoints */
        for (auto &checkpoint: checkpoints) {
            checkpoint.GBM &= ~branch_mask;
        }
        return;
    }
    /* Incorrect Branch Prediction */
    else {
        /* 1. Load checkpointed GBM into GBM */
        GBM = checkpoints[branch_ID].GBM;

        /*  2. Clear branch bit in checkpointed GBM */
        GBM &= ~branch_mask;

        /* 3. Restore the RMT */
        RMT = checkpoints[branch_ID].shadow_RMT;

        /* 4. Restore Free List head and head_phase */
        FreeList.set_head(checkpoints[branch_ID].head);
        FreeList.set_head_phase(checkpoints[branch_ID].head_phase);

        /* 5. Restore Active List tail and tail phase */
        uint64_t restored_tail = (AL_index + 1) % ActiveList.active_list_size;
        ActiveList.restore_tail(restored_tail);      // Restore tail to branch instr index + 1

        uint64_t head = ActiveList.get_head();
        bool head_phase = ActiveList.get_head_phase();
        bool tail_phase;

        if (restored_tail <= head) tail_phase = !head_phase;
        else tail_phase = head_phase;

        ActiveList.restore_tail_phase(tail_phase);
        return;
    }
}

//////////////////////////////////////////
// Functions related to Retire Stage.   //
//////////////////////////////////////////

///////////////////////////////////////////////////////////////////
// This function allows the caller to examine the instruction at the head
// of the Active List.
//
// Input arguments: none.
//
// Return value:
// * Return "true" if the Active List is NOT empty, i.e., there
//   is an instruction at the head of the Active List.
// * Return "false" if the Active List is empty, i.e., there is
//   no instruction at the head of the Active List.
//
// Output arguments:
// Simply return the following contents of the head entry of
// the Active List.  These are don't-cares if the Active List
// is empty (you may either return the contents of the head
// entry anyway, or not set these at all).
// * completed bit
// * exception bit
// * load violation bit
// * branch misprediction bit
// * value misprediction bit
// * load flag (indicates whether or not the instr. is a load)
// * store flag (indicates whether or not the instr. is a store)
// * branch flag (indicates whether or not the instr. is a branch)
// * amo flag (whether or not instr. is an atomic memory operation)
// * csr flag (whether or not instr. is a system instruction)
// * program counter of the instruction
/////////////////////////////////////////////////////////////////////
bool renamer::precommit(bool &completed,
                    bool &exception, bool &load_viol, bool &br_misp, bool &val_misp,
                bool &load, bool &store, bool &branch, bool &amo, bool &csr,
            uint64_t &PC) {

    if (!ActiveList.is_empty()) {
        /* If not empty, get head instruction from Active List */
        auto head_instr = ActiveList.get_head_instr();
        /* Populate head instruction */
        completed = head_instr.completed_bit;
        exception = head_instr.exception_bit;
        load_viol = head_instr.load_violation_bit;
        br_misp= head_instr.branch_mispred_bit;
        val_misp = head_instr.val_mispred_bit;
        load = head_instr.load_flag;
        store = head_instr.store_flag;
        branch = head_instr.branch_flag;
        amo = head_instr.amo_flag;
        csr = head_instr.csr_flag;
        PC = head_instr.PC;
        return true;
    }
    else {
        /* If empty, return false*/
        return false;
    }
}

/////////////////////////////////////////////////////////////////////
// This function commits the instruction at the head of the Active List.
//
// Tip (optional but helps catch bugs):
// Before committing the head instruction, assert that it is valid to
// do so (use assert() from standard library). Specifically, assert
// that all of the following are true:
// - there is a head instruction (the active list isn't empty)
// - the head instruction is completed
// - the head instruction is not marked as an exception
// - the head instruction is not marked as a load violation
// It is the caller's (pipeline's) duty to ensure that it is valid
// to commit the head instruction BEFORE calling this function
// (by examining the flags returned by "precommit()" above).
// This is why you should assert() that it is valid to commit the
// head instruction and otherwise cause the simulator to exit.
/////////////////////////////////////////////////////////////////////
void renamer::commit() {
    /* Assert that the head instruction is valid */
    assert(!ActiveList.is_empty());
    auto head_instr = ActiveList.get_head_instr();
    assert(head_instr.completed_bit);
    assert(!head_instr.exception_bit);
    assert(!head_instr.load_violation_bit);

    /* Commit the head instruction if instruction has dst register */
    if (head_instr.dst_flag) {
        // Get old physical register number from AMT
        uint64_t old_phy_reg_num = AMT[head_instr.logical_reg_num];

        // Update the Free List with old physical register number
        FreeList.push_free(old_phy_reg_num);

        // Update the AMT with new physical register number
        AMT[head_instr.logical_reg_num] = head_instr.physical_reg_num;
    }
    
    // Advance the Active List head pointer
    ActiveList.advance_head(); 
}

//////////////////////////////////////////////////////////////////////
// Squash the renamer class.
//
// Squash all instructions in the Active List and think about which
// structures in your renamer class need to be restored, and how.
//
// After this function is called, the renamer should be rolled-back
// to the committed state of the machine and all renamer state
// should be consistent with an empty pipeline.
/////////////////////////////////////////////////////////////////////
void renamer::squash() {
    /* 1. Roll back the RMT(speculative) to the AMT(commited)*/
    RMT = AMT;

    /* 2. Reset the Active List to empty */
    ActiveList.reset();

    /* 3. Restore the Free List */
    FreeList.reset();

    /* 4. Clear GBM */
    GBM = 0;

    /* 5. Reset PRF ready array */
    for (bool entry: PRF_ready_array) {
        entry = true;
    }
}

//////////////////////////////////////////
// Functions not tied to specific stage.//
//////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
// Functions for individually setting the exception bit,
// load violation bit, branch misprediction bit, and
// value misprediction bit, of the indicated entry in the Active List.
/////////////////////////////////////////////////////////////////////
void renamer::set_exception(uint64_t AL_index) {
    ActiveList.set_exception(AL_index);
}

void renamer::set_load_violation(uint64_t AL_index) {
    ActiveList.set_load_violation(AL_index);
}

void renamer::set_branch_misprediction(uint64_t AL_index) {
    ActiveList.set_branch_misprediction(AL_index);
}

void renamer::set_value_misprediction(uint64_t AL_index) {
    ActiveList.set_value_misprediction(AL_index);
}

/////////////////////////////////////////////////////////////////////
// Query the exception bit of the indicated entry in the Active List.
/////////////////////////////////////////////////////////////////////
bool renamer::get_exception(uint64_t AL_index) {
    return ActiveList.get_exception(AL_index);
}
