' HOSTS: node fwhost   (the simulator's mock GPS has a fix)
' EXPECTED:
' -1
' 0
' has imu
' no gps

' Predicate builtins return BASIC truth (-1 / 0), so NOT works on them.
' (The harness reports an IMU present and no GPS.)
> IMUPRESENT()
> NOT IMUPRESENT()
IF NOT IMUPRESENT() THEN > "no imu" ELSE > "has imu"
IF NOT HASGPS() THEN > "no gps"
