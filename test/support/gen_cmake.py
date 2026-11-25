#!/usr/bin/env python3
"""Generate test/CMakeLists.txt from the native test scripts.

The bash scripts in test/common/ are the source of truth for how each native
test is built. This tool runs every script with fake `c++`/`cc` compilers on
PATH that only record their arguments, then turns the recorded commands into
one CMake target per test executable, so the same suite builds with CMake and
runs with ctest on macOS, Linux and Windows.

Run it after adding or changing a test script:

    python3 test/support/gen_cmake.py

Apple-only decoder sources and frameworks found in the recorded (macOS)
commands are replaced with per-platform equivalents.
"""

import os
import shlex
import subprocess
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TEST_DIRS = ["test/common"]
SRC_EXT = (".c", ".cc", ".cpp", ".mm", ".m")
SEP = "\x1f"

SHIM = r"""#!/bin/bash
{ printf '%s' "$(basename "$0")"; for a in "$@"; do printf '\x1f%s' "$a"; done; printf '\n'; } >> "$CAPTURE_FILE"
out=""; prev=""; stdin_src=0
for a in "$@"; do [ "$prev" == "-o" ] && out="$a"; [ "$a" == "-" ] && stdin_src=1; prev="$a"; done
if [ "$stdin_src" == "1" ]; then { printf 'STDIN%s' "$SEP_CHAR"; cat | base64; } >> "$CAPTURE_FILE"; fi
if [ -n "$out" ] && [ "$out" != "/dev/null" ]; then
  case "$out" in *.o) : > "$out" ;; *) printf '#!/bin/bash\n{ printf "RUN%%s%%s" "\x1f" "$(basename "$0")"; for a in "$@"; do printf "\x1f%%s" "$a"; done; printf "\\n"; } >> "%s"\nexit 0\n' "$CAPTURE_FILE" > "$out"; chmod +x "$out" ;; esac
fi
exit 0
"""

# Apple source -> CMake variable holding the current platform's equivalent.
PLATFORM_SOURCES = {
    "src/native_decoder/os_decoder_apple.mm": "${SOLOUD_OS_DECODER_SOURCE}",
    "src/audiobuffer/aac_stream_decoder_apple.mm": "${SOLOUD_AAC_DECODER_SOURCE}",
    "src/soloud_miniaudio_objc.mm": "${SOLOUD_APPLE_ONLY_SOURCES}",
}
# Tests whose script chains several runs through temporary files, or builds
# against the VST3 SDK outside the repository: ctest runs the script itself
# (Unix only) instead of building a target.
SCRIPT_ONLY = {"pull_mp3_seek_test", "simd_fallback_test", "vst3_param_changes_test"}
# Tests that run each case in a child process (fork, waitpid): not built on
# Windows.
POSIX_ONLY = {"bindings_null_args_test", "out_of_memory_test"}
XIPH_LIBS = {"ogg", "vorbis", "vorbisenc", "vorbisfile", "opus", "FLAC"}


