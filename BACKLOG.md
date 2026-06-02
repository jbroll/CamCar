# CamCar backlog

Deferred ideas / TODOs, not yet implemented.

## UI
- **Resolution lock button.** A lock toggle immediately to the **left of the
  max-resolution selector** (top-right of the video). When engaged it locks the
  current resolution: the auto-adapt up/down-shifting is disabled so the stream
  stays fixed at the chosen size. Implementation sketch: a lock-icon button that
  sends an adapt-enable/disable command over `/CarInput`; firmware gates the
  resolution changes in `CameraHandler::adaptAndReport()` (e.g. an `mAutoAdapt`
  flag, or pin ceiling=floor=current level while locked).
