TCP-MQTT->websockett shim

This shim is nessassary if the MQTT server does not support web sockets.
It creates a local web server that internally provides the TCP to web socket translation.

Usage:

Start the shim with:
tools/tracker/start_tracker.sh

Connect to the webpage in the browser via:
http://127.0.0.1:8080/

Useful Raspberry Pi options:

- `--pi`
  Switches the shim to Pi-friendly defaults. If you leave `--http-host` at its default, this changes the bind host from `127.0.0.1` to `0.0.0.0` so other machines on the LAN can reach it. If you do not set `--db-path`, it also moves the SQLite database to `~/.local/share/conez-tracker/tracker_log.sqlite3`.
- `--data-dir <dir>`
  Stores persistent shim data, including the SQLite database, in the directory you choose.
- `--db-path <file>`
  Explicitly sets the SQLite database file path.
- `--advertise-host <host-or-ip>`
  Controls the URL printed at startup for remote browsers. This is useful when the Pi has multiple interfaces or you want the shim to print a fixed hostname.
- `--ws-port <port>`
  Sets the telnet WebSocket port explicitly instead of using `http-port + 1`.

Example Pi command:

`python3 tools/tracker/tracker_shim.py --pi --mqtt-host 192.168.2.10 --http-port 8080`

Example using the launcher script with environment variables:

`PI_MODE=1 MQTT_HOST=192.168.2.10 HTTP_HOST=0.0.0.0 HTTP_PORT=8080 DATA_DIR=/home/pi/conez-tracker tools/tracker/start_tracker.sh`
