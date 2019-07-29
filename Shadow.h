#ifndef SHADOW_H
#define SHADOW_H

#include <algorithm>
#include <cstring>
#include <iterator>
#include <map>

#include "Runtime.h"

#include <z3.h>

constexpr uintptr_t kPageSize = 4096;

/// Compute the corresponding page address.
constexpr uintptr_t pageStart(uintptr_t addr) {
  return (addr & ~(kPageSize - 1));
}

/// Compute the corresponding offset into the page.
constexpr uintptr_t pageOffset(uintptr_t addr) {
  return (addr & (kPageSize - 1));
}

/// A mapping from page addresses to the corresponding shadow regions. Each
/// shadow is large enough to hold one expression per byte on the shadowed page.
extern std::map<uintptr_t, Z3_ast *> g_shadow_pages;

class ReadShadowIterator
    : public std::iterator<std::input_iterator_tag, Z3_ast> {
public:
  explicit ReadShadowIterator(uintptr_t address)
      : std::iterator<std::input_iterator_tag, Z3_ast>(), address_(address),
        shadowPage_(getShadowPage(address)) {}

  ReadShadowIterator &operator++() {
    auto previousAddress = address_++;
    if (pageStart(address_) != pageStart(previousAddress))
      shadowPage_ = getShadowPage(address_);
    return *this;
  }

  Z3_ast operator*() {
    return shadowPage_ ? shadowPage_[pageOffset(address_)] : nullptr;
  }

  bool operator==(const ReadShadowIterator &other) {
    return (address_ == other.address_);
  }

  bool operator!=(const ReadShadowIterator &other) { return !(*this == other); }

protected:
  static Z3_ast *getShadowPage(uintptr_t address) {
    if (auto shadowPageIt = g_shadow_pages.find(pageStart(address));
        shadowPageIt != g_shadow_pages.end())
      return shadowPageIt->second;
    else
      return nullptr;
  }

  uintptr_t address_;
  Z3_ast *shadowPage_;
};

class NonNullReadShadowIterator : public ReadShadowIterator {
public:
  explicit NonNullReadShadowIterator(uintptr_t address)
      : ReadShadowIterator(address) {}

  Z3_ast operator*() {
    if (auto symbolicResult = ReadShadowIterator::operator*())
      return symbolicResult;

    return _sym_build_integer(*reinterpret_cast<const uint8_t *>(address_), 8);
  }
};

class WriteShadowIterator : public ReadShadowIterator {
public:
  WriteShadowIterator(uintptr_t address) : ReadShadowIterator(address) {
    shadowPage_ = getOrCreateShadowPage(address);
  }

  WriteShadowIterator &operator++() {
    auto previousAddress = address_++;
    if (pageStart(address_) != pageStart(previousAddress)) {
    }
    return *this;
  }

  Z3_ast &operator*() { return shadowPage_[pageOffset(address_)]; }

protected:
  static Z3_ast *getOrCreateShadowPage(uintptr_t address) {
    if (auto shadow = getShadowPage(address))
      return shadow;

    auto newShadow = static_cast<Z3_ast *>(malloc(kPageSize * sizeof(Z3_ast)));
    memset(newShadow, 0, kPageSize * sizeof(Z3_ast));
    g_shadow_pages[pageStart(address)] = newShadow;
    return newShadow;
  }
};

template <typename T> struct ReadOnlyShadow {
  ReadOnlyShadow(T *addr, size_t len)
      : address_(reinterpret_cast<uintptr_t>(addr)), length_(len) {}

  ReadShadowIterator begin() { return ReadShadowIterator(address_); }
  ReadShadowIterator end() { return ReadShadowIterator(address_ + length_); }

  NonNullReadShadowIterator begin_non_null() {
    return NonNullReadShadowIterator(address_);
  }

  NonNullReadShadowIterator end_non_null() {
    return NonNullReadShadowIterator(address_ + length_);
  }

  uintptr_t address_;
  size_t length_;
};

template <typename T> struct ReadWriteShadow {
  ReadWriteShadow(T *addr, size_t len)
      : address_(reinterpret_cast<uintptr_t>(addr)), length_(len) {}

  WriteShadowIterator begin() { return WriteShadowIterator(address_); }
  WriteShadowIterator end() { return WriteShadowIterator(address_ + length_); }

  uintptr_t address_;
  size_t length_;
};

template <typename T> bool isConcrete(T *addr, size_t nbytes) {
  ReadOnlyShadow shadow(addr, nbytes);
  return std::all_of(shadow.begin(), shadow.end(),
                     [](Z3_ast expr) { return (expr == nullptr); });
}

#endif
