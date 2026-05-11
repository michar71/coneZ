' LED API smoke test — SETLEDCOL takes (R,G,B), returns 0
S = SETLEDCOL(255, 0, 0)
S = SETLEDCOL(0, 255, 0)
S = SETLEDCOL(0, 0, 255)
S = WAIT(10)
N = GETMAXLED()
FORMAT "led_max=%", N
