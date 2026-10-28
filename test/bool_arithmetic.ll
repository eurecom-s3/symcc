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

; Verify that SymCC's Symbolizer handles add/sub/mul on i1 operands without
; crashing (eurecom-s3/symcc#171). The IR is written by hand so that the i1
; arithmetic instructions are seen by the Symbolizer; a C version compiled with
; optimizations would be folded into bitwise ops before reaching our pass and
; would not actually exercise the new switch cases.
;
; Since the bitcode is written by hand, we first run llc on it because it
; performs a validity check, whereas Clang doesn't.
;
; RUN: llc %s -o /dev/null
; RUN: %symcc %s -o %t
; RUN: echo -ne "\x01\x01" | %t

target triple = "x86_64-pc-linux-gnu"

define i32 @main(i32 %argc, i8** %argv) {
entry:
  ; Read two symbolic bytes from stdin and truncate each to i1.
  %bytes = alloca [2 x i8]
  %bytes_ptr = getelementptr [2 x i8], [2 x i8]* %bytes, i64 0, i64 0
  %read_count = call i64 @read(i32 0, i8* %bytes_ptr, i64 2)
  %b0_ptr = getelementptr [2 x i8], [2 x i8]* %bytes, i64 0, i64 0
  %b1_ptr = getelementptr [2 x i8], [2 x i8]* %bytes, i64 0, i64 1
  %b0_val = load i8, i8* %b0_ptr
  %b1_val = load i8, i8* %b1_ptr
  %b0 = trunc i8 %b0_val to i1
  %b1 = trunc i8 %b1_val to i1

  ; add/sub on i1 are arithmetic mod 2, equivalent to XOR; mul on i1 is
  ; equivalent to AND. Before the fix, visitBinaryOperator hit llvm_unreachable
  ; on each of these.
  %r_add = add i1 %b0, %b1
  %r_sub = sub i1 %r_add, %b0
  %r_mul = mul i1 %r_sub, %b1

  ; Branch on the symbolic bool so a path constraint built from the add/sub/mul
  ; expressions reaches the solver.
  br i1 %r_mul, label %t_label, label %f_label

t_label:
  ret i32 0

f_label:
  ret i32 0
}

declare i64 @read(i32, i8*, i64)
