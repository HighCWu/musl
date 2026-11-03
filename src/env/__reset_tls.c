#include <string.h>
#include "pthread_impl.h"
#include "libc.h"

#ifdef __wasm__
hidden void __wasm_init_tls(void *);
#endif

void __reset_tls()
{
	pthread_t self = __pthread_self();
	struct tls_module *p;
	size_t i, n = self->dtv[0];
	if (n) for (p=libc.tls_head, i=1; i<=n; i++, p=p->next) {
		char *mem = (char *)(self->dtv[i] - DTP_OFFSET);
		memcpy(mem, p->image, p->len);
		memset(mem+p->len, 0, p->size - p->len);
	}
#ifdef __wasm__
	/* Native Wasm TLS is a separate linker-owned block, not an ELF DTV
	 * module. Reinitialize it when reusing a SIGEV_THREAD worker. */
	__wasm_init_tls((void *)__builtin_wasm_tls_base());
#endif
}
