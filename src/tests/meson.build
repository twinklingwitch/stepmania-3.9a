test_base = [stepmania.extract_objects(
  'IniFile.cpp',
  'PrefsManager.cpp',
  'RageException.cpp',
  'RageLog.cpp',
  'RageThreads.cpp',
  'RageTimer.cpp',
  'RageUtil.cpp',
  'RageUtil_FileDB.cpp',
  'global.cpp',
  'RageFile.cpp',
  'RageFileDriver.cpp',
  'RageFileManager.cpp',
  'RageFileDriverDirect.cpp',
  'RageFileDriverDirectHelpers.cpp',
  'RageFileDriverMemory.cpp',
  'RageFileDriverZip.cpp',
  'arch/ArchHooks/ArchHooks.cpp',
  'arch/Dialog/Dialog.cpp',
  'arch/Dialog/DialogDriver_SDL.cpp',
  'arch/Threads/Threads_Pthreads.cpp',
  'archutils/Unix/EmergencyShutdown.cpp')]

if system == 'windows'
  test_base += [stepmania.extract_objects('arch/ArchHooks/ArchHooks_Win32.cpp')]
else
  test_base += [stepmania.extract_objects(
    'arch/ArchHooks/ArchHooks_Unix.cpp',
    'archutils/Unix/AssertionHandler.cpp',
    'archutils/Unix/GetSysInfo.cpp',
    'archutils/Unix/LinuxThreadHelpers.cpp',
    'archutils/Unix/RunningUnderValgrind.cpp',
    'archutils/Unix/SignalHandler.cpp')]
endif

if use_crash_handler
  test_base += [stepmania.extract_objects(
    'archutils/Unix/Backtrace.cpp',
    'archutils/Unix/BacktraceNames.cpp',
    'archutils/Unix/CrashHandler.cpp',
    'archutils/Unix/CrashHandlerChild.cpp')]
endif

test_audio_readers_objects = [stepmania.extract_objects(
  'RageSoundReader_FileReader.cpp',
  'RageSoundReader_Preload.cpp',
  'RageSoundReader_Resample.cpp',
  'RageSoundReader_Resample_Fast.cpp',
  'RageSoundReader_Resample_Good.cpp',
  'RageSoundResampler.cpp',
  'RageSoundReader_WAV.cpp')]
if vorbis_dep.found()
  test_audio_readers_objects += [stepmania.extract_objects('RageSoundReader_Vorbisfile.cpp')]
endif
if mad_dep.found()
  test_audio_readers_objects += [stepmania.extract_objects('RageSoundReader_MP3.cpp')]
endif

test_file_errors_objects = [stepmania.extract_objects(
  'DateTime.cpp',
  'MsdFile.cpp',
  'XmlFile.cpp',
  'CryptManager.cpp',
  'CryptHelpers.cpp')]

test_timing_data_objects = [stepmania.extract_objects('TimingData.cpp')]

test_wife_objects = [stepmania.extract_objects('Wife.cpp', 'WifeReplay.cpp')]

test_wifescores_objects = [stepmania.extract_objects(
  'WifeScores.cpp',
  'XmlFile.cpp',
  'DateTime.cpp')]

test_chartkey_objects = [stepmania.extract_objects(
  'ChartKey.cpp',
  'CryptManager.cpp',
  'CryptHelpers.cpp')]

test_minacalc_objects = [stepmania.extract_objects(
  'CalcRows.cpp',
  'NoteData.cpp',
  'NoteTypes.cpp',
  'TimingData.cpp')]

test_onlinejson_objects = [stepmania.extract_objects('OnlineJson.cpp', 'OnlineInputLog.cpp', 'OnlineHttp.cpp')]
if not online_dep.found()
  test_onlinejson_objects += [stepmania.extract_objects('OnlineHttp_None.cpp')]
elif system == 'windows'
  test_onlinejson_objects += [stepmania.extract_objects('OnlineHttp_WinHttp.cpp')]
else
  test_onlinejson_objects += [stepmania.extract_objects('OnlineHttp_Curl.cpp')]
endif

test_onlinerules_objects = [stepmania.extract_objects(
  'OnlineRules.cpp',
  'NoteData.cpp',
  'NoteTypes.cpp')]

test_discord_objects =[stepmania.extract_objects('DiscordIpc.cpp', 'OnlineJson.cpp')]

test_inc = [top_inc, src_inc]
test_deps = deps

tests = {
  'test_audio_readers': test_audio_readers_objects,
  'test_file_readers': [],
  'test_file_errors': test_file_errors_objects,
  'test_timing_data': test_timing_data_objects,
  'test_wife': test_wife_objects,
  'test_chartkey': test_chartkey_objects,
  'test_wifescores': test_wifescores_objects,
  'test_minacalc': test_minacalc_objects,
  'test_onlinejson': test_onlinejson_objects,
  'test_onlinerules': test_onlinerules_objects,
  'test_discord': test_discord_objects,
}

foreach name, objects : tests
  exe = executable(name,
    [name + '.cpp', 'test_misc.cpp'],
    objects: [test_base, objects],
    include_directories: test_inc,
    dependencies: test_deps,
    export_dynamic: use_crash_handler,
    link_args: exe_link_args)
  if name == 'test_file_readers'
    test(name + '_create', exe, args: ['-c'], is_parallel: false, priority: 1,
      workdir: meson.current_build_dir())
  endif
  test_args = []
  if name == 'test_chartkey'
    test_args = [meson.current_source_dir() / 'chartkey']
  endif
  test(name, exe, args: test_args, is_parallel: false, workdir: meson.current_build_dir())
endforeach
