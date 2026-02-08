project('nwipe', 'c', 'cpp', version: '0.40')

cc = meson.get_compiler('c')

ncurses_dep = dependency('ncurses', required: true)
panel_dep = dependency('panel', required: true)
libconfig_dep = dependency('libconfig', required: true)
parted_dep = dependency('libparted', required: true)
threads_dep = dependency('threads', required: true)

config_data = configuration_data()

if cc.has_header('ncurses.h')
  # default include path
elif cc.has_header('ncurses/ncurses.h')
  config_data.set('NCURSES_IN_SUBDIR', 1)
else
  error('ncurses headers not found')
endif

if cc.has_header('panel.h')
  # default include path
elif cc.has_header('ncurses/panel.h')
  config_data.set('PANEL_IN_SUBDIR', 1)
else
  error('panel headers not found')
endif

configure_file(
  output: 'config.h',
  configuration: config_data,
)

add_project_arguments('-DHAVE_CONFIG_H', language: ['c', 'cpp'])

include_dirs = include_directories('src')

nwipe_sources = files(
  'src/aes/aes_ctr_prng.cpp',
  'src/aes/aes_ctr_prng.h',
  'src/alfg/add_lagg_fibonacci_prng.c',
  'src/alfg/add_lagg_fibonacci_prng.h',
  'src/conf.c',
  'src/conf.h',
  'src/context.h',
  'src/cpu_features.c',
  'src/cpu_features.h',
  'src/create_pdf.c',
  'src/create_pdf.h',
  'src/customers.c',
  'src/customers.h',
  'src/device.c',
  'src/device.h',
  'src/embedded_images/nwipe_exclamation.jpg.c',
  'src/embedded_images/nwipe_exclamation.jpg.h',
  'src/embedded_images/redcross.c',
  'src/embedded_images/redcross.h',
  'src/embedded_images/shred_db.jpg.c',
  'src/embedded_images/shred_db.jpg.h',
  'src/embedded_images/tick_erased.jpg.c',
  'src/embedded_images/tick_erased.jpg.h',
  'src/gui.c',
  'src/gui.h',
  'src/hddtemp_scsi/get_scsi_temp.c',
  'src/hddtemp_scsi/hddtemp.h',
  'src/hddtemp_scsi/scsi.c',
  'src/hddtemp_scsi/scsi.h',
  'src/hddtemp_scsi/scsicmds.c',
  'src/hddtemp_scsi/scsicmds.h',
  'src/hpa_dco.c',
  'src/hpa_dco.h',
  'src/isaac_rand/isaac64.c',
  'src/isaac_rand/isaac64.h',
  'src/isaac_rand/isaac_rand.c',
  'src/isaac_rand/isaac_rand.h',
  'src/isaac_rand/isaac_standard.h',
  'src/logging.c',
  'src/logging.h',
  'src/method.c',
  'src/method.h',
  'src/miscellaneous.c',
  'src/miscellaneous.h',
  'src/mt19937ar-cok/mt19937ar-cok.c',
  'src/mt19937ar-cok/mt19937ar-cok.h',
  'src/nwipe.c',
  'src/nwipe.h',
  'src/options.c',
  'src/options.h',
  'src/pass.c',
  'src/pass.h',
  'src/PDFGen/pdfgen.c',
  'src/PDFGen/pdfgen.h',
  'src/prng.c',
  'src/prng.h',
  'src/temperature.c',
  'src/temperature.h',
  'src/version.c',
  'src/version.h',
  'src/xor/xoroshiro256_prng.c',
  'src/xor/xoroshiro256_prng.h',
)

nwipe_exe = executable(
  'nwipe',
  nwipe_sources,
  include_directories: include_dirs,
  dependencies: [
    ncurses_dep,
    panel_dep,
    libconfig_dep,
    parted_dep,
    threads_dep,
  ],
  install: true,
)

install_man('man/nwipe.8')

run_target(
  'format',
  command: ['clang-format', '-i', '-style=file', 'src/*.c', 'src/*.h'],
)

run_target(
  'check-format',
  command: ['sh', '-c', 'clang-format -i -style=file src/*.c src/*.h && git diff --exit-code'],
)
