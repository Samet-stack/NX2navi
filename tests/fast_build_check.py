"""The optional fast profile does not change the default compile, and its flags are the ones BUILD_FAST.md names.

No game files and no compiler: the commands are built and not run.

Run: python tests/fast_build_check.py
"""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))

import fast_build  # noqa: E402
import make_game  # noqa: E402


def cmd(guest, level=3, opts=None, fast_cc=None):
    src = Path('lift/main_0001.c' if guest else 'rt/vk_exec.c')
    dst = Path('obj/main_main_0001.o' if guest else 'obj/vk_exec.o')
    built = make_game.runtime_cmd('clang', 'clang++', ['-I', 'rt'], level, fast_cc, 'C:/msys64/ucrt64', opts=opts)
    return built(src, dst)


class Flags(unittest.TestCase):
    def test_the_default_build_is_unchanged(self):
        guest, runtime = cmd(True), cmd(False)
        self.assertIn('-O1', guest)
        self.assertNotIn('-O2', guest)
        self.assertNotIn('-O3', guest)
        self.assertIn('-march=x86-64-v3', guest)
        self.assertIn('-O2', runtime)
        self.assertNotIn('-O3', runtime)
        self.assertNotIn('-march=x86-64-v3', runtime)
        self.assertNotIn('-flto=thin', guest + runtime)
        self.assertNotIn('-fno-plt', guest + runtime)
        self.assertNotIn('-DNDEBUG', guest + runtime)
        self.assertNotIn('-fprofile-use=p.profdata', guest)

    def test_fast_compiles_the_runtime_for_the_same_march(self):
        opts = fast_build.Opts(True, 'znver4', '-march=znver4', None, (), ('-flto=thin',), None, 'fast+znver4', '', '')
        guest, runtime = cmd(True, opts=opts), cmd(False, opts=opts)
        for one in (guest, runtime):
            self.assertIn('-O3', one)
            self.assertNotIn('-O1', one)
            self.assertNotIn('-O2', one)
            self.assertIn('-march=znver4', one)
            self.assertNotIn('-march=x86-64-v3', one)
            for flag in fast_build.FAST_CFLAGS:
                self.assertIn(flag, one)
        self.assertNotIn('-DNX2NAVI_PGO_GEN', guest)
        self.assertNotIn('-DNX2NAVI_PGO_GEN', runtime)

    def test_fast_without_an_explicit_march_uses_the_detected_level_on_both(self):
        opts = fast_build.Opts(True, '', '', None, (), ('-flto=thin',), None, 'fast', '', '')
        self.assertIn('-march=x86-64-v3', cmd(False, opts=opts))
        self.assertNotIn('-march=x86-64-v3', cmd(False, level=0, opts=opts))

    def test_pgo_without_fast_lifts_at_o2_and_leaves_the_runtime_generic(self):
        opts = fast_build.Opts(False, '', '', 'use', fast_build.pgo_use_flags('p.profdata'), (), None, 'pgo-abc', 'abc', '')
        guest, runtime = cmd(True, opts=opts), cmd(False, opts=opts)
        self.assertIn('-O2', guest)
        self.assertNotIn('-O3', guest)
        self.assertIn('-fprofile-use=p.profdata', guest)
        self.assertIn('-O2', runtime)
        self.assertNotIn('-march=x86-64-v3', runtime)
        self.assertNotIn('-DNX2NAVI_PGO_GEN', runtime)

    def test_pgo_gen_defines_the_flush_only_on_the_runtime(self):
        opts = fast_build.Opts(False, '', '', 'gen', fast_build.pgo_gen_flags('C:/out/pgo'), ('-fprofile-generate',),
                               'llvm', 'pgo-gen', '', '')
        guest, runtime = cmd(True, opts=opts), cmd(False, opts=opts)
        self.assertIn('-fprofile-generate=C:/out/pgo', guest)
        self.assertNotIn('-DNX2NAVI_PGO_GEN', guest)
        self.assertIn('-DNX2NAVI_PGO_GEN', runtime)
        self.assertIn('--sysroot=C:/msys64/ucrt64', runtime)
        # the fast clang is not used for an instrumented build, even if one was passed
        self.assertEqual(guest[0], 'clang')

    def test_fast_does_not_use_the_second_clang(self):
        opts = fast_build.Opts(True, '', '', None, (), (), None, 'fast', '', '')
        guest = cmd(True, opts=opts, fast_cc='clang64')
        self.assertEqual(guest[0], 'clang')
        self.assertNotIn('--sysroot=C:/msys64/ucrt64', guest)

    def test_the_default_still_uses_the_second_clang_for_the_lifted_code(self):
        guest = cmd(True, fast_cc='clang64')
        self.assertEqual(guest[0], 'clang64')
        self.assertIn('--sysroot=C:/msys64/ucrt64', guest)
        self.assertEqual(cmd(False, fast_cc='clang64')[0], 'clang')

    def test_march_refuses_avx512_when_the_processor_has_none(self):
        self.assertTrue(fast_build.march_runs_here('znver4', '111'))
        self.assertFalse(fast_build.march_runs_here('znver4', '110'))
        self.assertFalse(fast_build.march_runs_here('v4', '110'))
        self.assertTrue(fast_build.march_runs_here('znver4', 'any'))
        self.assertTrue(fast_build.march_runs_here('v3', '100'))
        self.assertEqual(fast_build.march_flag('znver4'), '-march=znver4')
        self.assertEqual(fast_build.march_flag(''), '')

    def test_the_default_compile_key_gains_no_suffix(self):
        digests = {'exefs_digest': lambda out: 'e', 'lift_digest': lambda: 'l', 'tree_digest': lambda *a, **k: 't',
                   'sha256_file': lambda p: 's', 'kit_version': lambda: 'k', 'cpu_tag': lambda: '111'}
        from unittest import mock
        with mock.patch.multiple(make_game, **digests):
            plain = make_game.step_key('compile', 'id', Path('out'))
            self.assertEqual(plain, make_game.step_key('compile', 'id', Path('out'), ''))
            tagged = make_game.step_key('compile', 'id', Path('out'), 'fast+znver4')
        self.assertEqual(tagged, plain + '|fast+znver4')
        self.assertEqual(fast_build.profile_tag(False, '', None), '')
        self.assertEqual(fast_build.profile_tag(True, 'znver4', 'use', '0123456789abcdef999'),
                         'fast+znver4+pgo-0123456789abcdef')


