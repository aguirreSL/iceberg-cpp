#!/usr/bin/env python3
"""Runs the iceberg-cpp tests and writes one self-contained page with the result.

    python3 tests/report/build_report.py [BUILD_DIR] [--out OUT_DIR]

From the repository root, after a build (defaults: BUILD_DIR = build, OUT_DIR =
tests/report/out). Python 3 standard library only.

Every test executable BUILD_DIR/tests/test_* runs once, with GoogleTest's XML
output in OUT_DIR/xml/<name>.xml and its stdout and stderr in OUT_DIR/logs/<name>.log.
The page, OUT_DIR/index.html (any browser, no server, no network), is template.html
with the run injected; OUT_DIR/data.json holds the same data. It shows the result
of every test, its failure output, what it checks (the // comment block right above
its TEST line), what each file tests (the first comment block of the file), and
every measure the tests recorded: a property `measure:<label>` whose value is
"<value> <limit> <unit>".

Exit code 0 when no test failed, 1 otherwise. A skipped or disabled test does not
fail the run; an executable that crashes or writes no XML does.
"""

import argparse
import datetime
import json
import os
import platform
import re
import signal
import subprocess
import sys
import time
import xml.parsers.expat
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent

# What the comparisons are against (scripts/get_fixtures.sh, README).
REFERENCE = {
    "what": "Iceberg MATLAB v1.0.0",
    "doi": "10.5281/zenodo.23543831",
    "url": "https://doi.org/10.5281/zenodo.23543831",
    "script": "scripts/get_fixtures.sh",
}
# The message of ICEBERG_SKIP_IF_NO_FIXTURES() (tests/test_helpers.h).
NO_FIXTURES = "Fixtures dir missing"
# A page that embeds every log in full could grow without bound; each log keeps
# its head and its tail (the full file stays in OUT_DIR/logs).
LOG_HEAD, LOG_TAIL = 20_000, 180_000
WARN_RATIO = 0.5


# ---------------------------------------------------------------- running

def find_executables(build):
    # Single-config generators put them in tests/, multi-config ones in tests/<Config>/.
    pat = re.compile(r"^test_\w+(\.exe)?$")
    found = {}
    for p in sorted(build.glob("tests/test_*")) + sorted(build.glob("tests/*/test_*")):
        name = p.name[:-4] if p.name.endswith(".exe") else p.name
        if pat.match(p.name) and p.is_file() and os.access(p, os.X_OK) and name not in found:
            found[name] = p
    return found


def run_executable(exe, xml_path, log_path):
    if xml_path.exists():
        xml_path.unlink()   # a crash must not leave the previous run's XML behind
    env = dict(os.environ, GTEST_COLOR="no")
    t0 = time.monotonic()
    with open(log_path, "wb") as log:
        try:
            rc = subprocess.run([str(exe), f"--gtest_output=xml:{xml_path}"], cwd=exe.parent,
                                stdout=log, stderr=subprocess.STDOUT, env=env).returncode
        except OSError as e:
            log.write(f"could not run {exe}: {e}\n".encode())
            rc = None
    return rc, time.monotonic() - t0


def exit_text(rc):
    if rc is None:
        return "could not be started"
    if rc < 0:
        try:
            return f"killed by signal {-rc} ({signal.Signals(-rc).name})"
        except ValueError:
            return f"killed by signal {-rc}"
    return f"exit code {rc}"


def read_log(path):
    try:
        text = path.read_bytes().decode("utf-8", "replace")
    except OSError:
        return ""
    if len(text) > LOG_HEAD + LOG_TAIL:
        cut = len(text) - LOG_HEAD - LOG_TAIL
        text = (text[:LOG_HEAD] + f"\n\n[... {cut} characters left out here; the full log is "
                f"logs/{path.name} ...]\n\n" + text[-LOG_TAIL:])
    return text


# ---------------------------------------------------------------- GoogleTest XML

class Node:
    __slots__ = ("tag", "attrs", "children", "text")

    def __init__(self, tag, attrs):
        self.tag, self.attrs, self.children, self.text = tag, attrs, [], ""


def parse_xml(path):
    """The XML as a tree of Nodes. expat without namespace processing, so that an
    attribute named `measure:...` (older GoogleTest writes test properties as
    attributes) is a plain name and not an unbound prefix."""
    parser = xml.parsers.expat.ParserCreate()
    parser.buffer_text = True
    stack, root = [], []

    def start(tag, attrs):
        n = Node(tag, attrs)
        (stack[-1].children if stack else root).append(n)
        stack.append(n)

    def end(_tag):
        stack.pop()

    def data(s):
        if stack:
            stack[-1].text += s

    parser.StartElementHandler, parser.EndElementHandler = start, end
    parser.CharacterDataHandler = data
    parser.Parse(path.read_bytes(), True)
    return root[0]


