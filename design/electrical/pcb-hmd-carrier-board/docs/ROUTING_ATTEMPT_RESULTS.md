# Board2 routing promotion results

Validation date: 2026-08-20.

Only routes satisfying all of the following were promoted to
`pcb-hmd-carrier-board2.kicad_pcb`: electrically complete net, no DRC finding,
no orphaned differential-pair member, no undersized power segment, and no
regression against the approved placement baseline.

## Promoted

- Restored microSD hardware remains on the main PCB. `SD_DAT0` and `SD_CLK` are
  complete; the remaining eight storage nets are still open.
- A pruned low-speed pass retained 28 additional complete nets.
- Current authoritative copper: 1,077 segments and 213 vias.
- Current uncapped connectivity: 817 physical open edges across 184 incomplete
  nets, improved from 866/214 at the first routing checkpoint.
- Current geometry DRC: zero violations; component count remains 312.

## Rejected

- Power pass: its only two candidate complete rails crossed already validated
  low-speed copper after merge. Both routes were rejected.
- Left MIPI pass: the width-restored result had 149 DRC violations; no complete,
  paired, DRC-clean differential route was promotable.
- Right MIPI pass: the width-restored result had 75 DRC violations; no complete,
  paired, DRC-clean differential route was promotable.
- Raw/full autorouter candidates are review artifacts only and must not be used
  for fabrication.

## Release conclusion

This is a preserved, reconciled placement and partial-routing checkpoint, not a
fabrication release. A production claim would be false while 817 open edges,
zero complete high-speed pairs, the unassigned J7 tracking connector, the
unassigned J2 battery-flex contacts, and the exact-BOM/vendor review gates remain.
Gerbers and drills remain intentionally withheld.
