CC = clang
WASM_BITS ?= 32
LDSO_ARCH = wasm$(WASM_BITS)
CFLAGS += --target=wasm$(WASM_BITS) -matomics -mbulk-memory
LDFLAGS =
CROSS_COMPILE = llvm-
LIBCC =

# longjmp is implemented with LLVM's Wasm SjLj lowering. __wasm_longjmp uses
# __builtin_wasm_throw, and any musl translation unit that itself calls longjmp
# (siglongjmp) must be compiled with the pass enabled so the call is rewritten.
# Scoped to these objects so the rest of libc stays free of the Wasm
# exception-handling feature; a program requires it when using SjLj or cancellation.
#
# wasm_longjmp.c, siglongjmp.c and the existing cancellation implementation
# (which now contains a cancellation checkpoint) go through the pass.
# setjmp.c and longjmp.c are excluded: the
# setjmp/longjmp helpers are plain memory writes, and running the pass over the
# real setjmp/longjmp definitions rejects the _setjmp/_longjmp weak aliases as
# "indirect use of setjmp" or collides with the __wasm_longjmp symbol.
WASM_SJLJ_OBJS = \
	obj/src/setjmp/wasm32/wasm_longjmp.o obj/src/setjmp/wasm32/wasm_longjmp.lo \
	obj/src/signal/wasm32/siglongjmp.o obj/src/signal/wasm32/siglongjmp.lo \
	obj/src/thread/pthread_cancel.o obj/src/thread/pthread_cancel.lo
$(WASM_SJLJ_OBJS): CFLAGS_ALL += -mllvm -wasm-enable-sjlj
