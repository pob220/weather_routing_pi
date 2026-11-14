# xWeatherRouting 2.1.3

Consecutive waypoints at the same position now produce a setup error naming both
waypoints and the leg number. The complete sequence is checked before creating
legs, computing a saved sequence, or starting departure optimisation. Individually
computed saved legs use the same check. No routing engine, search budget or
weather constraint has changed.

The Plan passage Route card and readiness checks show the offending pair and an
**Edit route waypoints** action. The editor selects the overlapping point and
allows an explicitly saved position change through OpenCPN's waypoint API. It
preserves route and waypoint identity and reads fresh waypoint metadata before
saving. A shared waypoint moves in every route that uses it. **Show on chart**
opens the location for removal with OpenCPN's route-editing controls; no waypoint
is removed or moved automatically. After removing or reordering points, recreate
the Weather Routing legs from the edited OpenCPN route.

Only consecutive numerical coincidence is rejected (angular tolerance 1e-10
degrees, approximately 0.01 mm). Nearby distinct waypoints and legitimate return
passages such as A to B to A remain valid. Longitude wrapping and the poles are
handled consistently. Multi-leg readiness checks each leg rather than rejecting
a passage just because its overall start and finish are the same.

Version 1 remains the maintenance line. Version 2 preferences and data remain
separate. The Windows review ZIP includes its matching portable OpenCPN host.
