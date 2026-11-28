' HOSTS: node fwhost   (the simulator applies its real gamma table (GAMMA256(128) is 37 there))
' EXPECTED:
' loaded=0 size=-1 v0=0 gamma128=128

' LUT API smoke test (LOADLUT/LUT/LUTSIZE/GAMMA256)
LOADED = LOADLUT(0)
N = LUTSIZE(0)
V = LUT(0)
G = GAMMA256(128)
FORMAT "loaded=% size=% v0=% gamma128=%", LOADED, N, V, G
