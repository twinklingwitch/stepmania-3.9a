project('stepmania', ['c', 'cpp'],
  version: '3.9',
  meson_version: '>=1.1',
  license: 'MIT',
  default_options: [
    'c_std=gnu99',
    'cpp_std=gnu++03',
    'buildtype=debugoptimized',
    'warning_level=1',
  ])

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')
conf = configuration_data()
not_found = dependency('', required: false)

system = host_machine.system()
cpu = host_machine.cpu_family()

if system == 'windows'
  conf.set('WINDOWS', 1)
else
  conf.set('UNIX', 1)
endif
if system == 'linux'
  conf.set('LINUX', 1)
  conf.set('_GNU_SOURCE', 1)
elif system == 'freebsd'
  conf.set('FREEBSD', 1)
elif system == 'openbsd'
  conf.set('OPENBSD', 1)
elif system == 'windows'
  # Windows 10 and later only.
  # localtime_r() and friends are hidden unless this is set.
  add_project_arguments('-D_WIN32_WINNT=0x0A00', '-DWINVER=0x0A00', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX',
                        '-D_POSIX_THREAD_SAFE_FUNCTIONS=200112L',
                        # C++03 only gets UINT64_C() and INT64_MAX from stdint.h on request; FFmpeg's headers want them.
                        '-D__STDC_CONSTANT_MACROS', '-D__STDC_LIMIT_MACROS', language: ['c', 'cpp'])
elif system == 'darwin'
  # macOS 14 and later on Apple Silicon.  It's a Unix as far as the code is
  # concerned; DARWIN marks the few places where it differs.
  conf.set('DARWIN', 1)
  # The legacy OpenGL calls still work, but warn on every use.
  add_project_arguments('-DGL_SILENCE_DEPRECATION', language: ['c', 'cpp'])
else
  error('Unsupported system: ' + system)
endif

if cpu == 'x86'
  conf.set('CPU_X86', 1)
elif cpu == 'x86_64'
  conf.set('CPU_X86_64', 1)
elif cpu in ['ppc', 'ppc64']
  conf.set('CPU_PPC', 1)
endif

if host_machine.endian() == 'little'
  conf.set('ENDIAN_LITTLE', 1)
else
  conf.set('ENDIAN_BIG', 1)
endif

threads_dep = dependency('threads')
dl_dep = not_found
if system != 'windows'
  dl_dep = dependency('dl')
endif
m_dep = cc.find_library('m', required: false)
if system == 'windows'
  gl_dep = cc.find_library('opengl32')
  glu_dep = cc.find_library('glu32')
elif system == 'darwin'
  # GLU is part of the OpenGL framework.  Don't let a gl.pc from Homebrew's
  # Mesa (an X11 libGL) get picked instead.
  gl_dep = dependency('appleframeworks', modules: ['OpenGL'])
  glu_dep = not_found
else
  gl_dep = dependency('gl')
  glu_dep = dependency('glu')
endif
winmm_dep = not_found
ws2_dep = not_found
if system == 'windows'
  winmm_dep = cc.find_library('winmm')
  ws2_dep = cc.find_library('ws2_32')
endif
zlib_dep = dependency('zlib')
sqlite_dep = dependency('sqlite3', required: system != 'darwin')
if not sqlite_dep.found()
  # The one in the macOS SDK has no pkg-config file of its own.
  sqlite_dep = cc.find_library('sqlite3', has_headers: 'sqlite3.h')
endif
png_dep = dependency('libpng')
iconv_dep = dependency('iconv', required: false)
if not iconv_dep.found()
  # A separate GNU libiconv, as on Windows.
  iconv_dep = cc.find_library('iconv', has_headers: 'iconv.h', required: false)
endif
crypto_dep = dependency('libcrypto', required: system != 'darwin')
if not crypto_dep.found()
  # macOS's own libcrypto is private (loading it aborts the program), and
  # Homebrew's openssl@3 is keg-only, so it has to be pointed to.
  error('libcrypto not found.  Install openssl@3 with Homebrew and add $(brew --prefix openssl@3)/lib/pkgconfig to PKG_CONFIG_PATH.')
endif
pcre_dep = dependency('libpcre2-8')
samplerate_dep = dependency('samplerate')

if cc.has_header('endian.h')
  conf.set('HAVE_ENDIAN_H', 1)
elif system == 'darwin' and cc.has_header('machine/endian.h')
  conf.set('HAVE_MACHINE_ENDIAN_H', 1)
endif
if cc.has_header('inttypes.h')
  conf.set('HAVE_INTTYPES_H', 1)
endif
if cc.has_header_symbol('signal.h', 'SIGPWR')
  conf.set('HAVE_DECL_SIGPWR', 1)
endif
if cc.has_function('pthread_mutex_timedlock', dependencies: threads_dep, prefix: '#include <pthread.h>')
  conf.set('HAVE_PTHREAD_MUTEX_TIMEDLOCK', 1)
