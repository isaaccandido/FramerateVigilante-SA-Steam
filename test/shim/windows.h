// Minimal Win32 shim so src/main.c can run inside a 32-bit Linux test harness.
#pragma once
#include <stdint.h>
#include <stdarg.h>
#include <sys/mman.h>

typedef void *HMODULE, *HINSTANCE, *LPVOID, *HANDLE;
typedef uint32_t DWORD;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define WINAPI
#define __cdecl __attribute__((cdecl))
#define MAX_PATH 260
#define DLL_PROCESS_ATTACH 1
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define PAGE_EXECUTE_READWRITE 0x40

extern uintptr_t shim_imageBase;

static inline HMODULE GetModuleHandleA(const char *n) { (void)n; return (HMODULE)shim_imageBase; }
static inline DWORD GetModuleFileNameA(HMODULE m, char *buf, DWORD n) { (void)m; const char *p = ".\\FramerateVigilanteSteam.asi"; DWORD i = 0; for (; p[i] && i + 1 < n; i++) buf[i] = p[i]; buf[i] = 0; return i; }
static inline unsigned GetPrivateProfileIntA(const char *s, const char *k, int d, const char *f) { (void)s; (void)k; (void)f; return (unsigned)d; }
static inline void *VirtualAlloc(void *a, size_t n, DWORD t, DWORD p) { (void)a; (void)t; (void)p; void *r = mmap(0, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); return r == MAP_FAILED ? 0 : r; }
static inline BOOL VirtualProtect(void *a, size_t n, DWORD p, DWORD *o) { (void)a; (void)n; (void)p; *o = PAGE_EXECUTE_READWRITE; return TRUE; }
#include <strings.h>
#define _stricmp strcasecmp
static inline HANDLE GetCurrentProcess(void) { return 0; }
static inline BOOL FlushInstructionCache(HANDLE h, const void *a, size_t n) { (void)h; (void)a; (void)n; return TRUE; }
