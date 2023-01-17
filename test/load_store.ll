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

; Verify that loading and storing concrete values of various types works. For
; each type, we allocate space on the stack, then store a constant value into
; it, and finally load it back. Compiling this code with SymCC and verifying
; that the resulting binary exits cleanly shows that SymCC's instrumentation
; doesn't break the load/store operations.
;
; This test reproduces a bug where loading a concrete Boolean would lead to a
; program crash.
;
; Since the bitcode is written by hand, we first run llc on it because it
; performs a validity check, whereas Clang doesn't.
;
; RUN: llc %s -o /dev/null
; RUN: %symcc %s -o %t
; RUN: %t 2>&1

target triple = "x86_64-pc-linux-gnu"

define i32 @main(i32 %argc, i8** %argv) {
  ; Load and store a Boolean.
  %stack_bool = alloca i1
  store i1 0, i1* %stack_bool
  %copy_of_stack_bool = load i1, i1* %stack_bool

  ; Load and store a float.
  %stack_float = alloca float
  store float 0.0, float* %stack_float
  %copy_of_stack_float = load float, float* %stack_float

  ret i32 0
}
