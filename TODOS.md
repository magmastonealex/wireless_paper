X Update firmware to give up and sleep after a few minutes of trying to connect to openthread.
- Tidy up code.
- Write some infrastructure to "genericize" displays - can we have a byte sequence for init, then write all the data, then a byte sequence for trigger refresh, then a byte sequence for shutdown?
    -> Those sequences can come from example code for each display.
  -> Byte sequence for partial display update to write a small portion in the corner for a "disconnected" icon?