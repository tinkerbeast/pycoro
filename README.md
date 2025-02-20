# pycoro
Python coroutines without asyncio

### Build

For debug build (for some reason this does not generate wheel).
```
pip install -e . --no-build-isolation -Csetup-args=-Dbuildtype=debug -Cbuild-dir=build-dbg
```

For generating wheel.
```
python3 -m build --wheel
```

For production build. Needs things checked into git to work.
```
python3 -m build
```
