' EXPECTED:
' hello world ConeZ
' replaced
' world
' replaced world
' alpha omega

' String arrays — each cell holds a pool-allocated string handle (i32 ptr)
DIM S$(5)
S$(1) = "hello"
S$(2) = "world"
S$(3) = "ConeZ"
FORMAT "$ $ $", S$(1), S$(2), S$(3)

' Overwrite an element — frees the old string and stores a copy of the new
S$(1) = "replaced"
FORMAT "$", S$(1)

' Read into another string variable (gets a fresh copy)
T$ = S$(2)
FORMAT "$", T$

' Concat using array element on both sides
S$(4) = S$(1) + " " + S$(2)
FORMAT "$", S$(4)

' Multi-dim string array
DIM G$(2, 2)
G$(1, 1) = "alpha"
G$(2, 2) = "omega"
FORMAT "$ $", G$(1, 1), G$(2, 2)
