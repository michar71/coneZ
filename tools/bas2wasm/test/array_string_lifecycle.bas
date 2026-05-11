' String array memory lifecycle: ERASE, non-preserve REDIM, and PRESERVE
' shrink must free per-element pool strings (or they'd leak).

DIM S$(3)
S$(1) = "alpha"
S$(2) = "beta"
S$(3) = "gamma"

' ERASE frees per-element strings + array memory
ERASE S$

' REDIM after ERASE — works on a fresh array
DIM X$(2)
X$(1) = "after-erase"
FORMAT "$", X$(1)

' REDIM (non-preserve) on a populated string array — must free old elements
DIM Y$(3)
Y$(1) = "old1"
Y$(2) = "old2"
Y$(3) = "old3"
REDIM Y$(5)
Y$(1) = "new1"
FORMAT "$", Y$(1)

' REDIM PRESERVE shrink — high-index elements get dropped, must be freed
DIM Z$(5)
Z$(1) = "keep1"
Z$(2) = "keep2"
Z$(3) = "drop3"
Z$(4) = "drop4"
Z$(5) = "drop5"
REDIM PRESERVE Z$(2)      ' drops elements 3..5 — those strings must be freed
FORMAT "$ $", Z$(1), Z$(2)

' REDIM PRESERVE grow on a string array — new slots zero (= null = empty)
DIM W$(2)
W$(1) = "first"
REDIM PRESERVE W$(4)
W$(3) = "third"
FORMAT "$ $", W$(1), W$(3)

ERASE X$
ERASE Y$
ERASE Z$
ERASE W$
