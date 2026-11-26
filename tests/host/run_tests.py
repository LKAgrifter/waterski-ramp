#!/usr/bin/env python3
"""
Host simulation tests for the ramp controller and remote firmware.

Builds the real .ino sketches (from ../../firmware) for a PC, with small stand-ins for the Arduino / Heltec
libraries (stubs/), then runs scenario tests against them: button presses, LoRa packets, limit switches,
a simulated ramp moving at the real speed, faults, power cycles...

This is NOT the ESP32 compiler and NOT real hardware. It catches logic bugs; it can't catch wiring, timing of
real relays, radio range or library-version problems. Always finish by flashing and testing the real boards.

Needs: Python 3 and g++ (Linux/macOS; on Windows use WSL or MSYS2).

    python3 run_tests.py            # everything (the games fuzz test takes a minute or two)
    python3 run_tests.py --quick    # skip the long games fuzz test
    python3 run_tests.py -k motion  # only tests whose name contains "motion"
    python3 run_tests.py --trace    # print a millisecond relay trace for manual UP/DOWN (local and remote)
    python3 run_tests.py -v         # show every check, not just failures
"""
import argparse, concurrent.futures as cf, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.normpath(os.path.join(HERE, '..', '..', 'firmware'))
BUILD = os.path.join(HERE, 'build')
STUBS = os.path.join(HERE, 'stubs')
TESTS = os.path.join(HERE, 'tests')

# Sketch variants: (name, sketch folder, main .ino name, {constant line to change: replacement})
VARIANTS = [
    ('ramp',       'Ramp_Control', 'Ramp_Control', {}),
    ('remote',     'Ramp_Remote',  'Ramp_Remote',  {}),
    ('ramp_cal',   'Ramp_Control', 'Ramp_Control', {'LADDER_CALIBRATION_MODE = false;': 'LADDER_CALIBRATION_MODE = true;'}),
    ('remote_cal', 'Ramp_Remote',  'Ramp_Remote',  {'LADDER_CALIBRATION_MODE = false;': 'LADDER_CALIBRATION_MODE = true;'}),
    ('ramp_long',  'Ramp_Control', 'Ramp_Control', {'MOTOR_STEP_TIMEOUT_MS = 100000;': 'MOTOR_STEP_TIMEOUT_MS = 200000;'}),
    ('ramp_sim',   'Ramp_Control', 'Ramp_Control', {'SIM_MODE = false;': 'SIM_MODE = true;'}),
    ('ramp_no_remote_manual', 'Ramp_Control', 'Ramp_Control', {'ALLOW_REMOTE_MANUAL = true;': 'ALLOW_REMOTE_MANUAL = false;'}),
]