def parse_measure(test_id, file, label, value):
    """One `measure:<label>` property: "<value> <limit> <unit>"."""
    rec = {"test": test_id, "file": file, "label": label, "value": None, "limit": None,
           "unit": "", "ratio": None, "state": "unread", "raw": value}
    parts = value.split()
    try:
        v, lim = float(parts[0]), float(parts[1])
    except (IndexError, ValueError):
        return rec
    rec["unit"] = " ".join(parts[2:])
    finite = lambda x: x if x == x and abs(x) != float("inf") else None   # JSON has no inf / nan
    rec["value"], rec["limit"] = finite(v), finite(lim)
    if v != v or lim != lim or lim < 0:
        rec["state"] = "over"   # a NaN error, or a negative limit, cannot pass
        return rec
    # Signed: the tests assert value < limit, and some measures are negative when
    # all is well (the largest rise of a curve that only falls).
    if lim == 0:
        ratio = 0.0 if v <= 0 else float("inf")
    else:
        ratio = v / lim
    rec["ratio"] = finite(ratio)
    rec["state"] = "over" if ratio > 1 else "warn" if ratio > WARN_RATIO else "ok"
    return rec


def read_results(xml_path, file):
    """The test cases of one executable's XML and the measures they recorded."""
    tests, records = [], []
    root = parse_xml(xml_path)
    for suite in (n for n in root.children if n.tag == "testsuite"):
        for case in (n for n in suite.children if n.tag == "testcase"):
            a = case.attrs
            name = f"{a.get('classname', suite.attrs.get('name', ''))}.{a.get('name', '')}"
            test_id = f"{file}/{name}"
            failures = [c for c in case.children if c.tag == "failure"]
            skipped = [c for c in case.children if c.tag == "skipped"]
            out = "\n\n".join((c.text or c.attrs.get("message", "")).strip() for c in failures)
            skip = "\n".join((c.text or c.attrs.get("message", "")).strip() for c in skipped)
            if failures:
                status = "failed"
            elif a.get("status") == "notrun" or a.get("result") == "suppressed":
                status = "disabled"
            elif skipped or a.get("result") == "skipped":
                status = "noref" if NO_FIXTURES in skip else "skipped"
            else:
                status = "passed"
            try:
                t = float(a.get("time", "0") or 0)
            except ValueError:
                t = 0.0
            src = Path(a["file"]).name + ":" + a["line"] if a.get("file") and a.get("line") else ""
            tests.append({"id": test_id, "name": name, "file": file, "status": status, "time": t,
                          "out": out, "skip": skip, "source": src, "doc": ""})
            props = [(p.attrs.get("name", ""), p.attrs.get("value", ""))
                     for c in case.children if c.tag == "properties"
                     for p in c.children if p.tag == "property"]
            props += list(a.items())   # older GoogleTest: properties as attributes
            for key, value in props:
                if key.startswith("measure:"):
                    records.append(parse_measure(test_id, file, key[len("measure:"):].strip(), value))
    return tests, records


# ---------------------------------------------------------------- sources

TEST_RE = re.compile(r"^[ \t]*(?:TEST|TEST_F|TEST_P|TYPED_TEST|TYPED_TEST_P)\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)",
                     re.M)
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([\w./]+)"')


def comment_text(lines):
    """A // block as prose: lines joined, an empty // line starts a paragraph, an
    indented line or a list item keeps its own line (a deeper-indented line under
    it continues it), banner rules (=== ---) go."""
    out, own = "", None   # own: indent of the last line kept on its own, None after prose
    for raw in lines:
        s = re.sub(r"^\s*//[/!]?", "", raw)
        s = s[1:] if s.startswith(" ") else s
        s = re.sub(r"^\s*[=\-]{3,}\s*", "", s)
        s = re.sub(r"\s*[=\-]{3,}\s*$", "", s)
        indent = len(s) - len(s.lstrip())
        item = re.match(r"(?:[-*]|\d+[.)])\s", s.strip())
        if not s.strip():
            out, own = out.rstrip() + ("\n\n" if out.strip() else ""), None
        elif own is not None and indent > own and not item:
            out = out.rstrip("\n") + " " + s.strip() + "\n"
        elif indent >= 2 or item:
            out = out + ("\n" if out and not out.endswith("\n") else "") + s.strip() + "\n"
            own = indent
        else:
            out += ("" if not out or out.endswith("\n") else " ") + s.strip()
            own = None
    return re.sub(r"\n{3,}", "\n\n", out).strip()


def is_comment(line):
    return line.strip().startswith("//")


