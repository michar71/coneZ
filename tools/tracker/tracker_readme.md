TCP-MQTT->websockett shim

This shim is nessassary if the MQTT server does not support web sockets.
It creates a local web server that internally provides the TCP to web socket translation.

Usage:

Start the shim with:
tools/tracker/start_tracker.sh

Connect to the webpage in the browser via:
http://127.0.0.1:8080/