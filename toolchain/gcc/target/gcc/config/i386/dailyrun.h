#undef LIB_SPEC
#define LIB_SPEC "%{pthread:-lpthread} -lc"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC "crt0.o%s crti.o%s crtbegin.o%s"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC "crtend.o%s crtn.o%s"

#define NATIVE_SYSTEM_WRAPPER_HEADER_DIR "/usr/lib/dailyrun/include"

#define INCLUDE_DEFAULTS                                     \
    {                                                        \
        { GCC_INCLUDE_DIR, "GCC", 0, 0, 0, 0 },              \
        { LOCAL_INCLUDE_DIR, 0, 0, 1, 1, 2 },                \
        { LOCAL_INCLUDE_DIR, 0, 0, 1, 1, 0 },                \
        { NATIVE_SYSTEM_WRAPPER_HEADER_DIR, 0, 0, 0, 1, 2 }, \
        { NATIVE_SYSTEM_WRAPPER_HEADER_DIR, 0, 0, 0, 1, 0 }, \
        { NATIVE_SYSTEM_HEADER_DIR, 0, 0, 0, 1, 2 },         \
        { NATIVE_SYSTEM_HEADER_DIR, 0, 0, 0, 1, 0 },         \
        { FIXED_INCLUDE_DIR, "GCC", 0, 0, 0, 0 },            \
        { 0, 0, 0, 0, 0, 0 },                                \
    }

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()      \
  do {                                \
    builtin_define_std ("dailyrun");      \
    builtin_define_std ("unix");      \
    builtin_assert ("system=dailyrun");   \
    builtin_assert ("system=unix");   \
  } while(0);
