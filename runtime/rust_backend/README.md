# SymCC Rust Runtime

This runtime is a wrapper around a stripped down runtime which can be implemented in Rust (or any other language).
This wrapper implements Garbage Collection like the the Simple and QSym runtimes and implements the `_sym_bits_helper`.

The functions that are left to be implemented by the wrapped runtime are defined in `RustRuntime.h` and mirror those which are defined in `RuntimeCommon.h` except for having new name prefixes and missing those which are related to memory management and utilites.

## GC implementation
The GC implementation works by keeping track of all expressions that the wrapped runtime generates and calling a new method (`_rsym_expression_unreachable(RSymExpr)`) for each expression that became unreachable in terms of the GC.
The details of this implementation are the same as those of the Simple backend (it's a straight copy).

## Bits Helper
The bits helper is implemented by embedding the number of bits inside the expression pointer.
Specifically, the least significant byte contains the bit width of the expression.
Boolean expressions have a bit width of 0.
The actual expression pointer is shifted towards the MSB to make space for the bit width.
This reduces the amount of available bits in the expression pointer by 8.
The runtime panics if an expression pointer is returned that would not fit, but this is not expected on 64-bit systems.
(On 32-bit systems this may be a problem, but at this point, we don't care about 32-bit.)

On a high level, this means that there are two `SymExpr` types now: `SymExpr`, which is used by the wrapper, and `RSymExpr`, which is used by the wrapped runtime.
The wrapper takes care of translating between the two representations as necessary.

The wrapper also takes care of maintaining the correct bit widths by calculating the resulting width when a width-changing instruction is encountered.