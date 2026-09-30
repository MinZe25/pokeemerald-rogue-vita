// Crash-compatibility helpers for the x86 PC port.
//
// 1) Integer division by zero: ARM (GBA, Vita) returns 0, x86 traps. The game
//    divides by stats/speeds that can be 0, so on x86 the faulting div is
//    emulated ARM-style (quotient and remainder 0) and execution continues.
//    Always installed (Linux and Windows).
//
// 2) Debug aid (32-bit Linux only): finds GBA code that reads or writes near
//    address 0. On the GBA that hits the BIOS / unmapped memory and is
//    harmless; on the Vita it crashes. With ROGUE_NULLTRAP=<log file> the low
//    64KB are mapped PROT_NONE; each access faults, gets logged (once per code
//    address, with a backtrace), then is allowed by opening the page for a
//    single step. Needs `sysctl vm.mmap_min_addr=0` (root).
#define _GNU_SOURCE // before any include: REG_EIP & co. in ucontext.h
#include <stdint.h>

#if defined(__i386__)
// Length of a DIV/IDIV instruction at ip, 0 if it isn't one.
// *byteOp: 8-bit form (AL/AH), *wordOp: 16-bit form (AX/DX)
static int X86DivLength(const uint8_t *ip, int *byteOp, int *wordOp)
{
    int n = 0, mod, rm, reg;

    *byteOp = *wordOp = 0;
    for (;; n++)
    {
        if (ip[n] == 0x66)
            *wordOp = 1;
        else if (!(ip[n] == 0x2E || ip[n] == 0x3E || ip[n] == 0x26 || ip[n] == 0x36 || ip[n] == 0x64 || ip[n] == 0x65))
            break;
    }
    if (ip[n] == 0xF6)
        *byteOp = 1;
    else if (ip[n] != 0xF7)
        return 0;
    n++;
    reg = (ip[n] >> 3) & 7;
    mod = ip[n] >> 6;
    rm = ip[n] & 7;
    n++;
    if (reg != 6 && reg != 7) // DIV, IDIV
        return 0;
    if (mod != 3 && rm == 4)
    {
        if (mod == 0 && (ip[n] & 7) == 5)
            n += 4;
        n++; // SIB
    }
    if (mod == 1)
        n += 1;
    else if (mod == 2 || (mod == 0 && rm == 5))
        n += 4;
    return n;
}

// Applies the ARM result (0) to eax/edx; returns false if not a division
static int EmulateDivByZero(uint32_t *eip, uint32_t *eax, uint32_t *edx)
{
    int byteOp, wordOp;
    int len = X86DivLength((const uint8_t *)(uintptr_t)*eip, &byteOp, &wordOp);

    if (len == 0)
        return 0;
    if (byteOp)
        *eax &= 0xFFFF0000; // AL, AH
    else if (wordOp)
    {
        *eax &= 0xFFFF0000;
        *edx &= 0xFFFF0000;
    }
    else
        *eax = *edx = 0;
    *eip += len;
    return 1;
}
#endif

#if defined(__linux__) && defined(__i386__)
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#define LOW_SIZE 0x10000
#define MAX_SITES 4096
#define EFLAGS_TF 0x100

static int sLogFd = -1;
static volatile uint8_t *sLow; // always NULL once mapped: that's the point
static int sArmed;
static uintptr_t sSites[MAX_SITES];
static int sNumSites;
static int sStepping;
static unsigned long sHits;

extern unsigned long NullTrap_CurrentFrame(void);
extern const char *NullTrap_CaseTag(void); // test case being run (src/pc_harness.c)

static void LogLine(const char *s)
{
    if (sLogFd >= 0 && write(sLogFd, s, strlen(s)) < 0)
        sLogFd = -1;
}

static void LogBacktrace(void)
{
    void *frames[24];
    int n;
    char buf[32];

    if (sLogFd < 0)
        return;
    n = backtrace(frames, 24);
    LogLine("  bt:");
    for (int i = 0; i < n; i++)
    {
        snprintf(buf, sizeof(buf), " %p", frames[i]);
        LogLine(buf);
    }
    LogLine("\n");
}

static int SeenSite(uintptr_t pc)
{
    for (int i = 0; i < sNumSites; i++)
        if (sSites[i] == pc)
            return 1;
    if (sNumSites < MAX_SITES)
        sSites[sNumSites++] = pc;
    return 0;
}