def capture(script):
    with tempfile.TemporaryDirectory() as tmp:
        shim_dir = os.path.join(tmp, "shim")
        os.mkdir(shim_dir)
        shim = os.path.join(shim_dir, "c++")
        with open(shim, "w") as f:
            f.write(SHIM)
        os.chmod(shim, 0o755)
        for name in ("cc", "clang", "clang++", "g++", "gcc"):
            os.symlink(shim, os.path.join(shim_dir, name))
        cap = os.path.join(tmp, "capture.txt")
        open(cap, "w").close()
        env = dict(os.environ, CAPTURE_FILE=cap, SEP_CHAR=SEP, TMPDIR=tmp,
                   PATH=shim_dir + os.pathsep + os.environ["PATH"])
        subprocess.run(["bash", script], cwd=REPO, env=env, timeout=300,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        lines = open(cap, encoding="utf-8", errors="replace").read().splitlines()
        tmp = os.path.realpath(tmp) if False else tmp
    cmds, runs, i = [], {}, 0
    while i < len(lines):
        if lines[i].startswith("RUN" + SEP):
            parts = lines[i].split(SEP)[1:]
            runs[parts[0]] = [a for a in parts[1:] if not a.startswith(tmp)]
            i += 1
            continue
        argv = lines[i].split(SEP)[1:]
        stdin = None
        if i + 1 < len(lines) and lines[i + 1].startswith("STDIN" + SEP):
            import base64
            b64 = [lines[i + 1].split(SEP, 1)[1]]
            i += 2
            while i < len(lines) and SEP not in lines[i]:
                b64.append(lines[i])
                i += 1
            stdin = base64.b64decode("".join(b64)).decode()
        else:
            i += 1
        cmds.append((argv, stdin))
    return cmds, runs


def parse(argv):
    r = dict(out=None, compile_only=False, sources=[], objects=[], defines=[],
             includes=[], libdirs=[], libs=[], frameworks=[], options=[], lang=None)
    it = iter(range(len(argv)))
    k = 0
    while k < len(argv):
        a = argv[k]
        nxt = argv[k + 1] if k + 1 < len(argv) else None
        if a == "-o":
            r["out"] = nxt; k += 2; continue
        if a == "-c":
            r["compile_only"] = True
        elif a == "-x":
            r["lang"] = nxt; k += 2; continue
        elif a == "-I":
            r["includes"].append(nxt); k += 2; continue
        elif a.startswith("-I"):
            r["includes"].append(a[2:])
        elif a == "-L":
            r["libdirs"].append(nxt); k += 2; continue
        elif a.startswith("-L"):
            r["libdirs"].append(a[2:])
        elif a.startswith("-D"):
            r["defines"].append(a[2:])
        elif a.startswith("-l"):
            r["libs"].append(a[2:])
        elif a == "-framework":
            r["frameworks"].append(nxt); k += 2; continue
        elif a == "-":
            r["sources"].append("-")
        elif a.endswith(".o"):
            r["objects"].append(a)
        elif a.endswith(SRC_EXT) and not a.startswith("-"):
            r["sources"].append(a)
        elif a.startswith(("-O", "-g", "-w", "-W", "-std=", "-fsanitize",
                           "-fno-omit-frame-pointer", "-pthread")):
            pass
        elif a.startswith("-"):
            r["options"].append(a)
        k += 1
    return r


def cm_path(p):
    if p.startswith(("/opt/homebrew", "/usr/local")):
        return None  # Xiph prefix, handled by XIPH_* variables
    if os.path.isabs(p):
        return p
    return "${REPO_ROOT}/" + p


def emit_script_test(name, test_dir):
    script = os.path.basename(test_dir) + "/" + name + ".sh"
    return [
        "if(NOT WIN32)",
        f"  add_test(NAME {name} COMMAND bash ${{REPO_ROOT}}/test/{script} WORKING_DIRECTORY ${{REPO_ROOT}})",
        f"  set_tests_properties({name} PROPERTIES LABELS \"{os.path.basename(test_dir)};script\" SKIP_REGULAR_EXPRESSION \"^SKIPPED: ;\\nSKIPPED: \" TIMEOUT 900)",
        "endif()",
    ]


def emit_target(name, link, objects, test_dir, run_args):
    base = name.split("_scalar")[0]
    if base in SCRIPT_ONLY:
        return [] if name.endswith("_scalar") else emit_script_test(name, test_dir)
    out = []
    srcs = []
    platform_vars = []
    apple_only = False
    for s in link["sources"]:
        if s in PLATFORM_SOURCES:
            platform_vars.append(PLATFORM_SOURCES[s])
        elif s.endswith((".mm", ".m")):
            apple_only = True
            srcs.append(cm_path(s))
        else:
            srcs.append(cm_path(s))
    for s in sorted(set(platform_vars)):
        srcs.append(s)
    needs_xiph = bool(XIPH_LIBS & set(link["libs"]))
    for o in objects.values():
        if XIPH_LIBS & set(o["libs"]):
            needs_xiph = True
    conds = []
    if apple_only or name.endswith("_apple"):
        conds.append("APPLE")
    if needs_xiph:
        conds.append("SOLOUD_XIPH_FOUND")
    if name in POSIX_ONLY:
        conds.append("NOT WIN32")
    ind = "  " if conds else ""
    if conds:
        out.append(f"if({' AND '.join(conds)})")
    for oname, o in objects.items():
        osrc = o["generated"] if o.get("generated") else cm_path(o["sources"][0])
        out.append(f"{ind}add_library({oname} OBJECT {osrc})")
        out += target_props(oname, o, ind)
        srcs.append(f"$<TARGET_OBJECTS:{oname}>")
    out.append(f"{ind}add_executable({name}")
    for s in srcs:
        out.append(f"{ind}  {s}")
    out.append(f"{ind})")
    out += target_props(name, link, ind)
    libs = []
    for l in link["libs"]:
        if l == "dl":
            libs.append("${CMAKE_DL_LIBS}")
        elif l == "m":
            libs.append("${SOLOUD_M_LIB}")
        elif l == "pthread":
            continue
        elif l in XIPH_LIBS:
            libs.append(f"${{XIPH_{l.upper()}_LIB}}")
        else:
            libs.append(l)
    libs.append("Threads::Threads")
    out.append(f"{ind}target_link_libraries({name} PRIVATE {' '.join(libs)} ${{SOLOUD_PLATFORM_LIBS}})")
    label = os.path.basename(test_dir)
    args = " ".join(shlex.quote(a) for a in run_args if not a.startswith("/"))
    out.append(f"{ind}add_test(NAME {name} COMMAND {name} {args} WORKING_DIRECTORY ${{REPO_ROOT}})".replace("  ", " "))
    out.append(f"{ind}set_tests_properties({name} PROPERTIES LABELS \"{label}\" "
               f"SKIP_REGULAR_EXPRESSION \"^SKIPPED: ;\\nSKIPPED: \" TIMEOUT 900)")
    if conds:
        out.append("endif()")
    return out


def target_props(name, r, ind):
    out = []
    if r["defines"]:
        out.append(f"{ind}target_compile_definitions({name} PRIVATE {' '.join(r['defines'])})")
    incs = []
    for i in r["includes"]:
        p = cm_path(i)
        incs.append(p if p else "${XIPH_INCLUDE_DIR}")
    if incs:
        out.append(f"{ind}target_include_directories({name} PRIVATE {' '.join(dict.fromkeys(incs))})")
    opts = [o for o in r["options"] if o not in ("-x",)]
    if opts:
        out.append(f"{ind}if(NOT MSVC)")
        out.append(f"{ind}  target_compile_options({name} PRIVATE {' '.join(opts)})")
        out.append(f"{ind}endif()")
    return out


def main():
    body = []
    for d in TEST_DIRS:
        for script in sorted(os.listdir(os.path.join(REPO, d))):
            if not script.endswith("_test.sh"):
                continue
            path = os.path.join(d, script)
            cmds, runs = capture(path)
            objects_by_out = {}
            links = []
            for argv, stdin in cmds:
                r = parse(argv)
                if not r["out"] or r["out"] == "/dev/null":
                    continue
                if stdin is not None:
                    r["stdin"] = stdin
                if r["compile_only"]:
                    objects_by_out[r["out"]] = r
                else:
                    links.append(r)
            if not links:
                body.append(f"# {path}: no executable found, not part of the CMake suite")
                continue
            body.append(f"\n# ---- {path}")
            for link in links:
                name = os.path.basename(link["out"])
                objs = {}
                for n, o in enumerate(link["objects"]):
                    if o not in objects_by_out:
                        continue
                    ob = dict(objects_by_out[o])
                    oname = f"{name}_obj{n}"
                    if ob.get("stdin") is not None:
                        gen = f"${{CMAKE_CURRENT_BINARY_DIR}}/{oname}.cpp"
                        content = ob["stdin"].replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
                        body.append(f'file(WRITE "{gen}" "{content}")')
                        ob["generated"] = gen
                    objs[oname] = ob
                body += emit_target(name, link, objs, d, runs.get(name, []))

    header = open(os.path.join(os.path.dirname(__file__), "CMakeLists.header.cmake")).read()
    FOOTER = """
# Sanitizer runtime options for every test (see the header).
if(SOLOUD_TEST_ENV)
  get_property(_soloud_tests DIRECTORY PROPERTY TESTS)
  set_tests_properties(${_soloud_tests} PROPERTIES ENVIRONMENT "${SOLOUD_TEST_ENV}")
endif()
"""
    with open(os.path.join(REPO, "test", "CMakeLists.txt"), "w") as f:
        f.write(header)
        f.write("\n# ==== Generated by test/support/gen_cmake.py, do not edit below ====\n")
        f.write("\n".join(body) + "\n")
        f.write(FOOTER)
    print("wrote test/CMakeLists.txt")


if __name__ == "__main__":
    sys.exit(main())
