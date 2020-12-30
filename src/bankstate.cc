#include "bankstate.h"

namespace dramsim3 {

BankState::BankState()
    : state_(State::CLOSED),
      cmd_timing_(static_cast<int>(CommandType::SIZE)),
      open_row_(-1),
      row_hit_count_(0),
      //MZOU
      in_serve(0),
      serve_end_cycle(0),
      precharge_by_refresh(false),
      activate_by_who(0)
      //MZOU
    {
    cmd_timing_[static_cast<int>(CommandType::READ)] = 0;
    cmd_timing_[static_cast<int>(CommandType::READ_PRECHARGE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::WRITE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::WRITE_PRECHARGE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::ACTIVATE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::PRECHARGE)] = 0;
    cmd_timing_[static_cast<int>(CommandType::REFRESH)] = 0;
    cmd_timing_[static_cast<int>(CommandType::SREF_ENTER)] = 0;
    cmd_timing_[static_cast<int>(CommandType::SREF_EXIT)] = 0;
}


Command BankState::GetReadyCommand(const Command& cmd, uint64_t clk) {
    CommandType required_type = CommandType::SIZE;
    switch (state_) {
        case State::CLOSED:
            switch (cmd.cmd_type) {
                case CommandType::READ:
                case CommandType::READ_PRECHARGE:
                    activate_by_who = 0;
                    required_type = CommandType::ACTIVATE;
                    break;
                case CommandType::WRITE:
                case CommandType::WRITE_PRECHARGE:
                    activate_by_who = 1;
                    required_type = CommandType::ACTIVATE;
                    break;
                case CommandType::REFRESH:
                case CommandType::REFRESH_BANK:
                case CommandType::SREF_ENTER:
                    required_type = cmd.cmd_type;
                    break;
                default:
                    std::cerr << "Unknown type!" << std::endl;
                    AbruptExit(__FILE__, __LINE__);
                    break;
            }
            break;
        case State::OPEN:
            switch (cmd.cmd_type) {
                case CommandType::READ:
                case CommandType::READ_PRECHARGE:
                case CommandType::WRITE:
                case CommandType::WRITE_PRECHARGE:
                    if (cmd.Row() == open_row_) {
                        required_type = cmd.cmd_type;
                    } else {
                        precharge_by_refresh = 1;
                        required_type = CommandType::PRECHARGE;
                    }
                    break;
                case CommandType::REFRESH:
                case CommandType::REFRESH_BANK:
                case CommandType::SREF_ENTER:
                    precharge_by_refresh = 0;
                    required_type = CommandType::PRECHARGE;
                    break;
                default:
                    std::cerr << "Unknown type!" << std::endl;
                    AbruptExit(__FILE__, __LINE__);
                    break;
            }
            break;
        case State::SREF:
            switch (cmd.cmd_type) {
                case CommandType::READ:
                case CommandType::READ_PRECHARGE:
                case CommandType::WRITE:
                case CommandType::WRITE_PRECHARGE:
                    required_type = CommandType::SREF_EXIT;
                    break;
                default:
                    std::cerr << "Unknown type!" << std::endl;
                    AbruptExit(__FILE__, __LINE__);
                    break;
            }
            break;
        case State::PD:
        case State::SIZE:
            std::cerr << "In unknown state" << std::endl;
            AbruptExit(__FILE__, __LINE__);
            break;
    }

    //在这里控制时序
    //cmd_timing_返回的是required command在这个bank里最早可以开始执行的时间
    //也就是如果当前cycle，required command还不能开始执行，返回的是一个无效command
    if (required_type != CommandType::SIZE) {
        //std::cout << "current clk: " << clk << ", cmd_timing: " << cmd_timing_[static_cast<int>(required_type)] << std::endl;
        if (clk >= cmd_timing_[static_cast<int>(required_type)]) {
            return Command(required_type, cmd.addr, cmd.hex_addr);
        }
    }
    return Command();
}

void BankState::UpdateState(const Command& cmd) {
    switch (state_) {
        case State::OPEN:
            switch (cmd.cmd_type) {
                case CommandType::READ:
                case CommandType::WRITE:
                    row_hit_count_++;
                    break;
                case CommandType::READ_PRECHARGE:
                case CommandType::WRITE_PRECHARGE:
                case CommandType::PRECHARGE:
                    state_ = State::CLOSED;
                    open_row_ = -1;
                    row_hit_count_ = 0;
                    break;
                case CommandType::ACTIVATE:
                case CommandType::REFRESH:
                case CommandType::REFRESH_BANK:
                case CommandType::SREF_ENTER:
                case CommandType::SREF_EXIT:
                default:
                    AbruptExit(__FILE__, __LINE__);
            }
            break;
        case State::CLOSED:
            switch (cmd.cmd_type) {
                case CommandType::REFRESH:
                case CommandType::REFRESH_BANK:
                    break;
                case CommandType::ACTIVATE:
                    state_ = State::OPEN;
                    open_row_ = cmd.Row();
                    break;
                case CommandType::SREF_ENTER:
                    state_ = State::SREF;
                    break;
                case CommandType::READ:
                case CommandType::WRITE:
                case CommandType::READ_PRECHARGE:
                case CommandType::WRITE_PRECHARGE:
                case CommandType::PRECHARGE:
                case CommandType::SREF_EXIT:
                default:
                    std::cout << cmd << std::endl;
                    AbruptExit(__FILE__, __LINE__);
            }
            break;
        case State::SREF:
            switch (cmd.cmd_type) {
                case CommandType::SREF_EXIT:
                    state_ = State::CLOSED;
                    break;
                case CommandType::READ:
                case CommandType::WRITE:
                case CommandType::READ_PRECHARGE:
                case CommandType::WRITE_PRECHARGE:
                case CommandType::ACTIVATE:
                case CommandType::PRECHARGE:
                case CommandType::REFRESH:
                case CommandType::REFRESH_BANK:
                case CommandType::SREF_ENTER:
                default:
                    AbruptExit(__FILE__, __LINE__);
            }
            break;
        default:
            AbruptExit(__FILE__, __LINE__);
    }
    return;
}

void BankState::UpdateTiming(CommandType cmd_type, uint64_t time) {
    cmd_timing_[static_cast<int>(cmd_type)] =
        std::max(cmd_timing_[static_cast<int>(cmd_type)], time);
    return;
}

}  // namespace dramsim3
