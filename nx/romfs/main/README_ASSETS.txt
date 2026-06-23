romfs:/main  (inside the .nro)
=============================

This directory is baked into the .nro as read-only data and is searched by the
engine as an additional game directory. It is meant ONLY for small, freely
redistributable helper files - currently just:

    autoexec.cfg   default controller bindings & console cvars

DO NOT put Medal of Honor: Allied Assault game data (pak0.pk3 ... pakN.pk3)
here. Those files are copyrighted and must NOT be redistributed inside the .nro.

Where the real game data goes
-----------------------------
Copy the "main" folder from your legitimate MOHAA install (and "mainta" for
Spearhead / "maintt" for Breakthrough) onto your SD card:

    sdmc:/switch/openmohaa/main/pak0.pk3
    sdmc:/switch/openmohaa/main/pak1.pk3
    ...
    sdmc:/switch/openmohaa/mainta/...     (Spearhead, optional)
    sdmc:/switch/openmohaa/maintt/...     (Breakthrough, optional)

The engine searches sdmc:/switch/openmohaa (writable) and this romfs:/ tree
together, so your paks on the SD card are picked up automatically.