def read_source(path):
    """What the file tests (its first comment block, above or below the includes),
    what each test checks ({"Suite.Name": text}) and the group (the src/ directory
    of the header it tests)."""
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return "", {}, "other"
    text = "\n".join(lines)
    docs = {}
    for m in TEST_RE.finditer(text):
        k = text.count("\n", 0, m.start())
        j = k
        while j > 0 and is_comment(lines[j - 1]):
            j -= 1
        if j < k:
            docs[f"{m.group(1)}.{m.group(2)}"] = comment_text(lines[j:k])
    header = ""
    k = 0
    while k < len(lines):
        s = lines[k].strip()
        if not s or s.startswith("#"):
            k += 1
        elif is_comment(lines[k]):
            j = k
            while j < len(lines) and is_comment(lines[j]):
                j += 1
            if not (j < len(lines) and TEST_RE.match(lines[j])):   # else it describes that test
                header = comment_text(lines[k:j])
            break
        else:
            break   # code before any comment: the file has no header
    # the group: the include named like the file (test_add.cpp -> native/add.h), else the first
    incs = [m.group(1) for m in map(INCLUDE_RE.match, lines) if m and "/" in m.group(1)]
    stem = path.stem[len("test_"):]
    own = [i for i in incs if Path(i).stem == stem]
    group = (own or incs or ["other/"])[0].split("/")[0]
    return header, docs, group


# ---------------------------------------------------------------- identity of the run

def cmake_cache(build):
    cache = {}
    try:
        for line in (build / "CMakeCache.txt").read_text(errors="replace").splitlines():
            m = re.match(r"^([A-Za-z_][\w.-]*):[A-Z]+=(.*)$", line)
            if m:
                cache[m.group(1)] = m.group(2)
    except OSError:
        return None
    return cache


