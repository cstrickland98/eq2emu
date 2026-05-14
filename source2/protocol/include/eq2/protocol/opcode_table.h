#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

namespace eq2::protocol {

inline constexpr std::uint16_t kMissingEqOpcode = 0xffff;
inline constexpr std::uint16_t kUnknownEmuOpcode = 0;

class OpcodeTable {
 public:
  void define_emu_opcode(std::uint16_t emu_opcode, std::string name) {
    name_to_emu_[std::move(name)] = emu_opcode;
  }

  void load_mappings(const std::map<std::string, std::uint16_t>& eq_opcodes) {
    emu_to_eq_.clear();
    eq_to_emu_.clear();

    for (const auto& [name, emu_opcode] : name_to_emu_) {
      const auto found = eq_opcodes.find(name);
      const auto eq_opcode = found == eq_opcodes.end() ? kMissingEqOpcode : found->second;
      set_opcode(emu_opcode, eq_opcode);
    }
  }

  void set_opcode(std::uint16_t emu_opcode, std::uint16_t eq_opcode) {
    if (const auto old = emu_to_eq_.find(emu_opcode); old != emu_to_eq_.end()) {
      eq_to_emu_.erase(old->second);
    }

    emu_to_eq_[emu_opcode] = eq_opcode;
    eq_to_emu_[eq_opcode] = emu_opcode;
  }

  [[nodiscard]] auto emu_to_eq(std::uint16_t emu_opcode) const -> std::uint16_t {
    const auto found = emu_to_eq_.find(emu_opcode);
    return found == emu_to_eq_.end() ? 0 : found->second;
  }

  [[nodiscard]] auto eq_to_emu(std::uint16_t eq_opcode) const -> std::uint16_t {
    const auto found = eq_to_emu_.find(eq_opcode);
    return found == eq_to_emu_.end() ? kUnknownEmuOpcode : found->second;
  }

 private:
  std::unordered_map<std::string, std::uint16_t> name_to_emu_;
  std::unordered_map<std::uint16_t, std::uint16_t> emu_to_eq_;
  std::unordered_map<std::uint16_t, std::uint16_t> eq_to_emu_;
};

}  // namespace eq2::protocol
