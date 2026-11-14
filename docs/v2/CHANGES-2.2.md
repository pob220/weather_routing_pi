# 2.2.4 — automatic adaptive settings

* New passages default to automatic numerical settings and remember the user's
  explicit choice. Existing routes keep their manual inputs until enabled.
* Managed controls are greyed out; switching off restores the exact saved values.
* Route/boat/weather preflight informs Quick and Quick2 inputs. Buffered local
  current/geometry regions favour hourly stepping while engine-local adaptation
  stays active. Standard gains a bounded recovery reserve where appropriate.
* Route-aware work admission checks memory headroom alongside shared cache and
  concurrent-route guards. Count limits remain distinct from actual memory errors.
* Auto stops at the first accepted validated result, including Quick2; All
  compares selected engines and admitted alternatives.
* Original solver sources are preserved; the two adaptive variants are separate.
* Includes 2.2.3 forecast-provider backoff and incorporated JPEG2000 reader fixes,
  plus the 2.2.2 date/time controls and clearer allowance wording.

See [the policy, evidence and limits](ADAPTIVE-SETTINGS.md).

# xWeatherRouting 2.2.3 (in development)

* Share the production 1.36.2 weather-service fix: retain confirmed missing
  timeline frames in a separate bounded cache allowance, so long passages and
  engine fallbacks do not evict them merely to retain forecast grids.
* Distinguish a provider that does not reply from a confirmed missing frame.
  Retry an unresponsive service with a 1–30 second backoff, resetting it on
  an actual reply or a new computation batch. Existing cached forecasts remain
  usable; permitted Climatology fallback no longer waits on every timestamp.
* Report negative cache hits, unanswered requests, backoff skips and cumulative
  request waiting alongside the existing cache and eviction counters.

* Fix the incorporated reader's JPEG2000/JasPer initialization abort. Own and
  serialize decoder initialization, decoding and cleanup, including worker
  calls; apply a bounded decoder memory allowance. Add a real Farallon fixture
  with 1,260 independently decoded wind/wave values and missing masks, plus
  repeated concurrent loads.
* Headless external-provider setup no longer queries an absent UI timeline
  after a failed/unsupported file load. Dated weather requests and interactive
  timeline synchronization are unchanged. Set `WR_HEADLESS_GRIB_PROVIDER` to
  `external` to test either GRIB or xGRIB; the default remains `internal`.

The common cache/broker and headless bridge corrections also ship in production 1.36.2. Routing
algorithms, safety checks, sampling defaults and engine allowances are unchanged.

# xWeatherRouting 2.2.2 (in development)

* Distinguish search-history/search-memory/time allowances from failed memory
  allocations in summaries and individual engine results. Keep raw reasons and
  statuses; mixed or unknown resource stops remain neutral.
* Make successful fallback explicit when other engine attempts stop.
* Replace typed ISO/Zulu timestamps in passage setup and weather requests with
  calendar/clock controls, an explicit time-zone choice and Now. Keep UTC
  instants internally and preserve separate departure/arrival edits.
* Rename the progress action to **Stop exploration - keep results**.

The public Windows trial remains 2.2.1 until 2.2.2 is qualified and published.
Automatic/adaptive settings are being designed separately; production engines,
resource allowances and saved manual search settings are unchanged in this pass.
Two standalone input-policy prototypes have completed the full 253-case corpus
across all six engines. Neither is qualified as a new default; see
[the screening results](AUTOMATIC-INPUT-SCREENING-2.2.2.md).

# xWeatherRouting 2.2.1

* Explain disabled calculation with **Cannot calculate**, the exact blocking
  setting, a blocker count and an action beside it. Keep warnings separate.
* Offer **Turn off Draw exclusions for this passage** when a saved passage
  requires an unavailable Draw provider. Apply to all linked legs, preserving
  land avoidance, other passages and saved preferences. Passage checks offer
  the same action, with blockers listed before warnings.
* Mark Draw exclusions optional and show provider availability in Passage
  settings. Fresh defaults remain Draw off, land avoidance on, Auto with every
  engine checked; explicit saved choices remain intact.
* Expose **Try example passage** when the route is unset. The example uses
  bundled boat/Climatology data and clears inherited Draw/depth requirements.
* Clarify that unavailable Climatology fallback is a warning when forecast
  wind exists at departure; later coverage is still checked during routing.

Production engines and resource policies remain unchanged. Native exclusion
area creation/editing is outside this patch; Draw integration remains optional.

# xWeatherRouting 2.2.0

This release focuses on the planning interface and diagnostic records. All
production engine sources, dispatch policies, resource policies, vessel inputs
and solver defaults are unchanged from 2.1.3. Experimental search
work is confined to an independently built executable under `tools/engine-lab`.

* New passages use Auto with all six engines checked for both Auto and All.
  Saved mode and checkbox choices carry forward to the next route and survive
  restart. Existing routes keep their own settings.
* Replace lengthy always-visible configuration notes with section Help buttons,
  short tooltips and F1 help. Help is available offline by click/tap and keyboard.
  Active readiness problems and current safety status remain visible.
* Make both desktop configuration tabs scrollable and responsive to narrow
  windows. Preserve settings and planner geometry, including secondary monitors;
  clamp windows back onto an available display after monitor changes.
* Provide an explicit wind/wave reset with a preview and Cancel. It preserves
  route, boat, weather-source choices, engine settings and chart-safety settings.
  The existing search-preset reset has a separate, explicit label.
* Remove the redundant collapsed pane inside Routes and analysis. Add an
  Analysis tools menu linking to the existing route table, reports, statistics,
  plots and run diagnostics.
* Retain bounded run records for the session: actual input snapshots, observed
  stage transitions and final engine diagnostic reports, including stopped runs.
  Finished elapsed time remains fixed. Copy or save records for comparisons.
  Interface-observed stage times are explicitly distinguished from solver times.

No new engine is registered, no heading or time-step defaults change, and no
solver failure is reclassified. The standalone Atlas policy experiment documents
its route-quality tradeoff rather than changing the production search.
