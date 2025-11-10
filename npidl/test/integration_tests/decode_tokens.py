#!/usr/bin/env python3
import json

# Token type mapping from handle_initialize
TOKEN_TYPES = [
    "namespace",   # 0
    "interface",   # 1
    "class",       # 2 - struct/exception
    "enum",        # 3
    "function",    # 4
    "parameter",   # 5
    "property",    # 6 - field
    "type",        # 7
    "keyword"      # 8
]

TOKEN_MODIFIERS = [
    "readonly",    # bit 0
    "declaration", # bit 1
    "deprecated"   # bit 2
]

# Sample data from the test
data = [
    14, 2, 14, 6, 2,  # Token 0
    3, 0, 32, 2, 2,   # Token 1
    1, 0, 39, 2, 2,   # Token 2
    1, 0, 45, 2, 2,   # Token 3
    1, 0, 41, 2, 2,   # Token 4
    2, 2, 18, 6, 2,   # Token 5
    2, 0, 34, 2, 2,   # Token 6
    1, 0, 33, 2, 2    # Token 7
]

# Decode tokens
current_line = 0
current_col = 0

print("Decoded semantic tokens:\n")
print(f"{'Token':<6} {'Line':<5} {'Col':<5} {'Length':<7} {'Type':<12} {'Modifiers'}")
print("-" * 70)

for i in range(len(data) // 5):
    idx = i * 5
    delta_line = data[idx]
    delta_col = data[idx + 1]
    length = data[idx + 2]
    token_type = data[idx + 3]
    token_modifiers_bits = data[idx + 4]
    
    # Calculate absolute position
    if delta_line > 0:
        current_line += delta_line
        current_col = delta_col
    else:
        current_col += delta_col
    
    # Decode modifiers
    modifiers = []
    for bit_idx, mod_name in enumerate(TOKEN_MODIFIERS):
        if token_modifiers_bits & (1 << bit_idx):
            modifiers.append(mod_name)
    
    type_name = TOKEN_TYPES[token_type] if token_type < len(TOKEN_TYPES) else f"UNKNOWN({token_type})"
    mods_str = ", ".join(modifiers) if modifiers else "none"
    
    print(f"{i:<6} {current_line:<5} {current_col:<5} {length:<7} {type_name:<12} {mods_str}")

# Now read the actual file to see what's at those positions
print("\n\nActual file content at token positions:")
print("-" * 70)

with open("/home/nikita/projects/npsystem/nprpc/idl/nprpc_base.npidl") as f:
    lines = f.readlines()

current_line = 0
current_col = 0

for i in range(len(data) // 5):
    idx = i * 5
    delta_line = data[idx]
    delta_col = data[idx + 1]
    length = data[idx + 2]
    token_type = data[idx + 3]
    
    if delta_line > 0:
        current_line += delta_line
        current_col = delta_col
    else:
        current_col += delta_col
    
    # Get the text at this position (0-based line, 0-based col)
    if current_line < len(lines):
        line_text = lines[current_line]
        token_text = line_text[current_col:current_col + length] if current_col < len(line_text) else "???"
        type_name = TOKEN_TYPES[token_type] if token_type < len(TOKEN_TYPES) else f"UNKNOWN({token_type})"
        print(f"Token {i} (line {current_line}, col {current_col}): '{token_text}' [{type_name}]")
        print(f"  Context: {line_text.rstrip()}")
