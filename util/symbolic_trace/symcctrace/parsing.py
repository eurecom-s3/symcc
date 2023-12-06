import functools
import itertools
import json
import math
import pathlib

import cattrs

from . import data

MEMORY_AREA_MAX_DISTANCE = 0x100000


def parse_trace(
        trace_file: pathlib.Path,
        memory_region_names_file: pathlib.Path,
) -> data.TraceData:
    with open(trace_file) as file:
        raw_trace_data = cattrs.Converter(forbid_extra_keys=True).structure(json.load(file), data.RawTraceData)

    with open(memory_region_names_file) as file:
        def convert_to_memory_area(raw_memory_area: dict[str, str]) -> data.MemoryArea:
            return cattrs.Converter(forbid_extra_keys=True).structure(raw_memory_area, data.MemoryArea)

        memory_areas = list(map(convert_to_memory_area, json.load(file)))

    symbols = _convert_symbols(raw_trace_data.symbols)
    trace_steps = list(
        map(functools.partial(_convert_trace_step, symbols=symbols, memory_areas=memory_areas), raw_trace_data.trace))
    path_constraints = list(map(functools.partial(_convert_path_constraint, symbols=symbols, trace_steps=trace_steps),
                                raw_trace_data.path_constraints))

    trace_data = data.TraceData(
        trace=trace_steps,
        symbols=symbols,
        path_constraints=path_constraints,
        memory_areas=memory_areas
    )

    return trace_data


def _convert_operation(raw_operation: data.Operation, size_bits: int) -> data.Operation:
    operation = data.Operation(
        kind=data.SymbolKind(raw_operation.kind),
        properties=raw_operation.properties
    )

    if 'value' in operation.properties:
        operation.properties['value'] = int(operation.properties['value']).to_bytes(math.ceil(size_bits / 8), 'little')

    return operation


def _convert_symbols(raw_symbols: dict[str, data.RawSymbol]) -> dict[str, data.Symbol]:
    symbols = {}

    def recursively_create_symbol(symbol_id: str):
        if symbol_id in symbols:
            return symbols[symbol_id]

        raw_symbol = raw_symbols[symbol_id]
        args = [recursively_create_symbol(arg) for arg in raw_symbol.args]

        symbol = data.Symbol(
            operation=_convert_operation(raw_symbol.operation, raw_symbol.size_bits),
            args=args,
            input_byte_dependency=raw_symbol.input_byte_dependency,
            size_bits=raw_symbol.size_bits
        )
        symbols[symbol_id] = symbol
        return symbol

    for symbol_id in raw_symbols:
        recursively_create_symbol(symbol_id)

    return symbols


def _raw_memory_address_to_named_location(raw_memory_address: int, memory_areas: list[data.MemoryArea]) -> str:
    def distance(memory_area: data.MemoryArea) -> int:
        return raw_memory_address - memory_area.address

    def is_candidate(memory_area: data.MemoryArea) -> bool:
        return \
            abs(distance(memory_area)) < MEMORY_AREA_MAX_DISTANCE \
                if memory_area.name == 'stack' \
                else 0 <= distance(memory_area) < MEMORY_AREA_MAX_DISTANCE

    def absolute_distance(memory_area: data.MemoryArea) -> int:
        return abs(distance(memory_area))

    closest_memory_area = min(filter(is_candidate, memory_areas), key=absolute_distance)

    return f'{closest_memory_area.name}+{hex(distance(closest_memory_area))}'


def _convert_trace_step(raw_trace_step: data.RawTraceStep, symbols: dict[str, data.Symbol],
                        memory_areas: list[data.MemoryArea]) -> data.TraceStep:
    def convert_mapping(mapping: dict[str, str]) -> dict[str, data.Symbol]:
        def convert_mapping_element(raw_address: str, symbol_id: str) -> tuple[str, data.Symbol]:
            return _raw_memory_address_to_named_location(int(raw_address), memory_areas), symbols[symbol_id]

        return dict(itertools.starmap(convert_mapping_element, mapping.items()))

    return data.TraceStep(
        pc=raw_trace_step.pc,
        memory_to_symbol_mapping=convert_mapping(raw_trace_step.memory_to_symbol_mapping)
    )


def _convert_path_constraint(raw_path_constraint: data.RawPathConstraint, symbols: dict[str, data.Symbol],
                             trace_steps: list[data.TraceStep]) -> data.PathConstraint:
    return data.PathConstraint(
        symbol=symbols[raw_path_constraint.symbol],
        after_step=trace_steps[raw_path_constraint.after_step],
        new_input_value=bytes(
            raw_path_constraint.new_input_value) if raw_path_constraint.new_input_value is not None else None,
        taken=raw_path_constraint.taken
    )
