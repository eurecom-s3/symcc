#ifndef SHADOW_H
#define SHADOW_H

#include <algorithm>
#include <cstring>
#include <iterator>
#include <map>

#include <Runtime.h>

#include <z3.h>

//
// This file is dedicated to the management of shadow memory.
//
// We manage shadows at page granularity. Since the shadow for each page is
// malloc'ed and thus at an unpredictable location in memory, we need special
// handling for memory allocations that cross page boundaries. This header
// provides iterators over shadow memory that automatically handle jumps between
// memory pages (and thus shadow regions). They should work with the C++
// standard library.
//

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
extern std::map<uintptr_t, SymExpr *> g_shadow_pages;

/// An iterator that walks over the shadow bytes corresponding to a memory
/// region. If there is no shadow for any given memory address, it just returns
/// null.
class ReadShadowIterator
    : public std::iterator<std::input_iterator_tag, SymExpr> {
public:
  explicit ReadShadowIterator(uintptr_t address)
      : std::iterator<std::input_iterator_tag, SymExpr>(), address_(address),
        shadow_(getShadow(address)) {}

  ReadShadowIterator &operator++() {
    auto previousAddress = address_++;
    if (shadow_)
      shadow_++;
    if (pageStart(address_) != pageStart(previousAddress))
      shadow_ = getShadow(address_);
    return *this;
  }

  SymExpr operator*() { return shadow_ ? *shadow_ : nullptr; }

  bool operator==(const ReadShadowIterator &other) {
    return (address_ == other.address_);
  }

  bool operator!=(const ReadShadowIterator &other) { return !(*this == other); }

protected:
  static SymExpr *getShadow(uintptr_t address) {
    if (auto shadowPageIt = g_shadow_pages.find(pageStart(address));
        shadowPageIt != g_shadow_pages.end())
      return shadowPageIt->second + pageOffset(address);
    else
      return nullptr;
  }

  uintptr_t address_;
  SymExpr *shadow_;
};

/// Like ReadShadowIterator, but return an expression for the concrete memory
/// value if a region does not have a shadow.
class NonNullReadShadowIterator : public ReadShadowIterator {
public:
  explicit NonNullReadShadowIterator(uintptr_t address)
      : ReadShadowIterator(address) {}

  SymExpr operator*() {
    if (auto symbolicResult = ReadShadowIterator::operator*())
      return symbolicResult;

    return _sym_build_integer(*reinterpret_cast<const uint8_t *>(address_), 8);
  }
};

/// An iterator that walks over the shadow corresponding to a memory region and
/// exposes it for modification. If there is no shadow yet, it creates a new
/// one.
class WriteShadowIterator : public ReadShadowIterator {
public:
  WriteShadowIterator(uintptr_t address) : ReadShadowIterator(address) {
    shadow_ = getOrCreateShadow(address);
  }

  WriteShadowIterator &operator++() {
    auto previousAddress = address_++;
    shadow_++;
    if (pageStart(address_) != pageStart(previousAddress))
      shadow_ = getOrCreateShadow(address_);
    return *this;
  }

  SymExpr &operator*() { return *shadow_; }

protected:
  static SymExpr *getOrCreateShadow(uintptr_t address) {
    if (auto shadow = getShadow(address))
      return shadow;

    auto newShadow =
        static_cast<SymExpr *>(malloc(kPageSize * sizeof(SymExpr)));
    memset(newShadow, 0, kPageSize * sizeof(SymExpr));
    g_shadow_pages[pageStart(address)] = newShadow;
    return newShadow + pageOffset(address);
  }
};

/// A view on shadow memory that exposes read-only functionality.
struct ReadOnlyShadow {
  template <typename T>
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

/// A view on shadow memory that allows modifications.
template <typename T> struct ReadWriteShadow {
  ReadWriteShadow(T *addr, size_t len)
      : address_(reinterpret_cast<uintptr_t>(addr)), length_(len) {}

  WriteShadowIterator begin() { return WriteShadowIterator(address_); }
  WriteShadowIterator end() { return WriteShadowIterator(address_ + length_); }

  uintptr_t address_;
  size_t length_;
};

/// Check whether the indicated memory range is concrete, i.e., there is no
/// symbolic byte in the entire region.
template <typename T> bool isConcrete(T *addr, size_t nbytes) {
  // Fast path for allocations within one page.
  auto byteBuf = reinterpret_cast<uintptr_t>(addr);
  if (pageStart(byteBuf) == pageStart(byteBuf + nbytes) &&
      !g_shadow_pages.count(pageStart(byteBuf)))
    return true;

  ReadOnlyShadow shadow(addr, nbytes);
  return std::all_of(shadow.begin(), shadow.end(),
                     [](SymExpr expr) { return (expr == nullptr); });
}

#endif
