#undef LIB_SPEC
#define LIB_SPEC "%{pthread:-lpthread} -lc"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC "crt0.o%s crti.o%s crtbegin.o%s"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC "crtend.o%s crtn.o%s"

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()      \
  do {                                \
    builtin_define_std ("dailyrun");      \
    builtin_define_std ("unix");      \
    builtin_assert ("system=dailyrun");   \
    builtin_assert ("system=unix");   \
  } while(0);
