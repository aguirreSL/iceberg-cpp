# Test report

`build_report.py` runs every test executable of a build once and writes one
self-contained page, `out/index.html` (any browser, no network): the result of every
test, what it checks (the `//` comment right above its `TEST` line), its failure
output, and every measure the tests record (property `measure:<label>` with value
`"<value> <limit> <unit>"`) against its limit.

    cmake --build build
    python3 tests/report/build_report.py [build] [--out tests/report/out]

Python 3, standard library only. It also writes `out/data.json`, `out/xml/` (the
GoogleTest XML) and `out/logs/`. Exit code 1 when a test failed or an executable crashed
or wrote no XML. Without the MATLAB reference data (`scripts/get_fixtures.sh`) the
comparison tests skip, and the page says so at the top. The page follows the report of
Iceberg MATLAB (its `tests/report/`).
