# ConeZ Tracker Shim on Raspberry Pi

This guide shows how to run the tracker shim on a Raspberry Pi so other machines on your LAN can open the tracker webpage, view node status, use telnet-in-browser, and store notes and traffic logs in SQLite.

## What the shim does

The tracker shim:

- connects to the MQTT broker over plain TCP
- subscribes to `conez/+/status`
- serves the tracker web UI over HTTP
- serves the telnet WebSocket bridge
- stores traffic logs and notes in a local SQLite database

SQLite runs locally on the Pi. There is no separate database server to install.

## Assumptions

This guide assumes:

- Raspberry Pi OS or another Debian-based Pi image
- Python 3 is already installed
- the `coneZ` repository is already present on the Pi
- the MQTT broker is reachable from the Pi

Examples below use:

- repo path: `/home/pi/coneZ`
- tracker path: `/home/pi/coneZ/tools/tracker`
- MQTT broker: `192.168.2.10`
- tracker HTTP port: `8080`

Adjust those to match your setup.

## 1. Install basic packages

```bash
sudo apt update
sudo apt install -y python3 python3-venv python3-pip git
```

If you want to use `start_tracker.sh` exactly as shipped, also install `zsh`:

```bash
sudo apt install -y zsh
```

If you do not want `zsh`, you can skip it and run `tracker_shim.py` directly instead. Both approaches are shown below.

## 2. Confirm the repo location

Example:

```bash
cd /home/pi/coneZ/tools/tracker
ls
```

You should see files like:

- `tracker_shim.py`
- `tracker.html`
- `telnet.html`
- `notes.html`
- `start_tracker.sh`

## 3. Choose where to store tracker data

The shim stores SQLite data locally on the Pi.

Recommended persistent location:

```bash
mkdir -p /home/pi/conez-tracker-data
```

That directory will hold the SQLite database, for example:

`/home/pi/conez-tracker-data/tracker_log.sqlite3`

## 4. Test-run the shim manually

### Option A: run the Python script directly

This is the simplest and most reliable option on a Pi:

```bash
cd /home/pi/coneZ/tools/tracker
python3 tracker_shim.py \
  --pi \
  --mqtt-host 192.168.2.10 \
  --http-port 8080 \
  --data-dir /home/pi/conez-tracker-data
```

What `--pi` does:

- binds HTTP and WebSocket services on all interfaces if you leave `--http-host` at its default
- uses a persistent data directory if you do not explicitly pass `--db-path`

### Option B: run the launcher script

If `zsh` is installed:

```bash
cd /home/pi/coneZ/tools/tracker
PI_MODE=1 \
MQTT_HOST=192.168.2.10 \
HTTP_PORT=8080 \
DATA_DIR=/home/pi/conez-tracker-data \
./start_tracker.sh
```

## 5. Open the tracker from another machine

Find the Pi IP address:

```bash
hostname -I
```

Then open in a browser on another machine:

```text
http://<pi-ip>:8080/
```

Example:

```text
http://192.168.2.40:8080/
```

The shim startup log also prints the HTTP and WebSocket URLs it is advertising.

## 6. Run the shim as a systemd service

This is the recommended way to keep it running after reboot.

Create `/etc/systemd/system/conez-tracker.service`:

```ini
[Unit]
Description=ConeZ Tracker Shim
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/coneZ/tools/tracker
ExecStart=/usr/bin/python3 /home/pi/coneZ/tools/tracker/tracker_shim.py --pi --mqtt-host 192.168.2.10 --http-port 8080 --data-dir /home/pi/conez-tracker-data
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

Reload and enable it:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now conez-tracker.service
```

Check status:

```bash
sudo systemctl status conez-tracker.service
```

View logs:

```bash
journalctl -u conez-tracker.service -f
```

## 7. Useful command-line options

The shim supports these Pi-relevant options:

- `--pi`
  Enables Pi-friendly defaults.
- `--mqtt-host <host>`
  MQTT broker hostname or IP.
- `--mqtt-port <port>`
  MQTT broker TCP port. Default: `1883`.
- `--http-host <host>`
  HTTP bind address. Default: `127.0.0.1`, but with `--pi` it effectively becomes reachable on the LAN when left at default.
- `--http-port <port>`
  Tracker HTTP port. Default: `8080`.
- `--ws-port <port>`
  Telnet WebSocket port. Default: `http-port + 1`.
- `--data-dir <dir>`
  Persistent tracker data directory.
- `--db-path <file>`
  Explicit SQLite file path.
- `--advertise-host <host-or-ip>`
  Controls which URL the shim prints for remote browsers.
- `--export-csv <path>`
  Exports logged MQTT traffic to CSV and exits.
- `--reset-db`
  Clears logged traffic and notes from the SQLite database and exits.

Show all options:

```bash
python3 /home/pi/coneZ/tools/tracker/tracker_shim.py --help
```

## 8. Database location and backups

The tracker data is stored in SQLite on the Pi. If you run with:

```bash
--data-dir /home/pi/conez-tracker-data
```

the database will be created at:

```text
/home/pi/conez-tracker-data/tracker_log.sqlite3
```

To back it up:

```bash
cp /home/pi/conez-tracker-data/tracker_log.sqlite3 /home/pi/conez-tracker-data/tracker_log.sqlite3.backup
```

To export the MQTT traffic log to CSV:

```bash
python3 /home/pi/coneZ/tools/tracker/tracker_shim.py \
  --data-dir /home/pi/conez-tracker-data \
  --export-csv /home/pi/conez-tracker-data/tracker_log.csv
```

To reset the database:

```bash
python3 /home/pi/coneZ/tools/tracker/tracker_shim.py \
  --data-dir /home/pi/conez-tracker-data \
  --reset-db
```

Note: `--reset-db` clears both MQTT traffic logs and saved node notes.

## 9. Firewall and networking

If the tracker is reachable locally on the Pi but not from another machine, check:

- the Pi IP address is correct
- the MQTT broker is reachable from the Pi
- port `8080` is not blocked by a firewall
- you are opening `http://<pi-ip>:8080/` and not `127.0.0.1`

If you use a different HTTP port, update the URL accordingly.

## 10. Troubleshooting

### `./start_tracker.sh: bad interpreter: /bin/zsh`

Install `zsh`:

```bash
sudo apt install -y zsh
```

Or skip the wrapper and run:

```bash
python3 /home/pi/coneZ/tools/tracker/tracker_shim.py --pi --mqtt-host 192.168.2.10 --http-port 8080 --data-dir /home/pi/conez-tracker-data
```

### Browser loads the page but no node data appears

Check the service log:

```bash
journalctl -u conez-tracker.service -f
```

Look for:

- MQTT connection failures
- wrong broker host or port
- DNS resolution problems

### Notes are not being saved

Check that the service user can write to the chosen data directory:

```bash
ls -ld /home/pi/conez-tracker-data
```

If needed:

```bash
sudo chown -R pi:pi /home/pi/conez-tracker-data
```

### The tracker starts but another machine cannot connect

Check which address the shim is bound to in the startup log. If needed, force it:

```bash
python3 /home/pi/coneZ/tools/tracker/tracker_shim.py \
  --mqtt-host 192.168.2.10 \
  --http-host 0.0.0.0 \
  --http-port 8080 \
  --data-dir /home/pi/conez-tracker-data
```

## 11. Recommended production command

For most Raspberry Pi installs, this is a good starting point:

```bash
python3 /home/pi/coneZ/tools/tracker/tracker_shim.py \
  --pi \
  --mqtt-host 192.168.2.10 \
  --http-port 8080 \
  --data-dir /home/pi/conez-tracker-data
```
