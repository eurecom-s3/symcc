; This file is part of SymCC.
;
; SymCC is free software: you can redistribute it and/or modify it under the
; terms of the GNU General Public License as published by the Free Software
; Foundation, either version 3 of the License, or (at your option) any later
; version.
;
; SymCC is distributed in the hope that it will be useful, but WITHOUT ANY
; WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
; A PARTICULAR PURPOSE. See the GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License along with
; SymCC. If not, see <https://www.gnu.org/licenses/>.

; Verify that we correctly insert into symbolic struct values. We insert values
; of various types into a symbolic struct, thus triggering expression updates.
; Compiling this code with SymCC and verifying that the resulting binary exits
; cleanly shows that SymCC's instrumentation doesn't break the execution of the
; program.
;
; This test reproduces a bug where inserting a concrete floating-point value
; into a symbolic struct would lead to a program crash (eurecom-s3/symcc#138).
;
; Since the bitcode is written by hand, we first run llc on it because it
; performs a validity check, whereas Clang doesn't.

; RUN: llc %s -o /dev/null
; RUN: %symcc %s -o %t
; RUN: env SYMCC_MEMORY_INPUT=1 %t 2>&1

target triple = "x86_64-pc-linux-gnu"

; The struct type of our symbolic value. Include a floating-point value and a
; Boolean because they're represented with non-bitvector solver variables
; (reproducing eurecom-s3/symcc#138).
%struct_type = type { i8, i32, i8, float, i1 }

define i32 @main(i32 %argc, i8** %argv) {
  ; Create a symbolic struct value that we can subsequently insert values into.
  %struct_value_mem = alloca %struct_type
  call void @symcc_make_symbolic(%struct_type* %struct_value_mem, i64 20)
  %symbolic_struct = load %struct_type, %struct_type* %struct_value_mem

  ; Insert values of various types, triggering the creation of new expressions.
  insertvalue %struct_type %symbolic_struct, i32 5, 1
  insertvalue %struct_type %symbolic_struct, float 42.0, 3
  insertvalue %struct_type %symbolic_struct, i1 1, 4

  ret i32 0
}

declare void @symcc_make_symbolic(%struct_type*, i64)
