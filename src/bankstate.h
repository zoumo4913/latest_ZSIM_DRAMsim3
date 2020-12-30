#ifndef __BANKSTATE_H
#define __BANKSTATE_H

#include <vector>
#include "common.h"

namespace dramsim3 {

class BankState {
   public:
    BankState();

    enum class State { OPEN, CLOSED, SREF, PD, SIZE };
    Command GetReadyCommand(const Command& cmd, uint64_t clk);

    // Update the state of the bank resulting after the execution of the command
    void UpdateState(const Command& cmd);

    // Update the existing timing constraints for the command
    void UpdateTiming(const CommandType cmd_type, uint64_t time);

    bool IsRowOpen() const { return state_ == State::OPEN; }
    int OpenRow() const { return open_row_; }
    int RowHitCount() const { return row_hit_count_; }

    //MZOU
    // 返回bank的状态是否closed
    bool IsRowClosed() const {return state_ == State::CLOSED; }
    void SetInServe(bool in_) { in_serve = in_; }
    void SetServeEndCycle(uint64_t end_cycle) { serve_end_cycle = end_cycle; }
    bool ReturnInServe() const { return in_serve; }
    uint64_t ReturnServeEndCycle() const { return serve_end_cycle; }
    //void SetPrechargeByRefresh(bool in_) { precharge_by_refresh = in_; }
    bool ReturnPrechargeByRefresh() const { return precharge_by_refresh; }
    bool ReturnActivateByWho() const { return activate_by_who; }
    //MZOU

   private:
    // Current state of the Bank
    // Apriori or instantaneously transitions on a command.
    State state_;

    // Earliest time when the particular Command can be executed in this bank
    std::vector<uint64_t> cmd_timing_;

    // Currently open row
    int open_row_;

    // consecutive accesses to one row
    int row_hit_count_;

    //MZOU
    bool in_serve;
    uint64_t serve_end_cycle;
    // 0: by refresh
    // 1: by activate
    bool precharge_by_refresh;
    // 0: by read
    // 1: by write
    bool activate_by_who;
    //MZOU
};

}  // namespace dramsim3
#endif
