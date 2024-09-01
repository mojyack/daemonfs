project('daemonfs', 'cpp', version: '1.0.0', default_options : ['warning_level=3', 'werror=false', 'cpp_std=c++20'])
add_project_arguments('-Wfatal-errors', language: 'cpp')
add_project_arguments('-Wno-missing-field-initializers', language: 'cpp')

deps = [
  dependency('fuse3'),
]

executable('daemonfs',
  files(
    'src/main.cpp',
    'src/daemon.cpp',
    'src/daemonfs.cpp',
    'src/time.cpp',
    'src/signal.cpp',
    'src/message-buffer.cpp',
  ), 
  dependencies : deps,
  install : true)

executable('barebone-init',
  files(
    'src/init.cpp',
    'src/signal.cpp',
  ),
  install : true,
  install_dir : 'sbin')

executable('message-buffer-test',
  files(
    'src/message-buffer.cpp',
    'src/message-buffer-test.cpp',
  ))
