# Try to find gtest, fall back to subproject if not found.
gtest_dep = dependency('gtest',
  main : true,
  fallback : ['gtest', 'gtest_main_dep'],
  required : get_option('build_tests'))

if gtest_dep.found()
  # Most tests live next to their sources in `src/`; a few standalone ones
  # live here in `tests/`.
  simpletiff_tests = {
    'index_test' : '../src/index_test.cpp',
    'lzw_test' : '../src/lzw_test.cpp',
    'deflate_test' : '../src/deflate_test.cpp',
    'predictor_test' : '../src/predictor_test.cpp',
    'reader_test' : '../src/reader_test.cpp',
    'ccitt_test' : '../src/ccitt_test.cpp',
    'jpeg2000_sycc_pack_test' : 'jpeg2000_sycc_pack_test.cpp',
    'jpeg_decode_mode_test' : 'jpeg_decode_mode_test.cpp',
  }

  test_args = []
  if get_option('jpeg_decoder') == 'jpgd'
    test_args += '-DSIMPLETIFF_JPEG_BACKEND_JPGD'
  endif

  foreach test_name, test_src : simpletiff_tests
    exe = executable(test_name, test_src,
      cpp_args : test_args,
      dependencies : [simpletiff_dep, gtest_dep])
    test(test_name, exe)
  endforeach
endif
