import enum
import dataclasses


class ExpressionKind(enum.Enum):
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
    kind: ExpressionKind
    properties: dict


@dataclasses.dataclass
class RawExpression:
    operation: Operation
    size_bits: int
    input_byte_dependency: list[int]
    args: list[str]


@dataclasses.dataclass
class RawTraceStep:
    pc: int
    memory_to_expression_mapping: dict[str, str]


@dataclasses.dataclass
class RawPathConstraint:
    expression: str
    after_step: int
    new_input_value: list[int] | None
    taken: bool


@dataclasses.dataclass
class RawTraceData:
    trace: list[RawTraceStep]
    expressions: dict[str, RawExpression]
    path_constraints: list[RawPathConstraint]


@dataclasses.dataclass
class Expression:
    operation: Operation
    size_bits: int
    input_byte_dependency: list[int]
    args: list['Expression']


@dataclasses.dataclass
class TraceStep:
    pc: int
    memory_to_expression_mapping: dict[str, Expression]


@dataclasses.dataclass
class PathConstraint:
    expression: Expression
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
    expressions: dict[str, Expression]
    path_constraints: list[PathConstraint]
    memory_areas: list[MemoryArea]