static void OnFault(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    uintptr_t addr = (uintptr_t)si->si_addr;
    uintptr_t pc = uc->uc_mcontext.gregs[REG_EIP];
    char buf[160];

    if (sig == SIGFPE)
    {
        uint32_t eip = uc->uc_mcontext.gregs[REG_EIP];
        uint32_t eax = uc->uc_mcontext.gregs[REG_EAX];
        uint32_t edx = uc->uc_mcontext.gregs[REG_EDX];

        if (EmulateDivByZero(&eip, &eax, &edx))
        {
            if (!SeenSite(pc))
            {
                snprintf(buf, sizeof(buf), "DIV0 pc=%p frame=%lu case=%s\n", (void *)pc, NullTrap_CurrentFrame(), NullTrap_CaseTag());
                LogLine(buf);
                LogBacktrace();
            }
            uc->uc_mcontext.gregs[REG_EIP] = eip;
            uc->uc_mcontext.gregs[REG_EAX] = eax;
            uc->uc_mcontext.gregs[REG_EDX] = edx;
            return;
        }
    }
    else if (sig == SIGSEGV && sArmed && addr < LOW_SIZE && !sStepping)
    {
        sHits++;
        if (!SeenSite(pc))
        {
            snprintf(buf, sizeof(buf), "NULL pc=%p addr=0x%lx frame=%lu case=%s\n", (void *)pc, (unsigned long)addr, NullTrap_CurrentFrame(), NullTrap_CaseTag());
            LogLine(buf);
            LogBacktrace();
        }
        mprotect((void *)sLow, LOW_SIZE, PROT_READ | PROT_WRITE);
        uc->uc_mcontext.gregs[REG_EFL] |= EFLAGS_TF;
        sStepping = 1;
        return;
    }

    // a real crash: record it, then die normally
    snprintf(buf, sizeof(buf), "CRASH sig=%d pc=%p addr=%p frame=%lu case=%s\n", sig, (void *)pc, (void *)addr, NullTrap_CurrentFrame(), NullTrap_CaseTag());
    LogLine(buf);
    LogBacktrace();
    signal(sig, SIG_DFL);
}

static void OnStep(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;

    (void)sig;
    (void)si;
    if (!sStepping)
        return;
    // writes must not leak into later reads: the GBA would read BIOS/open bus
    memset((void *)sLow, 0, LOW_SIZE);
    mprotect((void *)sLow, LOW_SIZE, PROT_NONE);
    uc->uc_mcontext.gregs[REG_EFL] &= ~EFLAGS_TF;
    sStepping = 0;
}

// free-form line in the log (test case markers)
void NullTrap_Note(const char *line)
{
    LogLine(line);
}

static void OnExit(void)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "END sites=%d hits=%lu frame=%lu\n", sNumSites, sHits, NullTrap_CurrentFrame());
    LogLine(buf);
}

void NullTrap_Init(void)
{
    const char *path = getenv("ROGUE_NULLTRAP");
    struct sigaction sa;
    void *probe[2];

    // division by zero emulation: always on
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = OnFault;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigaction(SIGFPE, &sa, NULL);

    if (path == NULL)
        return;
    sLogFd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (sLogFd < 0)
        return;
    backtrace(probe, 2); // loads libgcc now, not inside a signal handler
    atexit(OnExit);

    sLow = mmap(NULL, LOW_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    sArmed = (sLow == NULL);
    if (!sArmed)
        LogLine("mmap of page 0 failed (sysctl vm.mmap_min_addr=0?)\n");

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sa.sa_sigaction = OnStep;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGTRAP, &sa, NULL);
    LogLine(sArmed ? "nulltrap armed\n" : "div0/crash logging only\n");
}

#elif defined(_WIN32) && defined(__i386__)
#include <windows.h>

static LONG CALLBACK OnException(EXCEPTION_POINTERS *info)
{
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_INT_DIVIDE_BY_ZERO)
    {
        CONTEXT *ctx = info->ContextRecord;
        uint32_t eip = ctx->Eip, eax = ctx->Eax, edx = ctx->Edx;

        if (EmulateDivByZero(&eip, &eax, &edx))
        {
            ctx->Eip = eip;
            ctx->Eax = eax;
            ctx->Edx = edx;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void NullTrap_Init(void)
{
    AddVectoredExceptionHandler(1, OnException);
}

void NullTrap_Note(const char *line)
{
    (void)line;
}

#else
void NullTrap_Init(void)
{
}

void NullTrap_Note(const char *line)
{
    (void)line;
}
#endif
