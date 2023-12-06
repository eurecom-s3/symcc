import enum
import dataclasses


class SymbolKind(enum.Enum):
    BOOL = 0
    CONSTANT = 1
    READ = 2
    CONCAT = 3
    EXTRACT = 4
    ZEXT = 5
    SEXT = 6
    ADD = 7
    SUB = 8
    MUL = 9
    UDIV = 10
    SDIV = 11
    UREM = 12
    SREM = 13
    NEG = 14
    NOT = 15
    AND = 16
    OR = 17
    XOR = 18
    SHL = 19
    LSHR = 20
    ASHR = 21
    EQUAL = 22
    DISTINCT = 23
    ULT = 24
    ULE = 25
    UGT = 26
    UGE = 27
    SLT = 28
    SLE = 29
    SGT = 30
    SGE = 31
    LOR = 32
    LAND = 33
    LNOT = 34
    ITE = 35
    ROL = 36
    ROR = 37
    INVALID = 38


@dataclasses.dataclass
class Operation:
    kind: SymbolKind
    properties: dict


@dataclasses.dataclass
class RawSymbol:
    operation: Operation
    size_bits: int
    input_byte_dependency: list[int]
    args: list[str]


@dataclasses.dataclass
class RawTraceStep:
    pc: int
    memory_to_symbol_mapping: dict[str, str]


@dataclasses.dataclass
class RawPathConstraint:
    symbol: str
    after_step: int
    new_input_value: list[int] | None
    taken: bool


@dataclasses.dataclass
class RawTraceData:
    trace: list[RawTraceStep]
    symbols: dict[str, RawSymbol]
    path_constraints: list[RawPathConstraint]


@dataclasses.dataclass
class Symbol:
    operation: Operation
    size_bits: int
    input_byte_dependency: list[int]
    args: list['Symbol']


@dataclasses.dataclass
class TraceStep:
    pc: int
    memory_to_symbol_mapping: dict[str, Symbol]


@dataclasses.dataclass
class PathConstraint:
    symbol: Symbol
    after_step: TraceStep
    new_input_value: bytes | None
    taken: bool


@dataclasses.dataclass
class MemoryArea:
    address: int
    name: str


@dataclasses.dataclass
class TraceData:
    trace: list[TraceStep]
    symbols: dict[str, Symbol]
    path_constraints: list[PathConstraint]
    memory_areas: list[MemoryArea]