# Tests: (name, test file, variant, extra -D defines, slow?)
TESTCASES = [
    ('ramp_logic',               'ramp_logic.cpp',              'ramp',       [], False),
    ('remote_logic',             'remote_logic.cpp',            'remote',     [], False),
    ('ramp_motion',              'ramp_motion.cpp',             'ramp',       ['ZONE_MM=8'], False),
    ('motion_overrun_too_long',  'motion_overrun_too_long.cpp', 'ramp',       ['ZONE_MM=1'], False),
    ('motion_missed_switch',     'motion_missed_switch.cpp',    'ramp_long',  ['ZONE_MM=8'], False),
    ('reversal_water',           'reversal_water.cpp',          'ramp',       ['CALMODE=0'], False),
    ('ladder_cal_mode_ramp',     'reversal_water.cpp',          'ramp_cal',   ['CALMODE=1'], False),
    ('pages_ramp',               'pages.cpp',                   'ramp',       [], False),
    ('pages_remote',             'pages.cpp',                   'remote',     ['REMOTE'], False),
    ('ladder_decode_ramp',       'ladder_decode.cpp',           'ramp',       [], False),
    ('ladder_decode_remote',     'ladder_decode.cpp',           'remote',     [], False),
    ('remote_ladder',            'remote_ladder.cpp',           'remote',     ['CALMODE=0'], False),
    ('remote_ladder_cal_mode',   'remote_ladder.cpp',           'remote_cal', ['CALMODE=1'], False),
    ('blank_heights_remote',     'blank_heights.cpp',           'remote',     ['REMOTE'], False),
    ('remote_id',                'remote_id.cpp',               'remote',     ['REMOTE'], False),
    ('remote_water_mode',        'remote_water_mode.cpp',       'remote',     ['REMOTE'], False),
    ('ramp_water_timer',         'ramp_water_timer.cpp',        'ramp',       [], False),
    ('bypass_buttons_ramp',      'bypass_buttons.cpp',          'ramp',       [], False),
    ('bypass_buttons_remote',    'bypass_buttons.cpp',          'remote',     ['REMOTE'], False),
    ('remote_manual_on_ramp',    'remote_manual_flag.cpp',      'ramp',       [], False),
    ('remote_manual_off_ramp',   'remote_manual_flag.cpp',      'ramp_no_remote_manual', ['OFF'], False),
    ('remote_manual_flag_remote','remote_manual_flag.cpp',      'remote',     ['REMOTE'], False),
    ('screen_awake_moving_ramp', 'screen_awake_moving.cpp',     'ramp',       [], False),
    ('screen_awake_moving_remote','screen_awake_moving.cpp',    'remote',     ['REMOTE'], False),
    ('radio_security_ramp',      'radio_security.cpp',          'ramp',       [], False),
    ('radio_security_remote',    'radio_security.cpp',          'remote',     ['REMOTE'], False),
    ('sim_mode',                 'sim_mode.cpp',                'ramp_sim',   ['SIM'], False),
    ('sim_mode_boot_no_sensor',  'sim_mode.cpp',                'ramp_sim',   ['SIM', 'NOSENSOR'], False),
    ('sim_mode_off',             'sim_mode.cpp',                'ramp',       [], False),
    ('games_ramp',               'games.cpp',                   'ramp',       ['RAMP_SIDE', 'DISPLAY_COST=22', 'VARIANT="\\"ramp\\""'], True),
    ('games_remote',             'games.cpp',                   'remote',     ['DISPLAY_COST=22', 'VARIANT="\\"remote\\""'], True),
]
for n in range(7):
    TESTCASES.append(('startup_case_%d' % n, 'startup.cpp', 'ramp', ['BC=%d' % n], False))
for n in range(3):
    TESTCASES.append(('blank_heights_ramp_%d' % n, 'blank_heights.cpp', 'ramp', ['BOOTCASE=%d' % n], False))

ANSI = sys.stdout.isatty() and os.name != 'nt'
def color(s, c): return ('\033[%sm%s\033[0m' % (c, s)) if ANSI else s


def amalgamate(sketch_dir, main):
    """Concatenate the tabs the way the Arduino IDE does (main tab first, then the rest alphabetically) and add
    function prototypes, using build_sketch.py."""
    out = os.path.join(BUILD, os.path.basename(sketch_dir) + '.cpp')
    subprocess.check_call([sys.executable, os.path.join(HERE, 'build_sketch.py'), sketch_dir, out, main])
    return out


def make_variants():
    os.makedirs(BUILD, exist_ok=True)
    srcs = {}
    for name, folder, main, edits in VARIANTS:
        d = os.path.join(BUILD, 'src_' + name, folder)
        if os.path.exists(os.path.dirname(d)):
            shutil.rmtree(os.path.dirname(d))
        shutil.copytree(os.path.join(FW, folder), d)
        for old, new in edits.items():
            p = os.path.join(d, main + '.ino')
            s = open(p).read()
            if s.count(old) != 1:
                sys.exit('Variant %s: could not find "%s" in %s.ino (was the constant renamed?)' % (name, old, main))
            open(p, 'w').write(s.replace(old, new))
        out = os.path.join(BUILD, name + '.cpp')
        subprocess.check_call([sys.executable, os.path.join(HERE, 'build_sketch.py'), d, out, main])
        srcs[name] = out
    return srcs


def sanitizer_flags():
    probe = os.path.join(BUILD, 'probe.cpp')
    open(probe, 'w').write('int main(){return 0;}\n')
    flags = ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined']
    r = subprocess.run(['g++', probe, '-o', probe + '.out'] + flags, capture_output=True)
    return flags if r.returncode == 0 else []


