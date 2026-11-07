# Device entry point

Starts the retained Vibe Mode control path, initializes the BSP display, and
presents the bridge snapshot as a 416×416 RGB565 canvas. Each source pixel
occupies a 13×13 block. The management address appears below the canvas.
The board display starts independently of the optional BLE panel link.

The standby owner handles AI/touch interrupts, display suspension and PM locks.
It retains Wi-Fi/MQTT for print wake, pauses idle BLE linking at the configured
timeout and prevents light sleep while an active USB host is attached. Battery
polling slows from five to thirty seconds while the display is off. Runtime
diagnostics are read-only; this application does not apply experimental gauge profiles.
