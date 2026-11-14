# Rick's reports and 2.2 scope

Reports reviewed on 2026-11-13, including their attached screenshots and logs:
[#27](https://github.com/pob220/xweather_routing_pi/issues/27),
[#28](https://github.com/pob220/xweather_routing_pi/issues/28),
[#29](https://github.com/pob220/xweather_routing_pi/issues/29),
[#31](https://github.com/pob220/xweather_routing_pi/issues/31),
[#32](https://github.com/pob220/xweather_routing_pi/issues/32),
[#33](https://github.com/pob220/xweather_routing_pi/issues/33).

| Report | Supported finding | 2.2 response |
|---|---|---|
| 27 | Explanations dominate settings; sizing is awkward; reset scope is unclear. Geometry persistence already exists but later Fit/SetSize calls undermine it. | Context help, scrolling/responsive layout, geometry restoration, explicit wind/wave reset alongside the existing search reset. |
| 28/29 | Existing tools need clearer access; requested workflows overlap features already present. | Direct Analysis tools navigation and retained run records. No speculative eleven-workspace redesign, cloud sync or unsubstantiated confidence score. |
| 31 | Screenshot shows a large empty analysis page with its inner Weather Routing pane collapsed. | Remove the redundant wrapper, retain the complete existing route table and operations. |
| 32 | Quick/Quick2 hit work limits; Standard exhausts 600,000 states and reports wind_forecast_required; Professional is cancelled after about 22m43s, reaching 13.037 NM from the target. The log has reverse_reachability=0. | Preserve diagnostics for input-matched comparisons. Investigate possible stale failure-reason masking separately. No engine edits or claim that the passage is infeasible. |
| 33 | One attempt is cancelled after about 77s; another completes and validates with Standard in about 6m51s after Quick/Quick2 work limits. Its delivered wind is Climatology. Later screenshots change several settings together. | Treat slow routing as evidence to investigate. Do not adopt the proposed propagation algorithm or coarse defaults on the strength of this report. Benchmark separate alternatives with identical inputs and independent replay. |

Atlas already uses direct shooting and coarse pilot guidance; Auto deliberately
starts engines independently with fresh allowances. The reports do not establish
that engines forget a valid route or that broad initial fans alone explain slow
runs. Rick's suggested algorithm is a hypothesis, not the implementation brief.

The consecutive coincident-waypoint hotfix from 2.1.3 remains in place, including
route-card/readiness feedback and route editing. Return passages and merely
nearby waypoints remain legitimate. None of these reports justifies disabling
land/depth safety or silently raising wind/wave limits to obtain a result.

Engine investigations remain read-only in the product. The first standalone lab
uses an existing Atlas first-valid policy and demonstrates a speed/quality
tradeoff; it does not reproduce Rick's original inputs or establish a production
fix. See `tools/engine-lab/README.md`.