endif
if cc.has_function('pthread_cond_timedwait', dependencies: threads_dep, prefix: '#include <pthread.h>')
  conf.set('HAVE_PTHREAD_COND_TIMEDWAIT', 1)
endif
if cc.compiles('__thread int x; int main(void) { return x; }', name: '__thread')
  conf.set('HAVE_TLS', 1)
endif

if iconv_dep.found() and cc.has_header('iconv.h', dependencies: iconv_dep)
  conf.set('HAVE_ICONV', 1)
  # iconv was found in libc (the BSDs).  Other dependencies may still put
  # /usr/local/include on the include path, where GNU libiconv's iconv.h
  # renames iconv_open() and friends to libiconv_open(), which libc does not
  # have.  LIBICONV_PLUG makes that header keep the standard names.
  if iconv_dep.type_name() == 'builtin'
    conf.set('LIBICONV_PLUG', 1)
  endif
  iconv_test = '#include <iconv.h>\nint main() { iconv_t c = 0; char *in = 0, *out = 0; size_t i = 0, o = 0; iconv(c, &in, &i, &out, &o); return 0; }'
  if cxx.compiles(iconv_test, dependencies: iconv_dep, name: 'iconv() takes char **')
    conf.set('ICONV_CONST', '')
  else
    conf.set('ICONV_CONST', 'const')
  endif
endif

sdl_dep = dependency('sdl3', version: '>=3.2.0')
conf.set('HAVE_SDL', 1)

ffmpeg_deps = []
if not get_option('ffmpeg').disabled()
  foreach name : ['libavformat', 'libavcodec', 'libavutil', 'libswscale']
    ffmpeg_deps += dependency(name, required: get_option('ffmpeg'))
  endforeach
endif
have_ffmpeg = ffmpeg_deps.length() > 0
foreach dep : ffmpeg_deps
  have_ffmpeg = have_ffmpeg and dep.found()
endforeach
if have_ffmpeg
  conf.set('HAVE_FFMPEG', 1)
endif

vorbis_dep = not_found
if not get_option('vorbis').disabled()
  vorbis_dep = dependency('vorbisfile', required: get_option('vorbis'))
endif
if not vorbis_dep.found()
  conf.set('NO_VORBIS_SUPPORT', 1)
endif

mad_dep = not_found
if not get_option('mad').disabled()
  mad_dep = dependency('mad', required: false)
  if not mad_dep.found()
    mad_dep = cc.find_library('mad', has_headers: 'mad.h', required: get_option('mad'))
  endif
endif
if not mad_dep.found()
  conf.set('NO_MP3_SUPPORT', 1)
endif

jpeg_dep = not_found
if not get_option('jpeg').disabled()
  jpeg_dep = dependency('libjpeg', required: get_option('jpeg'))
endif
if not jpeg_dep.found()
  conf.set('NO_JPEG_SUPPORT', 1)
endif

# The extras (Wife scoring and everything that hangs off it: MSD, local and
# online leaderboards, chat and rooms) are the aura build.  Without them the
# game is plain 3.9 with the fixes, and none of their code is built.
extras = get_option('extras')
if not extras
  conf.set('WITHOUT_EXTRAS', 1)
endif

# Online scores need an HTTPS client: WinHTTP comes with Windows, libcurl is
# optional elsewhere.  Without one the online code is built without a backend.
# The vanilla build has no online code, so it needs neither.
online_dep = not_found
if extras and not get_option('online').disabled()
  if system == 'windows'
    online_dep = cc.find_library('winhttp', required: get_option('online'))
  elif system == 'darwin'
    # The system libcurl.  Homebrew's pkg-config knows about it; if this one
    # doesn't, look for it directly.
    online_dep = dependency('libcurl', required: false)
    if not online_dep.found()
      online_dep = cc.find_library('curl', has_headers: 'curl/curl.h', required: get_option('online'))
    endif
  else
    online_dep = dependency('libcurl', required: get_option('online'))
  endif
endif
if online_dep.found()
  conf.set('HAVE_ONLINE', 1)
endif

use_networking = get_option('networking')
if not use_networking
  conf.set('WITHOUT_NETWORKING', 1)
endif