def compiler(build, cache):
    """CMAKE_CXX_COMPILER_ID and _VERSION: from the cache if it holds them, else from
    the compiler file CMake writes next to it (CMakeFiles/<cmake version>/)."""
    cid, ver = cache.get("CMAKE_CXX_COMPILER_ID", ""), cache.get("CMAKE_CXX_COMPILER_VERSION", "")
    if not cid:
        files = sorted(build.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"), key=lambda p: p.stat().st_mtime)
        if files:
            text = files[-1].read_text(errors="replace")
            get = lambda k: (re.search(r'set\(%s "([^"]*)"\)' % k, text) or [None, ""])[1]
            cid, ver = get("CMAKE_CXX_COMPILER_ID"), get("CMAKE_CXX_COMPILER_VERSION")
    return " ".join(x for x in (cid, ver) if x)


def git(*args):
    try:
        return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def run_identity(build):
    cache = cmake_cache(build)
    commit = os.environ.get("GITHUB_SHA") or git("rev-parse", "HEAD")
    meta = {
        "date": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
        "commit": commit,
        "commit_from": "GITHUB_SHA" if os.environ.get("GITHUB_SHA") else "git",
        "branch": os.environ.get("GITHUB_REF_NAME") or git("rev-parse", "--abbrev-ref", "HEAD"),
        "dirty": bool(git("status", "--porcelain", "--untracked-files=no")),
        "platform": platform.platform(),
        "cores": os.cpu_count(),
        "python": platform.python_version(),
        "build_dir": str(build.resolve()),
        "cache": cache is not None,
        "compiler": "", "build_type": "", "generator": "", "version": "",
        "source_dir": "", "fixtures_dir": "", "fixtures": None, "require_fixtures": False,
        "reference": REFERENCE,
    }
    if cache is not None:
        fx = cache.get("ICEBERG_FIXTURES_DIR", "")
        meta.update({
            "compiler": compiler(build, cache),
            "build_type": cache.get("CMAKE_BUILD_TYPE", ""),
            "generator": cache.get("CMAKE_GENERATOR", ""),
            "version": cache.get("CMAKE_PROJECT_VERSION", ""),
            "source_dir": cache.get("CMAKE_HOME_DIRECTORY", ""),
            "fixtures_dir": os.path.normpath(fx) if fx else "",
            "fixtures": bool(fx) and Path(fx).is_dir(),
            "require_fixtures": cache.get("ICEBERG_REQUIRE_FIXTURES", "").upper() in ("ON", "1", "TRUE", "YES"),
        })
    return meta


# ---------------------------------------------------------------- the report

def summary_text(tests, records):
    """One line per test, then one per measure (the end of the raw log in the page)."""
    word = {"passed": "PASS", "failed": "FAIL", "skipped": "SKIP", "noref": "SKIP (no ref)",
            "disabled": "DISABLED"}
    lines = ["Results", "======="]
    lines += [f"{word[t['status']]:<14} {t['time']:8.3f} s  {t['id']}" for t in tests]
    lines += ["", "Measures", "========",
              f"{'state':<6} {'value/limit':<12} {'value':<13} {'limit':<13} {'unit':<8} label (test)"]
    fmt = lambda x: "-" if x is None else f"{x:.6e}"
    state = {"ok": "", "warn": "HALF", "over": "OVER", "unread": "?"}
    for r in records:
        lines.append(f"{state[r['state']]:<6} {fmt(r['ratio']):<12} {fmt(r['value']):<13} {fmt(r['limit']):<13} "
                     f"{r['unit'] or '-':<8} {r['label']} ({r['test']})")
    if not records:
        lines.append("(none recorded)")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description="Run the iceberg-cpp tests and write the HTML report.")
    ap.add_argument("build", nargs="?", default="build", help="CMake build directory (default: build)")
    ap.add_argument("--out", default="tests/report/out", help="output directory (default: tests/report/out)")
    args = ap.parse_args()
    # Absolute: each executable runs in its own folder.
    build, out = Path(args.build).resolve(), Path(args.out).resolve()
    exes = find_executables(build)
    if not exes:
        sys.exit(f"build_report: no test executable in {build / 'tests'}; build the project first.")
    (out / "xml").mkdir(parents=True, exist_ok=True)
    (out / "logs").mkdir(parents=True, exist_ok=True)
    meta = run_identity(build)

    tests, records, files = [], [], []
    for i, (name, exe) in enumerate(exes.items(), 1):
        xml_path, log_path = (out / "xml" / f"{name}.xml").resolve(), out / "logs" / f"{name}.log"
        print(f"[{i:2d}/{len(exes)}] {name} ...", end=" ", flush=True)
        rc, wall = run_executable(exe, xml_path, log_path)
        header, docs, group = read_source(ROOT / "tests" / f"{name}.cpp")
        ts, rs, problem = [], [], ""
        if not xml_path.is_file():
            problem = f"{name} wrote no XML ({exit_text(rc)})."
        else:
            try:
                ts, rs = read_results(xml_path, name)
            except (xml.parsers.expat.ExpatError, OSError, IndexError) as e:
                problem = f"{name}: its XML could not be read ({e}; {exit_text(rc)})."
        if not problem and rc != 0 and not any(t["status"] == "failed" for t in ts):
            problem = f"{name} ended with {exit_text(rc)}, with no failed test in its XML."
        for t in ts:
            t["doc"] = docs.get(t["name"], "")
        log = read_log(log_path)
        if problem:
            # the executable itself counts as a failed test, with the end of its log
            tail = "\n".join(log.rstrip().splitlines()[-60:])
            ts.append({"id": f"{name}/(executable)", "name": "(executable)", "file": name,
                       "status": "failed", "time": wall, "out": problem + "\n\n" + tail,
                       "skip": "", "source": "", "doc": "", "crash": True})
        tests += ts
        records += rs
        files.append({"file": name, "group": group, "doc": header, "rc": rc, "exit": exit_text(rc),
                      "wall": wall, "problem": problem, "log": log})
        n_fail = sum(t["status"] == "failed" for t in ts)
        print(f"{len(ts)} tests, {n_fail} failed, {wall:.1f} s" + (f"  [{problem}]" if problem else ""))

    data = {"meta": meta, "files": files, "tests": tests, "records": records,
            "summary": summary_text(tests, records)}
    (out / "data.json").write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
    js = json.dumps(data, ensure_ascii=False).replace("</", "<\\/").replace("<!--", "<\\!--")
    template = (HERE / "template.html").read_text(encoding="utf-8")
    if template.count("/*__DATA__*/") != 1:
        sys.exit("build_report: template.html must hold the /*__DATA__*/ marker once.")
    (out / "index.html").write_text(template.replace("/*__DATA__*/", f"window.REPORT = {js};"),
                                    encoding="utf-8")

    count = lambda s: sum(t["status"] == s for t in tests)
    state = lambda s: sum(r["state"] == s for r in records)
    ref = {True: "present", False: "ABSENT (the comparisons with MATLAB were skipped)",
           None: "unknown (no CMakeCache.txt)"}[meta["fixtures"]]
    print(f"report: {(out / 'index.html').resolve()}")
    print(f"tests: {len(tests)}, passed {count('passed')}, failed {count('failed')}, "
          f"skipped {count('skipped') + count('noref')} (no reference data {count('noref')}), "
          f"disabled {count('disabled')} | measures: {len(records)}, over the limit {state('over')}, "
          f"above half {state('warn')} | reference data: {ref}")
    # Every test should say what it checks (CONTRIBUTING.md); the page shows the comment.
    undocumented = [t["id"] for t in tests if not t.get("doc") and t["name"] != "(executable)"]
    if undocumented:
        print(f"tests without a comment above them ({len(undocumented)}): "
              + ", ".join(undocumented))
    return 1 if count("failed") else 0


if __name__ == "__main__":
    sys.exit(main())
