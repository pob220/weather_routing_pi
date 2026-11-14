# xWeatherRouting 2.1.2 qualification

This patch changes Unicode UI text, passage display names, weather preference
handling and optional OpenCPN Draw readiness/help. Routing engines, headless
scenario parsing and scientific GRIB provider code are unchanged. The previous
[2.1.1 qualification](VALIDATION-2.1.1.md) is retained as history.

The [2.1.2 review release](https://github.com/pob220/xweather_routing_pi/releases/tag/windows-ui-trial-2.1.2)
is published only after qualification and records the exact source, native
Windows run, ZIP checksum and test receipts. Qualification covers both pinned
Windows x64 hosts (wxWidgets 3.3.4 and 3.2.9), their test suites and packaged-helper
checks, and the portable launcher's behavior after relocation. The unchanged
scientific helper is reused only after its source and build inputs match the
qualified original.

Unicode regression checks exercise the passage-label helpers under Windows'
Western ANSI locale and read the actual planner window labels from the delivered
launcher. They reject the corrupt punctuation seen in the review screenshots.
Named passages and unnamed passages with identifier fallbacks are covered.

Linux checks also cover the weather preference and Draw readiness follow-ups,
and retain the 2.1.1 request-hour tests. Local test installation uses a separate
version 2 profile and preserves existing test data. The working OpenCPN and
Weather Routing 1.36 remain separate. These checks do not claim an authenticated
Copernicus download or qualification of macOS or Android.
