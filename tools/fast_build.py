"""Optional compile profile. The default build does not use it.

tools/make_game.py (--fast, --pgo) and tools/build_game.py (--fast) read these flags only when asked.
BUILD_FAST.md is the Windows guide: what the default compiles with, the commands, and what is not measured.
"""

FAST_OPT = '-O3'
# -fomit-frame-pointer is already what clang does at -O1 and above on x86-64. It is named here so the profile says so.
# -DNDEBUG compiles out assert(). The runtime's own checks are rt_fatal and stay; perf traces stay too.
# -flto=thin needs lld: GNU ld cannot load LLVMgold.dll from the launcher's MSYS2 UCRT64 toolchain (no ld.lld there),
# so the probe link fails. LTO is only added when ld.lld is next to clang (lto_ok).
FAST_CFLAGS = ('-fomit-frame-pointer', '-fno-plt', '-DNDEBUG')
FAST_LDFLAGS = ()
LTO_FLAG = '-flto=thin'


def lto_ok(bindir):
    import os
    return os.path.isfile(os.path.join(str(bindir), 'ld.lld.exe')) or os.path.isfile(os.path.join(str(bindir), 'ld.lld'))

# --march names. v3 and v4 are the x86-64 levels; znver4 also schedules for Zen 4.
MARCHES = {
    'znver4': '-march=znver4',
    'v3': '-march=x86-64-v3',
    'v4': '-march=x86-64-v4',
    'x86-64-v3': '-march=x86-64-v3',
    'x86-64-v4': '-march=x86-64-v4',
}
AVX512 = frozenset({'znver4', 'v4', 'x86-64-v4'})


class Opts:
    """What --fast and --pgo add. Absent (None) means the default build."""

    def __init__(self, fast, march_name, march_flag, pgo, flags, link, toolchain, tag, salt, text):
        self.fast = fast              # -O3, thin LTO, and a march on the runtime as well as the lifted code
        self.march_name = march_name  # the --march name, or ''
        self.march_flag = march_flag  # '-march=...' or '' (then the fast profile uses the detected x86-64 level)
        self.pgo = pgo                # None, 'gen' or 'use'
        self.flags = flags            # profile flags for every compile (not the -O and -march, which depend on the file)
        self.link = link              # extra link flags
        self.toolchain = toolchain    # llvm-mingw directory for --pgo gen, or None
        self.tag = tag                # compile step key suffix; '' would be the default and is never stored here
        self.salt = salt              # mixed into the object-cache key when the profile file's bytes matter
        self.text = text              # one line for the build log


def march_flag(name):
    """The -march= flag for a --march name, or '' when name is empty. An unknown name raises ValueError."""
    if not name:
        return ''
    if name not in MARCHES:
        raise ValueError(name)
    return MARCHES[name]


def needs_avx512(name):
    return name in AVX512


def march_runs_here(name, cpu_tag):
    """False when name asks for AVX-512 and cpu_tag says this processor has none.
    'any' and an empty tag do not refuse: the probe could not read the processor."""
    if not needs_avx512(name):
        return True
    if not cpu_tag or cpu_tag == 'any':
        return True
    return cpu_tag.endswith('1')


def opt_flag(guest, fast, pgo):
    """The -O flag. fast is -O3 for both sides. A profile without fast compiles the lifted code at -O2, the level
    tools/build_game.py --pgo uses. The runtime is already -O2."""
    if fast:
        return FAST_OPT
    if pgo and guest:
        return '-O2'
    return '-O1' if guest else '-O2'


def march_args(guest, level, fast, chosen):
    """chosen is a -march= flag or ''.
    The default runtime stays generic (no -march), so one prebuilt library serves every PC.
    The fast profile compiles the runtime for the same march as the lifted code: the replay is runtime code."""
    if fast:
        if chosen:
            return [chosen]
        if level:
            return [f'-march=x86-64-v{level}']
        return []
    if guest and level:
        return [f'-march=x86-64-v{level}']
    return []


def pgo_use_flags(path):
    """path is the .profdata as clang should see it (forward slashes). A function the profile does not match compiles
    without counts; the warnings for that are off, as in tools/build_game.py --pgo."""
    return (
        f'-fprofile-use={path}',
        '-Wno-profile-instr-unprofiled',
        '-Wno-profile-instr-out-of-date',
        '-Wno-backend-plugin',
        '-mllvm', '-static-func-full-module-prefix=false',
    )


def pgo_gen_flags(directory):
    """directory is where the instrumented exe writes its .profraw, forward slashes, absolute."""
    return (
        f'-fprofile-generate={directory}',
        '-mllvm', '-static-func-full-module-prefix=false',
    )


def profile_tag(fast, march_name, pgo_kind, pgo_digest=''):
    """Suffix of the compile step key. '' for the default build, so a build that does not opt in keeps the key it had."""
    bits = []
    if fast:
        bits.append('fast')
        if march_name:
            bits.append(march_name)
    if pgo_kind == 'gen':
        bits.append('pgo-gen')
    elif pgo_kind == 'use':
        bits.append('pgo-' + pgo_digest[:16])
    return '+'.join(bits)


def describe(fast, march_text, pgo_kind):
    parts = []
    if fast:
        parts.append('-O3 -fno-plt -fomit-frame-pointer -DNDEBUG, march ' + march_text)
    if pgo_kind == 'gen':
        parts.append('instrumented (-fprofile-generate)')
    elif pgo_kind == 'use':
        parts.append('profile-guided (-fprofile-use)')
    return '; '.join(parts)
