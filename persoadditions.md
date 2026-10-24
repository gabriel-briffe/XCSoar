# perso additions (vs master)

Overview of features and UI mods on the `perso` branch that are not on `master`.

## Glide cone
- Reachable-area glide cone (GPU primary, CPU option) with DEM sampling
- Optional area (“Options”): CPU/GPU engine, colour by margin / reduced L/D, background compute
- Pink worst-case and green highest-arrival routes; red critical discs on air-to-ground steps
- Settings under Glide Computer → Glide Cone; InfoBox setup tab
- OpenGL overlay baking (RGBA) for drawing the options area

## Terrain / DEM
- Max-pooled DEM overview pyramid (HD / medium / coarse)
- Auto DEM from tile memory budget; DEM 2 / DEM 3 scale thresholds in settings
- DEM detail level next to the map scale bar
- GeoToScreen mesh for OpenGL terrain projection

## Map / InfoBox
- Map stopwatch from Status → Times (tap start/stop, hold to reset, hide clears; InfoBox look)
- Route drawn toggle (obstacle-avoiding route on/off)
- Waypoint label position (top/bottom × left/right)
- Simulator remembers last glider position/altitude between relaunches
- Status → Flight shows outside air temperature when a device reports it
- OAT alarm on Safety Factors (Off or a temperature; status message + beep)
- OAT probe, either way: USB serial `$POV,T` stays driver OpenVario (Pico CDC `2E8A:000A` already allowlisted). USB HID `2E8A:0A01` is port USB HID and driver OAT HID, no baud, not on the serial allowlist. Report is 4 bytes (id 1, int16 LE hundredths of °C, status). Status 1 ignores the number. Same temperature fields, InfoBox, Status Flight, and alarm. Mac desk test: TCP client `127.0.0.1` port `4354 (OAT HID)`, same driver (`emulate_oat_hid.py --tcp`)
- Animate zoom on Map Display → Orientation, under Circling zoom (default on). Off changes the scale at once. E-paper and slow CPUs always change at once. Stops a fast swipe series looking one zoom late while the short glide is still running

## Weather
- Rainbow.ai weather overlay (layers, cache-first Auto, checklist UI)
- XC Therm: GeoJSON overlays (earcut tessellation, holes), layer checklist, Preload Selected / Clear downloaded, download span
- SkySight: slippy-tile OverlayBitmap (Web Mercator), options strip as grouped list
- Shared weather map-overlay control models; EDL / RASP pages as grouped lists

## Settings UI
- GroupedListWidget: settings pages as scrollable grouped lists (Wind-style)
- Floating list dialogs (Flight Setup, Plane, Wind, Replay, Team code, Map Item List, …)
- Tiled Configuration menu (icon grid) with optional On setting; Close short=parent / long=map
- Quick Menu as button grid; Sounds On/Off tile; extra tile icons
- Flat button look, dialog/title/list polish (explanations under row, Off badge, list font for paths)
- GroupedListWidget hold: arm on the long-press timer, growing fill, commit on lift, slide-off cancels; e-paper/slow CPU show the fill only once armed
- Config → Pages: long press, or the Show/Hide button, hides a page without deleting it. Marked Off, saved as `PageNEnabled` (missing key stays on), skipped while turning pages. The last page that is on cannot be hidden. Keyboard/remote use the same button
- Android: InfoBox arrange / Sets content below the status-bar shade

## Other
- Mapbox earcut for OpenGL polygon tessellation (weather fills, etc.)
- Identical status text is one line while a copy is still waiting or on screen; its display time starts again. After it has gone, the next copy is a new message. Distinct messages still queue while a dialog is open
- English manual + NEWS coverage for the items above
