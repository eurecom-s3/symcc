#ifndef PIN_H
#define PIN_H

//
// This file provides mocks for everything that would normally be included from
// Pin, so that we don't have to change all the Qsym code.
//

#include <cstdint>
#include <string>
#include <sstream>

using namespace std;

typedef uint8_t UINT8;
typedef unsigned int USIZE;
typedef unsigned int UINT;
typedef int INT;
typedef int32_t INT32;
typedef uint32_t UINT32;
typedef uint64_t __uint64;
typedef uintptr_t ADDRINT;

/// Return the hex representation of a number.
template<typename T> string hexstr(T arg) {
  stringstream stream;
  stream << hex << arg;
  return stream.str();
}

#define decstr(x) to_string(x)

#endif
