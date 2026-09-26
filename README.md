# Experimental Wind

The canonical repository name, native module ID, and target are `wxl-experimental-wind` (the earlier `wxl-experimental-win` URL redirects here). This review branch carries the committed WXL v1.1 module snapshot from `b14c80943b500cd006bd86b3383f67deff238dce`. Its weather field supplies deterministic wind values to environment consumers, including experimental water, without making the wind provider depend on the water module. It is experimental and is not a standalone game-data patch.

## Integration and release checks

The source uses WXL core environment support, weather offsets, hooks/events, and the shared ImGui integration. `shared.cmake` and `target.cmake` are meant to be consumed by the matching WXL core build; copying this folder alone is not a portable CMake build. Build the Win32 DLL against a pinned core/API revision, then verify load, weather-driven field changes, dependent water behavior, and world/device transitions in the client. Package only the reviewed DLL and required license/manifest documentation. No client terrain, model, texture, or server files are provided here.

Keep this a draft until the exact source and core pair builds and the wind/water route passes a runtime test. For rollback, close the client and restore the previous compatible DLL/core set.

## Credits and license

Preserve WarcraftXL copyright headers and the GPL-3.0 license. Furioz is credited for the local v1.1 integration in Git history. No external game assets are bundled.
