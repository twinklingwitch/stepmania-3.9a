# Etterna's difficulty calculator, built on its own: it needs C++20, while the
# game stays C++03 and only uses it through minacalc_c.h.
#
# NDEBUG as in Etterna's release builds (some asserts would otherwise abort on
# odd charts), and no fused multiply-add so the numbers don't depend on the CPU.
#
# Vendored code: warning_level=0 still leaves the warnings clang enables by
# default (like -Winconsistent-missing-override), so silence those too.
minacalc_args = ['-DSTANDALONE_CALC', '-DNDEBUG', '-w']
minacalc_args += cxx.get_supported_arguments('-ffp-contract=off')

# On 64-bit ARM (Apple Silicon) the calc's SSE intrinsics come from sse2neon,
# as in Etterna's own ARM builds.
minacalc_inc = []
if host_machine.cpu_family() == 'aarch64'
  minacalc_inc += include_directories('sse2neon')
endif

minacalc_lib = static_library('minacalc',
  'minacalc_c.cpp',
  'Etterna/MinaCalc/MinaCalc.cpp',
  cpp_args: minacalc_args,
  include_directories: minacalc_inc,
  override_options: ['cpp_std=c++20', 'warning_level=0'])

minacalc_dep = declare_dependency(
  link_with: minacalc_lib,
  include_directories: include_directories('.'))
