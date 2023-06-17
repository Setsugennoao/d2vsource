project('d2vsource', 'cpp',
  default_options : ['buildtype=release', 'b_ndebug=if-release', 'cpp_std=c++17'],
  meson_version : '>=0.51.0',
  version : '8'
)

add_project_arguments('-ffast-math', '-Wno-unused-variable', '-Wno-ignored-attributes', '-Wno-sign-compare', '-Wno-narrowing', '-Wno-int-in-bool-context', '-ffast-math', language : 'cpp')

core_include_dir = include_directories('src/core')

sources = [
  'src/core/compat.cpp',
  'src/core/compat.hpp',
  'src/core/d2v.cpp',
  'src/core/d2v.hpp',
  'src/core/decode.cpp',
  'src/core/decode.hpp',
  'src/core/gop.hpp',
  'src/vs/applyrff.cpp',
  'src/vs/applyrff.hpp',
  'src/vs/d2vsource.cpp',
  'src/vs/d2vsource.hpp',
  'src/vs/directrender.cpp',
  'src/vs/directrender.hpp',
  'src/vs/vapoursynth.cpp',
]

vapoursynth_dep = dependency('vapoursynth').partial_dependency(compile_args : true, includes : true)

avutil_dep = dependency('libavutil', static : false)
avformat_dep = dependency('libavformat', static : false)
avcodec_dep = dependency('libavcodec', static : false)

deps = [vapoursynth_dep, avutil_dep, avformat_dep, avcodec_dep]

if host_machine.system() == 'windows'
  deps += [meson.get_compiler('cpp').find_library('winpthread', static: true)]
endif

if host_machine.cpu_family().startswith('x86')
  add_project_arguments('-mfpmath=sse', '-msse2', language : 'cpp')
endif

shared_module('libd2vsource', sources,
  dependencies : deps,
  include_directories : core_include_dir,
  link_args: ['-static'],
  gnu_symbol_visibility : 'hidden',
)
