project(
  'contended',
  'cpp',
  meson_version: '>=1.1',
  # warning_level=2 is -Wall -Wextra. compile_commands.json is always
  # generated in the build directory.
  default_options: ['cpp_std=c++20', 'warning_level=2'],
)

# Only debug (tests, unoptimized) and debugoptimized (benchmarks/sanitizers)
# are supported.
buildtype = get_option('buildtype')
supported_buildtypes = ['debug', 'debugoptimized']
if buildtype not in supported_buildtypes
  error(
    'Unsupported buildtype \'@0@\'. Supported values: @1@'.format(
      buildtype,
      ', '.join(supported_buildtypes),
    ),
  )
endif

# Sanitizers use Meson's built-in -Db_sanitize=thread or
# -Db_sanitize=address,undefined (one value, so they are mutually exclusive).
if get_option('b_sanitize') != 'none' and buildtype != 'debugoptimized'
  error('-Db_sanitize requires --buildtype=debugoptimized')
endif

cds_args = []
# debugoptimized is the benchmark build: define NDEBUG and undo the standard
# library hardening Meson injects when b_ndebug is off, so the hot paths carry
# no extra checks. Tests re-enable asserts with -UNDEBUG.
if buildtype == 'debugoptimized'
  cds_args += ['-DNDEBUG', '-U_LIBCPP_HARDENING_MODE', '-U_GLIBCXX_ASSERTIONS']
endif
if get_option('cache_padding')
  cds_args += '-DENABLE_PADDING'
endif

# Header-only lib: depending on `cds_dep` picks up the include path, defines,
# and threads. Warnings and sanitizers apply project-wide via options.
cds_dep = declare_dependency(
  include_directories: include_directories('include'),
  compile_args: cds_args,
  dependencies: dependency('threads'),
)

if get_option('tests')
  subdir('tests')
endif

if get_option('benchmarks')
  subdir('benchmarks')
endif
