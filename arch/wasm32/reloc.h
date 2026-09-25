#if __SIZEOF_POINTER__ == 4
#define LDSO_ARCH "wasm32"
#elif __SIZEOF_POINTER__ == 8
#define LDSO_ARCH "wasm64"
#else
#error unsupported wasm pointer ABI
#endif

#define CRTJMP(pc, sp) __builtin_trap()
