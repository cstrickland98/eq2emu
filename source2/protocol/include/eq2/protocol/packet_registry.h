#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <eq2/protocol/opcode_table.h>
#include <eq2/protocol/opcode_version.h>

namespace eq2::protocol {

struct PacketDefinition {
  std::string name;
  std::uint16_t emu_opcode = kUnknownEmuOpcode;
  std::int16_t struct_version = 0;
};

class VersionedPacketRegistry {
 public:
  void define_packet(std::uint16_t emu_opcode, std::string name) {
    emu_to_name_[emu_opcode] = std::move(name);
  }

  void add_opcode_range(std::int16_t min_version, std::int16_t max_version) {
    opcode_versions_.add_range(min_version, max_version);
  }

  void register_version(std::int16_t opcode_version, const std::map<std::string, std::uint16_t>& eq_opcodes) {
    OpcodeTable table;
    for (const auto& [emu_opcode, name] : emu_to_name_) {
      table.define_emu_opcode(emu_opcode, name);
    }
    table.load_mappings(eq_opcodes);
    opcode_tables_[opcode_version] = std::move(table);
  }

  [[nodiscard]] auto opcode_version_for(std::int16_t client_version) const -> std::int16_t {
    return opcode_versions_.opcode_version_for(client_version);
  }

  [[nodiscard]] auto table_for_client(std::int16_t client_version) const -> const OpcodeTable* {
    const auto opcode_version = opcode_version_for(client_version);
    const auto found = opcode_tables_.find(opcode_version);
    if (found == opcode_tables_.end()) {
      return nullptr;
    }

    return &found->second;
  }

  [[nodiscard]] auto eq_to_emu(std::int16_t client_version, std::uint16_t eq_opcode) const
      -> std::optional<std::uint16_t> {
    const auto* table = table_for_client(client_version);
    if (table == nullptr) {
      return std::nullopt;
    }

    const auto emu_opcode = table->eq_to_emu(eq_opcode);
    if (emu_opcode == kUnknownEmuOpcode) {
      return std::nullopt;
    }

    return emu_opcode;
  }

  [[nodiscard]] auto emu_to_eq(std::int16_t client_version, std::uint16_t emu_opcode) const
      -> std::optional<std::uint16_t> {
    const auto* table = table_for_client(client_version);
    if (table == nullptr) {
      return std::nullopt;
    }

    const auto eq_opcode = table->emu_to_eq(emu_opcode);
    if (eq_opcode == 0 || eq_opcode == kMissingEqOpcode) {
      return std::nullopt;
    }

    return eq_opcode;
  }

  [[nodiscard]] auto packet_definition(std::uint16_t emu_opcode, std::int16_t struct_version) const
      -> std::optional<PacketDefinition> {
    const auto found = emu_to_name_.find(emu_opcode);
    if (found == emu_to_name_.end()) {
      return std::nullopt;
    }

    return PacketDefinition{
        .name = found->second,
        .emu_opcode = emu_opcode,
        .struct_version = struct_version,
    };
  }

 private:
  OpcodeVersionRanges opcode_versions_;
  std::unordered_map<std::uint16_t, std::string> emu_to_name_;
  std::map<std::int16_t, OpcodeTable> opcode_tables_;
};

}  // namespace eq2::protocol
