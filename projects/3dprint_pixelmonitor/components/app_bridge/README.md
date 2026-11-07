# Application bridge

Owns the Bambu Lab network clients, retained web configuration, BLE binding and
shared 32×32 renderer. Call `bridgeStart()` once after Vibe Mode recovery support
starts, then call `bridgePoll()` from the same application task approximately
every 50 ms. The returned snapshot uses the shared display policy. The bridge
does not own the device display or the firmware update writer.
