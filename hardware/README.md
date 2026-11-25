# Hardware

```
hardware/
  pcb/                              KiCad project for the ramp control board
    Ramp Control PCB.kicad_pro        open this in KiCad (KiCad 10, stock libraries only)
    Ramp Control PCB.kicad_sch        schematic
    Ramp Control PCB.kicad_pcb        board layout
    Ramp Control PCB.csv              BOM (exported from the schematic)
    gerbers/                          fabrication outputs; Gerbers.zip is the fab-house upload
  cad/
    Ramp Control PCB.step             3D model of the assembled board
    Ramp Control PCB Controller Mount.SLDASM   SolidWorks assembly for the board mount
```

Notes:

- The Gerbers were exported on 2026-11-12. If the board is changed, re-export them from
  KiCad (File > Fabrication Outputs) into `pcb/gerbers/` and rebuild `Gerbers.zip`.
- The SolidWorks assembly references part files (`.SLDPRT`) that are not in the repo yet; add them to `cad/`
  so the assembly opens.
- KiCad's per-user settings (`*.kicad_prl`) and local history (`.history/`) are not tracked.

The pin assignments and wiring facts the firmware depends on are listed in `docs/DEVELOPMENT.md`
("Hardware facts the code depends on").
