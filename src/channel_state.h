#ifndef __CHANNEL_STATE_H
#define __CHANNEL_STATE_H

#include <vector>
#include "bankstate.h"
#include "common.h"
#include "configuration.h"
#include "timing.h"

namespace dramsim3 {

class ChannelState {
   public:
    ChannelState(const Config& config, const Timing& timing);
    Command GetReadyCommand(const Command& cmd, uint64_t clk);
    void UpdateState(const Command& cmd, uint64_t clk);
    void UpdateTiming(const Command& cmd, uint64_t clk);
    void UpdateTimingAndStates(const Command& cmd, uint64_t clk);
    bool ActivationWindowOk(int rank, uint64_t curr_time) const;
    void UpdateActivationTimes(int rank, uint64_t curr_time);
    bool IsRowOpen(int rank, int bankgroup, int bank) const {
        return bank_states_[rank][bankgroup][bank].IsRowOpen();
    }
    bool IsAllBankIdleInRank(int rank) const;
    //MZOU
    // 返回指定rank里有多少bank处于in_serve状态
    int InServeBankNum(int rank) const;
    // 设置指定bank的in_serve状态
    void SetInServeBank(int rank, int bankgroup, int bank, bool in_serve) { bank_states_[rank][bankgroup][bank].SetInServe(in_serve); }
    // 设置指定bank的serve_end_cycle
    void SetServeEndCycleBank(int rank, int bankgroup, int bank, uint64_t end_cycle) { bank_states_[rank][bankgroup][bank].SetServeEndCycle(end_cycle); }
    // 返回指定bank的in_serve状态
    bool GetInServeBank(int rank, int bankgroup, int bank) const { return bank_states_[rank][bankgroup][bank].ReturnInServe(); }
    // 返回指定bank的serve_end_cycle
    uint64_t GetServeEndCycleBank(int rank, int bankgroup, int bank) const { return bank_states_[rank][bankgroup][bank].ReturnServeEndCycle(); }
    // 返回指定bank的closed状态
    bool IsRowClosed(int rank, int bankgroup, int bank) const { return bank_states_[rank][bankgroup][bank].IsRowClosed(); }
    // 设置指定bank的precharge_by_refresh
    //void SetPrechargeByRefreshBank(int rank, int bankgroup, int bank, bool in_) { bank_states_[rank][bankgroup][bank].SetPrechargeByRefresh(in_); }
    // 返回指定bank的precharge_by_refresh
    bool GetPrechargeByRefreshBank(int rank, int bankgroup, int bank) const { return bank_states_[rank][bankgroup][bank].ReturnPrechargeByRefresh(); }
    // 返回指定bank的activate_by_read
    bool GetActivateByWhoBank(int rank, int bankgroup, int bank) const { return bank_states_[rank][bankgroup][bank].ReturnActivateByWho(); }
    //MZOU
    bool IsRankSelfRefreshing(int rank) const { return rank_is_sref_[rank]; }
    bool IsRefreshWaiting() const { return !refresh_q_.empty(); }
    bool IsRWPendingOnRef(const Command& cmd) const;
    const Command& PendingRefCommand() const {return refresh_q_.front(); }
    void BankNeedRefresh(int rank, int bankgroup, int bank, bool need);
    void RankNeedRefresh(int rank, bool need);
    int OpenRow(int rank, int bankgroup, int bank) const {
        return bank_states_[rank][bankgroup][bank].OpenRow();
    }
    int RowHitCount(int rank, int bankgroup, int bank) const {
        return bank_states_[rank][bankgroup][bank].RowHitCount();
    };

    std::vector<int> rank_idle_cycles;

   private:
    const Config& config_;
    const Timing& timing_;

    std::vector<bool> rank_is_sref_;
    std::vector<std::vector<std::vector<BankState> > > bank_states_;
    std::vector<Command> refresh_q_;

    std::vector<std::vector<uint64_t> > four_aw_;
    std::vector<std::vector<uint64_t> > thirty_two_aw_;
    bool IsFAWReady(int rank, uint64_t curr_time) const;
    bool Is32AWReady(int rank, uint64_t curr_time) const;
    // Update timing of the bank the command corresponds to
    void UpdateSameBankTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);

    // Update timing of the other banks in the same bankgroup as the command
    void UpdateOtherBanksSameBankgroupTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);

    // Update timing of banks in the same rank but different bankgroup as the
    // command
    void UpdateOtherBankgroupsSameRankTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);

    // Update timing of banks in a different rank as the command
    void UpdateOtherRanksTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);

    // Update timing of the entire rank (for rank level commands)
    void UpdateSameRankTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);
};

}  // namespace dramsim3
#endif
