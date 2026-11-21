#pragma once

#include <cstdint>
#include <string>

enum class wasmhex_atomic_kind_t : uint8_t
{
  notify,
  wait32,
  wait64,
  fence,
  load,
  store,
  rmw,
  cmpxchg,
};

struct wasmhex_atomic_info_t
{
  wasmhex_atomic_kind_t kind;
  uint8_t value_bits;
  uint8_t access_size;
  const char *operation;
};

inline bool wasmhex_atomic_info(uint64_t subopcode, wasmhex_atomic_info_t *info)
{
  static constexpr uint8_t value_bits[] = { 32, 64, 32, 32, 64, 64, 64 };
  static constexpr uint8_t access_sizes[] = { 4, 8, 1, 2, 1, 2, 4 };
  static constexpr const char *operations[] = {
    "add", "sub", "and", "or", "xor", "xchg", "cmpxchg",
  };
  if ( info == nullptr )
    return false;
  if ( subopcode <= 3 )
  {
    info->kind = subopcode == 0 ? wasmhex_atomic_kind_t::notify
      : subopcode == 1          ? wasmhex_atomic_kind_t::wait32
      : subopcode == 2          ? wasmhex_atomic_kind_t::wait64
                                : wasmhex_atomic_kind_t::fence;
    info->value_bits = subopcode == 2 ? 64 : 32;
    info->access_size = subopcode == 2 ? 8 : 4;
    info->operation = nullptr;
    return true;
  }
  if ( subopcode >= 0x10 && subopcode <= 0x1D )
  {
    const bool store = subopcode >= 0x17;
    const unsigned variant = unsigned(subopcode - (store ? 0x17 : 0x10));
    if ( variant >= 7 )
      return false;
    info->kind = store ? wasmhex_atomic_kind_t::store : wasmhex_atomic_kind_t::load;
    info->value_bits = value_bits[variant];
    info->access_size = access_sizes[variant];
    info->operation = nullptr;
    return true;
  }
  if ( subopcode >= 0x1E && subopcode <= 0x4E )
  {
    const unsigned group = unsigned((subopcode - 0x1E) / 7);
    const unsigned variant = unsigned((subopcode - 0x1E) % 7);
    info->kind = group == 6 ? wasmhex_atomic_kind_t::cmpxchg : wasmhex_atomic_kind_t::rmw;
    info->value_bits = value_bits[variant];
    info->access_size = access_sizes[variant];
    info->operation = operations[group];
    return true;
  }
  return false;
}

inline std::string wasmhex_atomic_mnemonic(const wasmhex_atomic_info_t &info)
{
  if ( info.kind == wasmhex_atomic_kind_t::notify )
    return "memory.atomic.notify";
  if ( info.kind == wasmhex_atomic_kind_t::wait32 )
    return "memory.atomic.wait32";
  if ( info.kind == wasmhex_atomic_kind_t::wait64 )
    return "memory.atomic.wait64";
  if ( info.kind == wasmhex_atomic_kind_t::fence )
    return "atomic.fence";

  std::string name = info.value_bits == 32 ? "i32.atomic." : "i64.atomic.";
  const bool narrow = info.access_size * 8 != info.value_bits;
  if ( info.kind == wasmhex_atomic_kind_t::load )
    name += "load";
  else if ( info.kind == wasmhex_atomic_kind_t::store )
    name += "store";
  else
  {
    name += "rmw";
    if ( narrow )
      name += std::to_string(info.access_size * 8);
    name += ".";
    name += info.operation;
  }
  if ( narrow && info.kind != wasmhex_atomic_kind_t::rmw
       && info.kind != wasmhex_atomic_kind_t::cmpxchg )
    name += std::to_string(info.access_size * 8);
  if ( info.kind == wasmhex_atomic_kind_t::load && narrow )
    name += "_u";
  if ( (info.kind == wasmhex_atomic_kind_t::rmw || info.kind == wasmhex_atomic_kind_t::cmpxchg)
       && narrow )
    name += "_u";
  return name;
}