def warnings_check(srcs):
    """Compile both sketches with warnings on and report any warning that points into a .ino file."""
    problems = []
    for name in ('ramp', 'remote', 'ramp_sim'):
        r = subprocess.run(['g++', '-std=c++17', '-fsyntax-only', '-Wall', '-Wextra', '-I', STUBS, '-x', 'c++', srcs[name]],
                           capture_output=True, text=True)
        for line in r.stderr.splitlines():
            if '.ino' in line and ('warning' in line or 'error' in line):
                problems.append('%s: %s' % (name, line))
        if r.returncode != 0:
            problems.append('%s: does not compile:\n%s' % (name, r.stderr[-3000:]))
    return problems


def build_and_run(tc, srcs, san, verbose):
    name, test, variant, defines, _slow = tc
    exe = os.path.join(BUILD, 'bin_' + name)
    cmd = ['g++', '-std=c++17', '-g', '-O1', '-I', STUBS, '-I', BUILD] + san + \
          ['-DSRC="%s"' % srcs[variant].replace('\\', '/')] + ['-D' + d for d in defines] + \
          [os.path.join(TESTS, test), os.path.join(STUBS, 'impl.cpp'), '-o', exe]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        return name, False, 'BUILD FAILED\n' + r.stderr[-4000:]
    r = subprocess.run([exe], capture_output=True, text=True, timeout=1800)
    out = r.stdout + r.stderr
    ok = (r.returncode == 0) and ('FAIL' not in out) and ('runtime error' not in out) and ('ERROR: AddressSanitizer' not in out)
    shown = out if verbose else '\n'.join(l for l in out.splitlines() if 'FAIL' in l or 'runtime error' in l or 'Sanitizer' in l)
    return name, ok, shown


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--quick', action='store_true', help='skip the slow games fuzz tests')
    ap.add_argument('-k', default='', help='only run tests whose name contains this text')
    ap.add_argument('-v', '--verbose', action='store_true', help='show every check')
    ap.add_argument('--trace', action='store_true', help='print a relay trace for manual UP/DOWN and exit')
    ap.add_argument('-j', type=int, default=os.cpu_count() or 2, help='parallel jobs')
    a = ap.parse_args()

    if not shutil.which('g++'):
        sys.exit('g++ not found. Install it (Linux: build-essential, macOS: Xcode tools, Windows: WSL or MSYS2).')

    srcs = make_variants()
    san = sanitizer_flags()

    if a.trace:
        exe = os.path.join(BUILD, 'relay_trace')
        subprocess.check_call(['g++', '-std=c++17', '-g', '-I', STUBS, '-DSRC="%s"' % srcs['ramp'].replace('\\', '/'),
                               os.path.join(HERE, 'tools', 'relay_trace.cpp'), os.path.join(STUBS, 'impl.cpp'), '-o', exe])
        subprocess.call([exe])
        return

    failed = 0
    print('Checking both sketches (and the ramp simulation build) compile cleanly with warnings on...')
    probs = warnings_check(srcs)
    for p in probs:
        print('  ' + color(p, '31'))
    failed += bool(probs)

    g1 = open(os.path.join(FW, 'Ramp_Control', 'Games.ino')).read()
    g2 = open(os.path.join(FW, 'Ramp_Remote', 'Games.ino')).read()
    if g1 != g2:
        print(color('  Games.ino differs between Ramp_Control and Ramp_Remote - they must be identical copies.', '31'))
        failed += 1
    s1 = open(os.path.join(FW, 'Ramp_Control', 'Radio_Security.ino')).read()
    s2 = open(os.path.join(FW, 'Ramp_Remote', 'Radio_Security.ino')).read()
    if s1 != s2:
        print(color('  Radio_Security.ino differs between Ramp_Control and Ramp_Remote - they must be identical copies.', '31'))
        failed += 1

    todo = [t for t in TESTCASES if a.k in t[0] and not (a.quick and t[4])]
    print('Running %d tests%s...' % (len(todo), '' if san else ' (no sanitizers available on this compiler)'))
    with cf.ThreadPoolExecutor(max_workers=a.j) as ex:
        futs = [ex.submit(build_and_run, t, srcs, san, a.verbose) for t in todo]
        for f in cf.as_completed(futs):
            name, ok, shown = f.result()
            print('  %s  %s' % (color('PASS', '32') if ok else color('FAIL', '31'), name))
            if shown.strip():
                print('\n'.join('        ' + l for l in shown.strip().splitlines()))
            failed += (not ok)

    print()
    print(color('ALL PASSED', '32') if not failed else color('%d PROBLEM(S)' % failed, '31'))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
