#ifndef _ASM_S390_SPARSEMEM_H
#define _ASM_S390_SPARSEMEM_H

#ifdef CONFIG_64BIT

#define SECTION_SIZE_BITS	28
#ifdef __GENKSYMS__
#define MAX_PHYSADDR_BITS	42
#define MAX_PHYSMEM_BITS	42
#else
#define MAX_PHYSADDR_BITS	46
#define MAX_PHYSMEM_BITS	46
#endif

#else

#define SECTION_SIZE_BITS	25
#define MAX_PHYSADDR_BITS	31
#define MAX_PHYSMEM_BITS	31

#endif /* CONFIG_64BIT */

#endif /* _ASM_S390_SPARSEMEM_H */