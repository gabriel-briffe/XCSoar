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
- Android: InfoBox arrange / Sets content below the status-bar shade

## Other
- Mapbox earcut for OpenGL polygon tessellation (weather fills, etc.)
- English manual + NEWS coverage for the items above
