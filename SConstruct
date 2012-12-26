env = Environment(
    CPPPATH=('src', '.'),
    CFLAGS=('-pthread', '-Werror', '-Wextra', '-std=gnu99', '-O3'),
    LINKFLAGS=('-pthread',),
)

# Add MySQL libs and includes
env.ParseConfig('mysql_config --include --cflags --libs_r')

env.Program(target='sfsql-proxy',
    source=env.Glob('src/*.c') + \
           env.Glob('src/*/*.c') + \
           env.Glob('map/*.c')
)
