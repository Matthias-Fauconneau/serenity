/* Derived by Jeff Muizelaar from addr2line.c in GNU Binutils
   Copyright 1997, 1998, 1999, 2000, 2001, 2002 Free Software Foundation, Inc.
   Contributed by Ulrich Lauther <Ulrich.Lauther@mchp.siemens.de>
   Under the terms of the GPL v2 or later
*/

#include "common.h"
#include <execinfo.h>
#include <bfd.h>
#include <cxxabi.h>

void logBacktrace() {
	void* buffer[64];
	int size = backtrace(buffer, 64);
	bfd_init();
	bfd* abfd = bfd_openr("/proc/self/exe",0);
	assert(!bfd_check_format(abfd, bfd_archive));
	char** matching; assert(bfd_check_format_matches(abfd, bfd_object, &matching));
	void* syms=0;
	if ((bfd_get_file_flags(abfd) & HAS_SYMS) != 0) {
		unsigned int size=0;
		long symcount = bfd_read_minisymbols(abfd, false, &syms, &size);
		if(symcount == 0) symcount = bfd_read_minisymbols(abfd, true, &syms, &size);
		assert(symcount >= 0);
	}
	for(int i=size-4; i>0; i--) {
		bfd_vma addr = (bfd_vma)buffer[i];
		for(bfd_section* s=abfd->sections;s;s=s->next) {
			const char* path=0; const char* function=0; unsigned int line=0;
			if(s->flags & SEC_ALLOC && addr >= s->vma && addr < s->vma + s->size &&
			   bfd_find_nearest_line(abfd, s, (bfd_symbol**)syms, addr - s->vma, &path, &function, &line)) {
				string file;
				if(path) file=section(string(path,(int)strlen(path)),'/',-2,-1);
				if(file.startsWith(_("functional"))) continue;

				const char* name = function;
				char* demangle = abi::__cxa_demangle(name,0,0,0);
				if(demangle) name=demangle;

				log_(file);log_(':');log_(line);log_(_("   \t"));log(name);
				if(demangle) free(demangle);
				break;
			}
		}
	}
	if(syms) free(syms);
	bfd_close(abfd);
}
