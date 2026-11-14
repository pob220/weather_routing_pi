# xWeatherRouting 2.1.0 — UI review changes

This records the initial Windows trial. Later local weather fixes are described
in [Weather follow-up](WEATHER-FOLLOWUP.md); the trial ZIP is pinned to its
qualified source commit and is not silently replaced.

This is a UI/UX refinement of 2.0.0. Routing engines, scenario parsing,
calculation defaults and stored vessel preferences are unchanged. Version 1
remains the stable catalogue line. Existing chart context-menu and multi-leg
workflows are retained.

| Idea in Rick's notes | What 2.1 does |
| --- | --- |
| Consistent field descriptions and units | Shares UI descriptions for wind/wave limits, sailing angles, time steps, motoring, charted depth, performance factors and departure workers. Existing controls gain contextual help, including stored versus displayed units. |
| Explicit defaults and zero meanings | Shows disabled wind/wave limits as **Off** in the passage summary. Explains **0 = Auto** for departure workers, percentages versus multipliers, and the illustrative boat's starting settings. Saved values are retained. |
| Dependency-aware controls and validation | Uses the existing enabled/disabled controls. The readiness screen explains a zero motor speed when motoring is enabled and an unavailable chart-depth host before calculation. Switching a feature off retains its stored settings. No new headless restrictions are introduced. |
| Diagnostics with a useful next step | The result card displays an existing blocking reason, its detail and suggested action. A review button opens the relevant setup or report. Multi-leg passages identify the failed leg; cancellation has its own message. Unknown causes remain unknown. Technical counters stay in the detailed report. |
| Direct relationship to the headless model | Documents the existing stored units, inheritance and source behaviour. Corrects obsolete claims that version 2 requires an external GRIB plugin. It does not create a second JSON schema. |

The proposed three-layer Advanced tab, user experience levels, duplicate
Resources screen, one-widget-per-JSON-field layout, speculative defaults and
new engine validation are not adopted. **Plan passage**, **Passage settings**,
**Routing controls** and **Routes & analysis** remain the task-based structure.
Route agreement is not presented as forecast confidence or a navigation guarantee.

## Suggested message to Rick

Rick, I've used a few useful principles from your notes in xWeatherRouting
2.1.0: consistent setting descriptions and units; clearer Off/Auto meanings;
prerequisite messages that account for related settings; and a result card
showing the existing failure reason and a relevant next action.

I've kept Route / Boat / Weather as the main workflow, with Passage settings,
Routing controls and Routes & analysis, rather than adding experience levels
or a large JSON-shaped Advanced tab. This is a UI pass: the routing engines,
headless behaviour, calculation defaults and saved vessel settings are unchanged.

Download the Windows ZIP, extract it and run Start-Weather-Routing.cmd. No
compiling or installation is needed; it includes a separate portable OpenCPN.
Please try the example passage, then tell me which messages or steps still
feel unclear or take too many clicks.

https://github.com/pob220/xweather_routing_pi/releases/tag/windows-ui-trial-2.1.0
