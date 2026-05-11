' WASM-native math opcodes: SQRT, FLOOR, CEIL (no imports needed)
S# = SQRT(16.0)
F# = FLOOR(3.7)
C# = CEIL(3.2)
' Negative inputs
FN# = FLOOR(-3.2)
CN# = CEIL(-3.7)
FORMAT "& & & & &", S#, F#, C#, FN#, CN#
