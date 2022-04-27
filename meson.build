project('SLB', 'c',
  version : '0.1',
  default_options : ['warning_level=3'])

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

url = 'https://raw.githubusercontent.com/Cyan4973/xxHash/dev'
run_command('wget', '-nv', '-N', url+'/xxhash.h')
run_command('wget', '-nv', '-N', url+'/xxhash.c')

lib1 = library('lib1', 'lib1.c',
               dependencies: m_dep)

ib1 = library('ib1', 'ib1.c',
              link_with: lib1,
              dependencies: dependency('libibverbs'))

executable('ibperfser',
           'ibperfser.c',
           link_with: [lib1,ib1],
           dependencies: dependency('libibverbs'),
           install : true)

executable('ibperfcli',
           'ibperfcli.c',
           link_with: [lib1,ib1],
           dependencies: dependency('libibverbs'),
           install : true)

executable('kvclient',
           'kvclient.c',
           link_with: lib1,
           install : true)

executable('ibkvser',
           'ibkvser.c',
           link_with: [lib1,ib1],
           dependencies: dependency('libibverbs'),
           install : true)

executable('ibkvcli',
           'ibkvcli.c',
           link_with: [lib1, ib1],
           dependencies: dependency('libibverbs'),
           install : true)

executable('kvserver',
           'kvserver.c',
           link_with: lib1,
           install : true)

executable('ib1_clean',
           'ib1_clean.c',
           link_with: [lib1, ib1],
           dependencies: dependency('libibverbs'),
           install : true)

executable('map_test',
           'map_test.c',
           link_with: lib1,
           install : true)

executable('lib1_test',
           ['lib1_test.c', 'xxhash.c'],
           link_with: lib1,)
