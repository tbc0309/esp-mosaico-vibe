# Device entry point

Starts the retained Vibe Mode control path, initializes the BSP display, and
presents the bridge snapshot as a 448×448 RGB565 canvas. Each source pixel
occupies a 14×14 block. The management address appears below the canvas.
The board display starts independently of the optional BLE panel link.