# The crash handler is signals, ucontext and ELF backtraces; none of it exists on
# Windows.  On macOS the system's crash reporter does the job instead.
use_crash_handler = get_option('crash_handler') and system not in ['windows', 'darwin']
execinfo_dep = not_found
if use_crash_handler
  conf.set('CRASH_HANDLER', 1)

  have_backtrace = cc.has_function('backtrace', prefix: '#include <execinfo.h>')
  if not have_backtrace
    execinfo_dep = cc.find_library('execinfo', required: false)
    have_backtrace = execinfo_dep.found() and cc.has_function('backtrace', dependencies: execinfo_dep, prefix: '#include <execinfo.h>')
  endif

  if system == 'linux' and cpu in ['x86', 'x86_64']
    conf.set('BACKTRACE_METHOD_X86_LINUX', 1)
    conf.set_quoted('BACKTRACE_METHOD_TEXT', 'x86 custom backtrace')
  elif have_backtrace
    conf.set('BACKTRACE_METHOD_BACKTRACE', 1)
    conf.set_quoted('BACKTRACE_METHOD_TEXT', 'backtrace()')
  else
    conf.set_quoted('BACKTRACE_METHOD_TEXT', 'none')
  endif

  if cc.has_function('dladdr', dependencies: dl_dep, prefix: '#define _GNU_SOURCE\n#include <dlfcn.h>')
    conf.set('BACKTRACE_LOOKUP_METHOD_DLADDR', 1)
    conf.set_quoted('BACKTRACE_LOOKUP_METHOD_TEXT', 'dladdr')
  elif have_backtrace
    conf.set('BACKTRACE_LOOKUP_METHOD_BACKTRACE_SYMBOLS', 1)
    conf.set_quoted('BACKTRACE_LOOKUP_METHOD_TEXT', 'backtrace_symbols')
  else
    conf.set_quoted('BACKTRACE_LOOKUP_METHOD_TEXT', 'none')
  endif

  if cxx.links('#include <cxxabi.h>\nint main() { return abi::__cxa_demangle("", 0, 0, 0) != 0; }', name: 'abi::__cxa_demangle')
    conf.set('HAVE_CXA_DEMANGLE', 1)
    conf.set_quoted('BACKTRACE_DEMANGLE_METHOD_TEXT', 'cxa_demangle')
  endif
endif

configure_file(output: 'config.h', configuration: conf)

add_project_arguments('-DHAVE_CONFIG_H', language: ['c', 'cpp'])
if cxx.get_id() == 'gcc'
  add_project_arguments('-finline-limit=300', language: 'cpp')
endif

top_inc = include_directories('.')

exe_link_args = []
if system == 'windows'
  # The compiler runtimes are built in, so only the SDL and codec DLLs need to ship
  # next to the exe.
  exe_link_args += ['-static-libgcc', '-static-libstdc++',
                    '-Wl,-Bstatic,--whole-archive', '-lwinpthread', '-Wl,--no-whole-archive,-Bdynamic']
endif
if use_crash_handler and cc.has_link_argument('-no-pie')
  exe_link_args += '-no-pie'
endif
if system == 'darwin'
  # Room for Utils/macos-bundle.sh to point the library paths into the app bundle.
  exe_link_args += '-Wl,-headerpad_max_install_names'
endif

if system == 'windows'
  # Everything goes in one folder, which is also where the game looks for its data.
  pkgdatadir = get_option('prefix')
  pkglibexecdir = get_option('prefix')
  pkgdocdir = get_option('prefix')
else
  # A system-wide install of the build with the extras is stepmania-aura, so
  # it can sit next to plain 3.9's.
  install_name = extras ? 'stepmania-aura' : 'stepmania'
  pkgdatadir = get_option('prefix') / get_option('datadir') / install_name
  pkglibexecdir = get_option('prefix') / get_option('libexecdir') / install_name
  pkgdocdir = get_option('prefix') / get_option('datadir') / 'doc' / install_name
endif

if extras
  subdir('extern/MinaCalc')
else
  minacalc_dep = declare_dependency()
endif
subdir('src')

foreach dir : ['Announcers', 'BGAnimations', 'CDTitles', 'Characters', 'Courses',
               'Data', 'NoteSkins', 'RandomMovies', 'Songs', 'Themes', 'Visualizations']
  install_subdir(dir, install_dir: pkgdatadir)
endforeach
install_subdir('Packages', install_dir: pkgdatadir)
if system == 'windows'
  install_subdir('Docs', install_dir: pkgdocdir)
else
  install_subdir('Docs', install_dir: pkgdocdir, strip_directory: true)
endif
install_data('NEWS', 'README-FIRST.html', 'FAQ.html', install_dir: pkgdocdir)

if system == 'windows'
  # Put the DLLs the exe needs next to it.  They live in the bin directory of
  # wherever the libraries come from, which is not on any search path when
  # cross-compiling, so it can be given as dll_dirs.
  dll_dirs = get_option('dll_dirs')
  sdl_prefix = sdl_dep.get_variable(pkgconfig: 'prefix', default_value: '')
  if sdl_prefix != ''
    dll_dirs += [sdl_prefix / 'bin']
  endif
  dll_args = ['--objdump', find_program('x86_64-w64-mingw32-objdump', 'i686-w64-mingw32-objdump', 'objdump').full_path()]
  foreach dir : dll_dirs
    dll_args += ['--dll-dir', dir]
  endforeach
  meson.add_install_script(find_program('python3'), files('Utils/windows-dlls.py'), dll_args, 'stepmania.exe')
else
  wrapper = configure_file(
    input: 'Utils/stepmania.in',
    output: install_name,
    configuration: {'pkgdatadir': pkgdatadir, 'pkglibexecdir': pkglibexecdir})
  install_data(wrapper, install_dir: get_option('prefix') / get_option('bindir'), install_mode: 'rwxr-xr-x')
endif
