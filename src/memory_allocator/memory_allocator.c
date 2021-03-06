#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <memory_allocator.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>



#define _ASSERT_STR_(x) #x
#define _ASSERT_STR(x) _ASSERT_STR_(x)
#define ASSERT(x) \
	do{ \
		if (!(x)){ \
			printf("File \"%s\", Line %u (%s): %s: Assertion Failed\n",__FILE__,__LINE__,__func__,_ASSERT_STR(x)); \
			raise(SIGABRT); \
		} \
	} while (0)

#define CORRECT_ALIGNMENT(n) ASSERT(!(((uint64_t)(n))&(ALLOCATOR_ALIGNMENT-1)))

#define ALIGN(a) ((((uint64_t)(a))+ALLOCATOR_ALIGNMENT-1)&(~(ALLOCATOR_ALIGNMENT-1)))

#define ALLOCATOR_ALIGNMENT 8

#define SIZE_MASK 0xfffffffffffffffe
#define FLAG_USED 1



typedef struct __PAGE_HEADER{
	void* p;
	uint64_t sz;
} page_header_t;



typedef struct __HEADER{
	uint64_t sz;
} header_t;



typedef struct __NODE{
	uint64_t sz;
	struct __NODE* p;
	struct __NODE* n;
} node_t;



typedef struct __ALLOCATOR{
	void* ptr;
	node_t* h;
} allocator_t;



uint64_t _pg_sz;
allocator_t a_dt;



void init_allocator(void){
#ifdef _MSC_VER
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	_pg_sz=si.dwPageSize;
#else
	_pg_sz=sysconf(_SC_PAGESIZE);
#endif
	a_dt.ptr=NULL;
	a_dt.h=NULL;
}



void* allocate(size_t sz){
	sz=ALIGN(sz)+sizeof(header_t);
	if (sz<sizeof(node_t)){
		sz=sizeof(node_t);
	}
	CORRECT_ALIGNMENT(sz);
	node_t* n=a_dt.h;
	while (n&&n->sz<sz){
		n=n->n;
	}
	if (!n){
		uint64_t pg_sz=(sz+sizeof(page_header_t)+sizeof(header_t)+_pg_sz-1)/_pg_sz*_pg_sz;
#ifdef _MSC_VER
		void* pg=VirtualAlloc(NULL,pg_sz,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
#else
		void* pg=mmap(NULL,pg_sz,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
#endif
		page_header_t* pg_h=(page_header_t*)pg;
		pg_h->p=a_dt.ptr;
		pg_h->sz=pg_sz;
		n=(node_t*)((uint64_t)pg+sizeof(page_header_t));
		CORRECT_ALIGNMENT(n);
		n->sz=pg_sz-sizeof(page_header_t)-sizeof(header_t);
		n->p=NULL;
		n->n=a_dt.h;
		if (n->n){
			n->n->p=n;
		}
		((header_t*)((uint64_t)pg+pg_sz-sizeof(header_t)))->sz=FLAG_USED;
		a_dt.ptr=pg;
		a_dt.h=n;
	}
	if (n->sz-sz>=sizeof(node_t)){
		node_t* nn=(node_t*)((uint64_t)n+sz);
		CORRECT_ALIGNMENT(nn);
		nn->sz=n->sz-sz;
		nn->p=n->p;
		nn->n=n->n;
		if (!nn->p){
			a_dt.h=nn;
		}
		else{
			nn->p->n=nn;
		}
		if (nn->n){
			nn->n->p=nn;
		}
	}
	else{
		sz=n->sz;
		CORRECT_ALIGNMENT(sz);
		if (!n->p){
			a_dt.h=n->n;
		}
		else{
			n->p->n=n->n;
		}
		if (n->n){
			n->n->p=n->p;
		}
	}
	header_t* h=(header_t*)n;
	h->sz=sz|FLAG_USED;
	return (void*)((uint64_t)h+sizeof(header_t));
}



void deallocate(void* p){
	node_t* n=(node_t*)((uint64_t)p-sizeof(header_t));
	n->sz&=SIZE_MASK;
	node_t* nn=(node_t*)((uint64_t)n+n->sz);
	if (nn->sz&FLAG_USED){
		n->p=NULL;
		n->n=a_dt.h;
		a_dt.h=n;
	}
	else{
		n->sz+=nn->sz;
		n->p=nn->p;
		n->n=nn->n;
		if (!n->p){
			a_dt.h=n;
		}
		else{
			n->p->n=n;
		}
	}
	if (n->n){
		n->n->p=n;
	}
}



void deinit_allocator(void){
	void* c=a_dt.ptr;
	while (c){
		page_header_t* h=(page_header_t*)c;
		void* n=h->p;
#ifdef _MSC_VER
		VirtualFree(c,0,MEM_RELEASE);
#else
		munmap(c,h->sz);
#endif
		c=n;
	}
}
