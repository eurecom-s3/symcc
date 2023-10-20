// This file is part of the SymCC runtime.
//
// The SymCC runtime is free software: you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// The SymCC runtime is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
// for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with the SymCC runtime. If not, see <https://www.gnu.org/licenses/>.

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
