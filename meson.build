project('VCOM', 'c',
  version : '0.9.0',
  default_options : [
    'c_std=gnu99',
    'optimization=3',
    'debug=false',
    'warning_level=2',
    'default_library=static'
  ],
  meson_version : '>=0.62.0',
)

cc = meson.get_compiler('c')

add_global_arguments(
  cc.get_supported_arguments(
    '-D_FORTIFY_SOURCE=2',
    '-Werror=format-security',
    '-Wno-stringop-overflow',
    '-Wno-deprecated-declarations',
  ),
  language : 'c',
)

# get the git revision
gitrev = 'unknown'
res = run_command(['git', 'describe', '--always', '--dirty'], capture : true, check : false)
if res.returncode() != 0
  warning('Failed to parse git revision, most likely not running in git repository!')
else
gitrev = res.stdout()
endif

add_project_arguments(
  '-DVCOM_VERSION="@0@"'.format(meson.project_version()),
  '-DVCOM_GITREV="@0@"'.format(gitrev.strip()),
  language : 'c',
)

vcom_src = files(
  'src/batch_run.c',
  'src/csp_server.c',
  'src/i2cif.c',
  'src/json_dot.c',
  'src/kiss_net.c',
  'src/main.c',
  'src/nii.c',
  'src/vcom_config.c',
  'src/vcom_debug.c',
  'src/posix/stcpsc.c',
  'src/posix/tcpbridge.c',
  'src/posix/tcpcli.c',
  'src/tcpp/cmd_tcpp.c',
  'src/tcpp/tcpp.c',
  'lib/socket_server/src/socket_server.c',
  'src/utils/hex.c',
)

cmds_type = get_option('commands_type')
if cmds_type == 'static'
  vcom_src += files('src/cmd_static.c')
elif cmds_type == 'plugins'
  vcom_src += files('src/plugins.c')
endif

vcom_deps = []
vcom_inc = include_directories(
  'include',
  'src',
  'lib/socket_server/include',
)

dep_names = [
  'csp',
  'pld',
  'console',
  'commands',
  'threads',
  'srv-abs',
]

dep_options = {
  'csp' : [
    'csp_platform=posix',
    'csp_use_ifc_kiss=true',
    'csp_usart_linux=true',
    'csp_use_dedup=false',
  ],
  'pld' : [
    'have_export=true',
  ],
  'console' : [
    'have_csp=true',
    'use_csp_commands=true',
  ]
}

if cmds_type == 'static'
  # if building with static commands, declare them as dependency here
  # example:
  '''
  dep_names += 'some_component-cli'
  dep_options += {
    'some_component-cli' : [
      'build_class=client',
      'build_autotest=false',
      'build_cmdline=static',
      'build_platform=Linux',
      'default_library=static',
    ]
  }
  '''
endif

if get_option('enable_can')
  dep_options += {
    'csp' : dep_options['csp'] + [
      'csp_use_ifc_can=true',
      'csp_can_socketcan=true',
      'csp_have_libsocketcan=true',
    ]
  }
endif

foreach name : dep_names
  d = dependency(name, default_options : dep_options.get(name, []))
  vcom_deps += d
endforeach

if cmds_type == 'plugins'
  dl = cc.find_library('dl', required : false)
  crypto = cc.find_library('crypto', required: true)
  vcom_deps += dl
  vcom_deps += crypto
endif

vcom_config = configuration_data()
vcom_config.set10('VCOM_ENABLE_CAN', get_option('enable_can'))
vcom_config.set('VCOM_STATIC_CMDS', cmds_type == 'static')
vcom_config.set('VCOM_ALLOW_PLUGINS', cmds_type == 'plugins')
vcom_config.set('VCOM_COMMANDS_TYPE', '"' + cmds_type + '"')

subdir('include')

vcom_exe = executable(
  'vcom',
  vcom_src,
  dependencies : vcom_deps,
  include_directories : vcom_inc,
  install : true,
  export_dynamic : true,
  link_args : ['-rdynamic'],
)
