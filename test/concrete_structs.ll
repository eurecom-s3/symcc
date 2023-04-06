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

; Verify that we create correct expressions from struct values. For each kind of
; value, we trigger expression creation by inserting a symbolic value into the
; struct. Compiling this code with SymCC and verifying that the resulting binary
; exits cleanly shows that SymCC's instrumentation doesn't break the execution
; of the program. Moreover, we store a struct value to memory, load one of its
; elements back into a register, and branch based on it in order to trigger the
; solver; by checking the generated test case we can verify that the expression
; was correct.
;
; This test reproduces a bug where creating expressions for some structs would
; lead to a program crash.
;
; Since the bitcode is written by hand, we first run llc on it because it
; performs a validity check, whereas Clang doesn't.

; RUN: llc %s -o /dev/null
; RUN: %symcc %s -o %t
; RUN: env SYMCC_MEMORY_INPUT=1 %t 2>&1 | %filecheck %s

target triple = "x86_64-pc-linux-gnu"

; The struct type which we'll create expressions for. Include a floating-point
; value and a Boolean because they're represented with non-bitvector solver
; variables (reproducing eurecom-s3/symcc#138).
%struct_type = type { i8, i32, i8, float, i1 }

; Global variable to record whether we've found a solution. Since the simple
; backend doesn't support test-case handlers, we start with "true".
@solved = global i1 1

; Our test-case handler verifies that the new test case is a 32-bit integer
; with the value 42.
define void @test_case_handler(i8* %data, i64 %data_length) {
  %correct_length = icmp eq i64 %data_length, 4
  br i1 %correct_length, label %check_data, label %failed

check_data:
  %value_pointer = bitcast i8* %data to i32*
  %value = load i32, i32* %value_pointer
  %correct_value = icmp eq i32 %value, 42
  br i1 %correct_value, label %all_good, label %failed

all_good:
  store i1 1, i1* @solved
  ret void

failed:
  store i1 0, i1* @solved
  ret void
}

define i32 @main(i32 %argc, i8** %argv) {
  ; Register our test-case handler.
  call void @symcc_set_test_case_handler(void (i8*, i64)* @test_case_handler)
  ; SIMPLE: Warning: test-case handlers

  ; Create a symbolic value that we can use to trigger the creation of struct
  ; expressions.
  %symbolic_value_mem = alloca i32
  store i32 1, i32* %symbolic_value_mem
  call void @symcc_make_symbolic(i32* %symbolic_value_mem, i64 4)
  %symbolic_value = load i32, i32* %symbolic_value_mem
  %symbolic_byte = trunc i32 %symbolic_value to i8

  ; Undef struct
  insertvalue %struct_type undef, i32 %symbolic_value, 1

  ; Struct with concrete value
  insertvalue %struct_type { i8 1, i32 undef, i8 2, float undef, i1 undef }, i32 %symbolic_value, 1

  ; Write a struct to memory and load one of its elements back into a register.
  ; It's important to also insert a symbolic value into the struct, so that we
  ; generate an expression in the first place.
  %struct_mem = alloca %struct_type
  %struct_value = insertvalue %struct_type { i8 0, i32 42, i8 undef, float undef, i1 undef }, i8 %symbolic_byte, 2
  store %struct_type %struct_value, %struct_type* %struct_mem
  %value_address = getelementptr %struct_type, %struct_type* %struct_mem, i32 0, i32 1
  %value_loaded = load i32, i32* %value_address
  %is_forty_two = icmp eq i32 %value_loaded, %symbolic_value
  br i1 %is_forty_two, label %never_executed, label %done
  ; QSYM: SMT

never_executed:
  br label %done

done:
  %solved = load i1, i1* @solved
  %result = select i1 %solved, i32 0, i32 1
  ret i32 %result
}

declare void @symcc_make_symbolic(i32*, i64)
declare void @symcc_set_test_case_handler(void (i8*, i64)*)