class BuildGameFlags(unittest.TestCase):
    def test_set_fast_and_pgo_keep_o3(self):
        import build_game
        gen, rt, link = build_game.GEN_FLAGS[:], build_game.RT_FLAGS[:], build_game.LINK_FLAGS[:]
        old = build_game.PGO_INPUT
        try:
            self.assertIn('-O1', build_game.GEN_FLAGS)
            self.assertIn('-O2', build_game.RT_FLAGS)
            self.assertNotIn('-flto=thin', build_game.LINK_FLAGS)
            build_game.set_fast('-march=znver4')
            self.assertIn('-O3', build_game.GEN_FLAGS)
            self.assertNotIn('-O1', build_game.GEN_FLAGS)
            self.assertIn('-march=znver4', build_game.RT_FLAGS)
            self.assertNotIn('-g', build_game.RT_FLAGS)
            self.assertIn('-DNDEBUG', build_game.RT_FLAGS)
            self.assertIn('-flto=thin', build_game.LINK_FLAGS)
            build_game.set_pgo('gen', Path('build/out'))
            opts = [f for f in build_game.GEN_FLAGS if f.startswith('-O')]
            self.assertEqual(opts, ['-O3'])
            self.assertTrue(any(f.startswith('-fprofile-generate=') for f in build_game.GEN_FLAGS))
            self.assertIn('-DNX2NAVI_PGO_GEN', build_game.RT_FLAGS)
            self.assertIn('-fprofile-generate', build_game.LINK_FLAGS)
        finally:
            build_game.GEN_FLAGS[:] = gen
            build_game.RT_FLAGS[:] = rt
            build_game.LINK_FLAGS[:] = link
            build_game.PGO_INPUT = old


if __name__ == '__main__':
    unittest.main(verbosity=2)
