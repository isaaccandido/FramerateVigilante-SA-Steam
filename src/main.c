// Framerate Vigilante SA Steam - https://github.com/isaaccandido/FramerateVigilante-SA-Steam
// Port of Junior_Djjr's Framerate Vigilante (https://github.com/GTAmodding/FramerateVigilante)
// to the Steam/Rockstar Launcher gta-sa.exe. Every patch verifies the original bytes first
// and is skipped (and logged) on mismatch, so a wrong address is a no-op instead of a crash.

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PREFERRED_BASE 0x400000u
#define VERSION_TAG "1.0.1"

static uintptr_t g_base;
static FILE *g_log;
static char g_dir[MAX_PATH];

// 1.0 timestep at 30 FPS is 50/30; scaling by (timestep / normalizer) leaves 30 FPS behavior unchanged.
static const float k_timestepToUnit = 30.0f / 50.0f;
static const float k_one = 1.0f;
static const float k_doorLossFirelaLadder = 0.08f; // 1 - 0.92
static const float k_doorLossOther = 0.03f;        // 1 - 0.97

// ================================================================ utilities

static uintptr_t va(uint32_t preferredVa) { return g_base + (preferredVa - PREFERRED_BASE); }

static void logf_(const char *fmt, ...)
{
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static void resolveModuleDir(HMODULE self)
{
    GetModuleFileNameA(self, g_dir, MAX_PATH);
    char *slash = strrchr(g_dir, '\\');
    if (slash) slash[1] = '\0';
}

static void openLog(void)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sFramerateVigilanteSteam.log", g_dir);
    g_log = fopen(path, "w");
}

static int readIni(const char *section, const char *key, int def)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sFramerateVigilanteSteam.ini", g_dir);
    return (int)GetPrivateProfileIntA(section, key, def, path);
}

static int bytesMatch(uintptr_t addr, const uint8_t *expected, size_t n)
{
    return memcmp((const void *)addr, expected, n) == 0;
}

static uint32_t read32(uintptr_t addr) { return *(const uint32_t *)addr; }

static void writeBytes(uintptr_t addr, const void *data, size_t n)
{
    DWORD old;
    VirtualProtect((void *)addr, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)addr, data, n);
    VirtualProtect((void *)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)addr, n);
}

// ================================================================ stub emitter

typedef struct { uint8_t *p; } Emitter;

#define STUB_POOL_SIZE 0x10000u // one allocation-granularity block; all stubs use well under this

static uint8_t *g_stubPool;
static size_t g_stubUsed;

static int allocStubPool(void)
{
    g_stubPool = VirtualAlloc(NULL, STUB_POOL_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    return g_stubPool != NULL;
}

static Emitter stubBegin(void)
{
    Emitter e = {g_stubPool + g_stubUsed};
    return e;
}

static void stubEnd(const Emitter *e, const uint8_t *start) { g_stubUsed += (size_t)(e->p - start); g_stubUsed = (g_stubUsed + 15) & ~(size_t)15; }

static void emitBytes(Emitter *e, const void *b, size_t n) { memcpy(e->p, b, n); e->p += n; }
static void emit8(Emitter *e, uint8_t b) { *e->p++ = b; }
static void emit32(Emitter *e, uint32_t v) { memcpy(e->p, &v, 4); e->p += 4; }
static void emitRel32To(Emitter *e, uintptr_t target) { int32_t rel = (int32_t)(target - ((uintptr_t)e->p + 4)); memcpy(e->p, &rel, 4); e->p += 4; }

static void emitFldMem(Emitter *e, const void *f)   { emit8(e, 0xD9); emit8(e, 0x05); emit32(e, (uint32_t)(uintptr_t)f); }
static void emitFmulMem(Emitter *e, const void *f)  { emit8(e, 0xD8); emit8(e, 0x0D); emit32(e, (uint32_t)(uintptr_t)f); }
static void emitFsubrMem(Emitter *e, const void *f) { emit8(e, 0xD8); emit8(e, 0x2D); emit32(e, (uint32_t)(uintptr_t)f); }
static void emitFdivMem(Emitter *e, const void *f)  { emit8(e, 0xD8); emit8(e, 0x35); emit32(e, (uint32_t)(uintptr_t)f); }
static void emitFmulpSt1(Emitter *e) { emit8(e, 0xDE); emit8(e, 0xC9); }
static void emitFaddpSt1(Emitter *e) { emit8(e, 0xDE); emit8(e, 0xC1); }
static void emitJmp(Emitter *e, uintptr_t target) { emit8(e, 0xE9); emitRel32To(e, target); }
static void emitCall(Emitter *e, uintptr_t target) { emit8(e, 0xE8); emitRel32To(e, target); }

// fld/fstp dword ptr [ebp+disp8]
static void emitFldEbp(Emitter *e, int8_t disp)  { emit8(e, 0xD9); emit8(e, 0x45); emit8(e, (uint8_t)disp); }
static void emitFstpEbp(Emitter *e, int8_t disp) { emit8(e, 0xD9); emit8(e, 0x5D); emit8(e, (uint8_t)disp); }

// st0 *= ms_fTimeStep * (30/50)
static void emitScaleByTimestep(Emitter *e)
{
    emitFmulMem(e, (const void *)va(0xC0FD50));
    emitFmulMem(e, &k_timestepToUnit);
}

// st0 /= ms_fTimeStep * (30/50)
static void emitUnscaleByTimestep(Emitter *e)
{
    emitFdivMem(e, (const void *)va(0xC0FD50));
    emitFdivMem(e, &k_timestepToUnit);
}

// [ebp+disp] = [ebp+disp] * timestep-scale (or divided, when `invert`)
static void emitScaleEbpLocal(Emitter *e, int8_t disp, int invert)
{
    emitFldEbp(e, disp);
    if (invert) emitUnscaleByTimestep(e); else emitScaleByTimestep(e);
    emitFstpEbp(e, disp);
}

// pushes (1 - ms_fTimeStep * (30/50) * loss)
static void emitDampingFactor(Emitter *e, const float *loss)
{
    emitFldMem(e, (const void *)va(0xC0FD50));
    emitFmulMem(e, &k_timestepToUnit);
    emitFmulMem(e, loss);
    emitFsubrMem(e, &k_one);
}

// Overwrite `len` (>=5) bytes at site with jmp stub + nops.
static void redirect(uintptr_t site, size_t len, const uint8_t *stub)
{
    uint8_t patch[16];
    patch[0] = 0xE9;
    int32_t rel = (int32_t)((uintptr_t)stub - (site + 5));
    memcpy(patch + 1, &rel, 4);
    memset(patch + 5, 0x90, len - 5);
    writeBytes(site, patch, len);
}

// ================================================================ game addresses (Steam, preferred base)

enum {
    TIMESTEP            = 0xC0FD50, // CTimer::ms_fTimeStep
    TIMER_TIME_MS             = 0xC0FD74, // CTimer::m_snTimeInMilliseconds
    TIMER_OLDSTEP       = 0xC0FD44,
    TIMER_UPD_FSTP      = 0x57A993, // fstp [TIMER_OLDSTEP] in CTimer::Update (relocation sanity check)
    PADS                = 0xC007E8, // CPad::Pads[]
    PLAYERS             = 0xC100D0, // CWorld::Players[]
    PAD_GET_HORN        = 0x5508C0, // CPad::GetHorn
    PAD_HORN_JUST_DOWN  = 0x550910, // CPad::HornJustDown
    HANDLING_SLOWDOWN   = 0xCACA14, // handling manager float (0.9) used by the car slowdown code
    RSGLOBAL_FRAMELIMIT = 0xCA3DB8, // RsGlobal.frameLimit
    ACTIVE_SCRIPTS      = 0xB02D98, // CTheScripts::pActiveScripts
    CUTSCENE_RUNNING    = 0xBD6F3D, // CCutsceneMgr::ms_running
    WIDESCREEN_ON       = 0xBFC485, // TheCamera.m_bWideScreenOn
    CURR_AREA           = 0xBFFBE4, // CGame::currArea
    IDLE_TIMER_UPDATE   = 0x54ED0C, // call CTimer::Update inside Idle (per-frame hook)
    TIMER_UPDATE        = 0x57A920, // CTimer::Update
};

enum { PAD_SIZE = 0x134, PLAYERINFO_SIZE = 0x190, VEHICLE_DRIVER = 0x460 };

// ================================================================ sanity

static int relocationArithmeticOk(void)
{
    static const uint8_t op[] = {0xD9, 0x1D};
    uintptr_t at = va(TIMER_UPD_FSTP);
    return bytesMatch(at, op, 2) && read32(at + 2) == va(TIMER_OLDSTEP);
}

// Expected bytes: opcode prefix followed by a relocated absolute address.
static int matchesAbs(uintptr_t at, const uint8_t *op, size_t opLen, uint32_t preferredTarget)
{
    return bytesMatch(at, op, opLen) && read32(at + opLen) == va(preferredTarget);
}

// ================================================================ frame limiter, wheels

static int patchFpsLimit(int fps)
{
    static const uint8_t op[] = {0xC7, 0x05};
    static const uint8_t imm30[] = {0x1E, 0x00, 0x00, 0x00};
    uintptr_t at = va(0x637480);
    if (!matchesAbs(at, op, 2, RSGLOBAL_FRAMELIMIT) || !bytesMatch(at + 6, imm30, 4)) return 0;

    uint32_t value = (uint32_t)fps;
    writeBytes(at + 6, &value, 4);
    *(uint32_t *)(uintptr_t)read32(at + 2) = value;
    return 1;
}

static int patchFrameDelay(void)
{
    static const uint8_t op[] = {0x3B, 0x05};
    static const uint8_t repl[] = {0x83, 0xF8, 0x00, 0x90, 0x90, 0x90};
    uintptr_t at = va(0x54ECEB);
    if (!matchesAbs(at, op, 2, 0x93FD78)) return 0;
    writeBytes(at, repl, sizeof repl);
    return 1;
}

static int patchWheelSpin(uint32_t siteVa, uint32_t fieldOffset)
{
    uint8_t orig[6] = {0xD8, 0x86};
    memcpy(orig + 2, &fieldOffset, 4);
    uintptr_t site = va(siteVa);
    if (!bytesMatch(site, orig, 6)) return 0;

    Emitter e = stubBegin(); uint8_t *start = e.p;
    emitFmulMem(&e, (const void *)va(TIMESTEP)); // original fix: multiply by raw timestep
    emitBytes(&e, orig, 6);
    emitJmp(&e, site + 6);
    stubEnd(&e, start);
    redirect(site, 6, start);
    return 1;
}

// ================================================================ doors / swing components

// fadd [esi+14h] ; fstp [esi+14h]   (st0 = force)  ->  force * timestep-scale, then add
static int patchDoorForce(uint32_t siteVa)
{
    static const uint8_t orig[] = {0xD8, 0x46, 0x14, 0xD9, 0x5E, 0x14};
    uintptr_t site = va(siteVa);
    if (!bytesMatch(site, orig, sizeof orig)) return 0;

    Emitter e = stubBegin(); uint8_t *start = e.p;
    emitScaleByTimestep(&e);
    emitBytes(&e, orig, sizeof orig);
    emitJmp(&e, site + sizeof orig);
    stubEnd(&e, start);
    redirect(site, sizeof orig, start);
    return 1;
}

// fmul qword [0.92]  (firela ladder)  ->  st0 *= (1 - ts*k*0.08)
static int patchDoorDampingFirela(void)
{
    static const uint8_t op[] = {0xDC, 0x0D};
    uintptr_t site = va(0x6F5E1E);
    if (!matchesAbs(site, op, 2, 0x8ADC30)) return 0;

    Emitter e = stubBegin(); uint8_t *start = e.p;
    emitDampingFactor(&e, &k_doorLossFirelaLadder);
    emitFmulpSt1(&e);
    emitJmp(&e, site + 6);
    stubEnd(&e, start);
    redirect(site, 6, start);
    return 1;
}

// fmul qword [0.97]  -> only for components with flag 0x20 (the case the original mod fixes)
static int patchDoorDampingFlagged(void)
{
    static const uint8_t op[] = {0xDC, 0x0D};
    uintptr_t site = va(0x6F5E40);
    if (!matchesAbs(site, op, 2, 0x8B1E50)) return 0;
    uint8_t orig[6];
    memcpy(orig, (const void *)site, 6);

    Emitter e = stubBegin(); uint8_t *start = e.p;
    static const uint8_t testFlag[] = {0xF6, 0x46, 0x08, 0x20};      // test byte ptr [esi+8], 20h
    emitBytes(&e, testFlag, sizeof testFlag);
    emit8(&e, 0x74); uint8_t *jzRel = e.p; emit8(&e, 0);            // jz original
    emitDampingFactor(&e, &k_doorLossOther);
    emitFmulpSt1(&e);
    emitJmp(&e, site + 6);
    *jzRel = (uint8_t)(e.p - (jzRel + 1));
    emitBytes(&e, orig, 6);                                          // original fmul (relocated operand)
    emitJmp(&e, site + 6);
    stubEnd(&e, start);
    redirect(site, 6, start);
    return 1;
}

// fadd [esi+14h] ; mov ecx, edx   ->  angle += angVel * timestep-scale
static int patchDoorAngleIntegrate(void)
{
    static const uint8_t orig[] = {0xD8, 0x46, 0x14, 0x8B, 0xCA};
    uintptr_t site = va(0x6F5EAD);
    if (!bytesMatch(site, orig, sizeof orig)) return 0;

    Emitter e = stubBegin(); uint8_t *start = e.p;
    static const uint8_t fldAngVel[] = {0xD9, 0x46, 0x14};
    emitBytes(&e, fldAngVel, sizeof fldAngVel);
    emitScaleByTimestep(&e);
    emitFaddpSt1(&e);
    emit8(&e, 0x8B); emit8(&e, 0xCA);                                 // mov ecx, edx
    emitJmp(&e, site + sizeof orig);
    stubEnd(&e, start);
    redirect(site, sizeof orig, start);
    return 1;
}

// ================================================================ vehicles

// fdivr qword [3000]  (st0 = 3000 / mass)  ->  then scale by timestep
static int patchBurnout(void)
{
    static const uint8_t op[] = {0xDC, 0x3D};
    uintptr_t site = va(0x6D2DB4);
    if (!matchesAbs(site, op, 2, 0x8A32B0)) return 0;
    uint8_t orig[6];
    memcpy(orig, (const void *)site, 6);

    Emitter e = stubBegin(); uint8_t *start = e.p;
    emitBytes(&e, orig, 6);
    emitScaleByTimestep(&e);
    emitJmp(&e, site + 6);
    stubEnd(&e, start);
    redirect(site, 6, start);
    return 1;
}

// fld dword [handlingSlowdown]  ->  fld [handlingSlowdown] * timestep-scale (x87 only: flags/eax untouched)
static int patchCarSlowdown(uint32_t siteVa)
{
    static const uint8_t op[] = {0xD9, 0x05};
    uintptr_t site = va(siteVa);
    if (!matchesAbs(site, op, 2, HANDLING_SLOWDOWN)) return 0;
    uint8_t orig[6];
    memcpy(orig, (const void *)site, 6);

    Emitter e = stubBegin(); uint8_t *start = e.p;
    emitBytes(&e, orig, 6);
    emitScaleByTimestep(&e);
    emitJmp(&e, site + 6);
    stubEnd(&e, start);
    redirect(site, 6, start);
    return 1;
}

// ================================================================ sirens

typedef int8_t (__attribute__((thiscall)) *PadQuery)(void *pad);

static uintptr_t g_sirenToggle, g_sirenHorn, g_sirenNoHorn;
static uint32_t g_hornPressTime;
static int g_hornHasPressed, g_hornJustUp;

static void *padForVehicle(const uint8_t *vehicle)
{
    void *driver = *(void *const *)(vehicle + VEHICLE_DRIVER);
    void *player1 = *(void *const *)va(PLAYERS);
    return (void *)(va(PADS) + (driver == player1 ? 0 : PAD_SIZE));
}

static void updateHornState(void *pad, uint32_t now, int horn)
{
    if (((PadQuery)va(PAD_HORN_JUST_DOWN))(pad)) {
        g_hornPressTime = now;
        g_hornHasPressed = 1;
    }
    g_hornJustUp = !horn && g_hornHasPressed;
}

// Called from the stub with the vehicle; returns where to resume.
static uintptr_t __cdecl sirenDecide(const uint8_t *vehicle)
{
    void *pad = padForVehicle(vehicle);
    uint32_t now = *(const uint32_t *)va(TIMER_TIME_MS);
    int horn = ((PadQuery)va(PAD_GET_HORN))(pad) != 0;
    updateHornState(pad, now, horn);

    if (horn && now - g_hornPressTime >= 150) return g_sirenHorn;
    if (g_hornJustUp && now - g_hornPressTime < 150) {
        g_hornJustUp = 0;
        g_hornHasPressed = 0;
        return g_sirenToggle;
    }
    return g_sirenNoHorn;
}

static int sirenTargetsOk(void)
{
    static const uint8_t toggle[] = {0x8A, 0x86, 0x2D, 0x04, 0x00, 0x00};            // mov al,[esi+42Dh]
    static const uint8_t horn[] = {0x5F, 0xC7, 0x86, 0x14, 0x05, 0x00, 0x00, 0x01};  // pop edi; mov [esi+514h],1
    static const uint8_t none[] = {0x5F, 0xC7, 0x86, 0x14, 0x05, 0x00, 0x00, 0x00};  // pop edi; mov [esi+514h],0
    return bytesMatch(g_sirenToggle, toggle, sizeof toggle) && bytesMatch(g_sirenHorn, horn, sizeof horn) &&
           bytesMatch(g_sirenNoHorn, none, sizeof none);
}

// movzx ecx, byte [hornHistoryIndex]  ->  decide in C, push edi (Steam pushes it after this point), jump
static int patchSiren(void)
{
    static const uint8_t op[] = {0x0F, 0xB6, 0x0D};
    uintptr_t site = va(0x71ABD1);
    g_sirenToggle = va(0x71AC07);
    g_sirenHorn = va(0x71AC51);
    g_sirenNoHorn = va(0x71AC61);
    if (!matchesAbs(site, op, 3, PADS + 0x116) || !sirenTargetsOk()) return 0;
    if (!bytesMatch(va(PAD_GET_HORN), (const uint8_t *)"\x66\x83\xB9\x0E\x01\x00\x00\x00", 8)) return 0;
    if (!bytesMatch(va(PAD_HORN_JUST_DOWN), (const uint8_t *)"\x66\x83\xB9\x0E\x01\x00\x00\x00", 8)) return 0;

    Emitter e = stubBegin(); uint8_t *start = e.p;
    emit8(&e, 0x60);                                  // pushad
    emit8(&e, 0x56);                                  // push esi (vehicle)
    emit8(&e, 0xE8); emitRel32To(&e, (uintptr_t)sirenDecide);
    static const uint8_t tail[] = {0x83, 0xC4, 0x04,  // add esp, 4
                                   0x89, 0x44, 0x24, 0x1C, // mov [esp+1Ch], eax (saved eax)
                                   0x61,              // popad
                                   0x57,              // push edi
                                   0xFF, 0xE0};       // jmp eax
    emitBytes(&e, tail, sizeof tail);
    stubEnd(&e, start);
    redirect(site, 7, start);
    return 1;
}

// ================================================================ swimming

// Patch a 6-byte "op qword [constPreferredVa]" by re-executing it in a stub, then running `after`.
typedef void (*StubTail)(Emitter *e);

static int patchConstOp(uint32_t siteVa, uint8_t op0, uint8_t op1, uint32_t constVa, StubTail after)
{
    const uint8_t op[] = {op0, op1};
    uintptr_t site = va(siteVa);
    if (!matchesAbs(site, op, 2, constVa)) return 0;
    uint8_t orig[6];
    memcpy(orig, (const void *)site, 6);

    Emitter e = stubBegin(); uint8_t *start = e.p;
    emitBytes(&e, orig, 6);
    after(&e);
    emitJmp(&e, site + 6);
    stubEnd(&e, start);
    redirect(site, 6, start);
    return 1;
}

// fmul qword [-0.1]  ->  * timestep-scale
static int patchDive(void) { return patchConstOp(0x6B740D, 0xDC, 0x0D, 0x8A8CC8, emitScaleByTimestep); }

// fadd qword [0.01]  ->  then / timestep-scale
static int patchDiveSurface(void) { return patchConstOp(0x6B74E8, 0xDC, 0x05, 0x8A1EF0, emitUnscaleByTimestep); }

// Before "fld [esi+44h]; fmul st(1)": scale the blended swim X/Y targets by 1 / timestep-scale.
// (The original also rescales Z, but in 1.0 that value was already consumed, so only X/Y take effect.)
static int patchSwimSpeed(void)
{
    static const uint8_t orig[] = {0xD9, 0x46, 0x44, 0xD8, 0xC9};
    uintptr_t site = va(0x6B7534);
    if (!bytesMatch(site, orig, sizeof orig)) return 0;

    Emitter e = stubBegin(); uint8_t *start = e.p;
    emitScaleEbpLocal(&e, -0x20, 1);
    emitScaleEbpLocal(&e, -0x1C, 1);
    emitBytes(&e, orig, sizeof orig);
    emitJmp(&e, site + sizeof orig);
    stubEnd(&e, start);
    redirect(site, sizeof orig, start);
    return 1;
}

static const float k_onePointFive = 1.5f;

// fmul [ebp+0Ch] (clamped timestep) ; fstp [ebp+0Ch]
// For the player ped: multiply by (1 + s/1.5) * s, s = timestep * 0.6. Everything else unchanged.
static int patchBuoyancy(void)
{
    static const uint8_t orig[] = {0xD8, 0x4D, 0x0C, 0xD9, 0x5D, 0x0C};
    uintptr_t site = va(0x6F6BD4);
    if (!bytesMatch(site, orig, sizeof orig)) return 0;

    Emitter e = stubBegin(); uint8_t *start = e.p;
    static const uint8_t pedCheck[] = {
        0x52,                               // push edx
        0x0F, 0xB6, 0x50, 0x36,             // movzx edx, byte [eax+36h]  (entity flags)
        0x83, 0xE2, 0x07,                   // and edx, 7                (m_nType)
        0x83, 0xFA, 0x03,                   // cmp edx, 3                (ENTITY_TYPE_PED)
        0x75, 0x00,                         // jne normal (patched below)
        0x83, 0xB8, 0x98, 0x05, 0x00, 0x00, 0x01, // cmp dword [eax+598h], 1 (m_nPedType: 0/1 = player)
        0x77, 0x00,                         // ja normal (patched below)
    };
    emitBytes(&e, pedCheck, sizeof pedCheck);
    uint8_t *jne = e.p - 10, *ja = e.p - 1;

    emitFldMem(&e, (const void *)va(TIMESTEP));
    emitFmulMem(&e, &k_timestepToUnit);                     // s
    emit8(&e, 0xD9); emit8(&e, 0xC0);                        // fld st(0)
    emitFdivMem(&e, &k_onePointFive);
    emit8(&e, 0xD8); emit8(&e, 0x05); emit32(&e, (uint32_t)(uintptr_t)&k_one); // fadd [1.0]
    emitFmulpSt1(&e);                                        // (1 + s/1.5) * s
    emitFmulpSt1(&e);                                        // value * factor
    emit8(&e, 0xEB); uint8_t *jmpTail = e.p; emit8(&e, 0);

    uint8_t *normal = e.p;
    *jne = (uint8_t)(normal - (jne + 1));
    *ja = (uint8_t)(normal - (ja + 1));
    emitBytes(&e, orig, 3);                                  // fmul [ebp+0Ch]

    *jmpTail = (uint8_t)(e.p - (jmpTail + 1));
    emit8(&e, 0x5A);                                         // pop edx
    emitBytes(&e, orig + 3, 3);                              // fstp [ebp+0Ch]
    emitJmp(&e, site + sizeof orig);
    stubEnd(&e, start);
    redirect(site, sizeof orig, start);
    return 1;
}

// ================================================================ vehicles / peds

static const float k_rotorRef = 220.0f;
static const float k_three = 3.0f;
static uint32_t g_rotorMaxPtrSlot; // code bytes holding &rotorMaxSpeed (double), read live for MixSets compatibility

// fadd qword [inc]  ->  fadd (rotorMax / 220 * mult) * timestep-scale
static int patchRotor(uint32_t siteVa, uint32_t incConstVa, int triple)
{
    static const uint8_t cmpOp[] = {0xDC, 0x1D};
    static const uint8_t addOp[] = {0xDC, 0x05};
    uintptr_t cmpAt = va(0x6F97AA), site = va(siteVa);
    if (!matchesAbs(cmpAt, cmpOp, 2, 0x8AD650) || !matchesAbs(site, addOp, 2, incConstVa)) return 0;
    g_rotorMaxPtrSlot = (uint32_t)(cmpAt + 2);

    Emitter e = stubBegin(); uint8_t *start = e.p;
    emit8(&e, 0x50);                                   // push eax
    emit8(&e, 0xA1); emit32(&e, g_rotorMaxPtrSlot);    // mov eax, [slot]
    emit8(&e, 0xDD); emit8(&e, 0x00);                  // fld qword [eax]
    emit8(&e, 0x58);                                   // pop eax
    emitFdivMem(&e, &k_rotorRef);
    if (triple) emitFmulMem(&e, &k_three);
    emitScaleByTimestep(&e);
    emitFaddpSt1(&e);
    emitJmp(&e, site + 6);
    stubEnd(&e, start);
    redirect(site, 6, start);
    return 1;
}

// fmul qword [30]  ->  * timestep-scale
static int patchSkimmer(void) { return patchConstOp(0x70C8ED, 0xDC, 0x0D, 0x8A1D48, emitScaleByTimestep); }

// fmul qword [0.07]  ->  then / timestep-scale
static int patchAimWalk(void) { return patchConstOp(0x63DBED, 0xDC, 0x0D, 0x8AE548, emitUnscaleByTimestep); }

// Before the force vector [ebp-0Ch..-4] is pushed to CPhysical::ApplyForce: scale it by timestep-scale.
static int patchPedPushCar(void)
{
    static const uint8_t orig[] = {0x8B, 0x55, 0xF4, 0x89, 0x48, 0x08}; // mov edx,[ebp-0Ch]; mov [eax+8],ecx
    uintptr_t site = va(0x55A62E);
    if (!bytesMatch(site, orig, sizeof orig)) return 0;

    Emitter e = stubBegin(); uint8_t *start = e.p;
    emitScaleEbpLocal(&e, -0x0C, 0);
    emitScaleEbpLocal(&e, -0x08, 0);
    emitScaleEbpLocal(&e, -0x04, 0);
    emitBytes(&e, orig, sizeof orig);
    emitJmp(&e, site + sizeof orig);
    stubEnd(&e, start);
    redirect(site, sizeof orig, start);
    return 1;
}

// ================================================================ automatic FPS limits

typedef struct RunningScript { struct RunningScript *next, *prev; char name[8]; } RunningScript;

typedef struct {
    int missions, minigames, schools, cutscenes, scriptedCutscenes;
} AutoLimitFlags;

static AutoLimitFlags g_auto;
static uint32_t g_savedLimit;

static int scriptLimit(const RunningScript *script)
{
    const char *n = script->name;
    if (g_auto.minigames && (_stricmp(n, "POOL2") == 0 || _stricmp(n, "GFSEX") == 0)) return 30;
    if (g_auto.missions && _stricmp(n, "DRUGS1") == 0) return *(const int32_t *)va(CURR_AREA) != 0 ? 50 : 0;
    if (g_auto.schools && (_stricmp(n, "DSKOOL") == 0 || _stricmp(n, "BOAT") == 0 || _stricmp(n, "BSKOOL") == 0)) return 80;
    return -1; // no opinion
}

// Same precedence as the original: cutscene, then scripted scene borders, then active scripts (last match wins).
static int preferredLimit(void)
{
    if (*(const uint8_t *)va(CUTSCENE_RUNNING)) return g_auto.cutscenes ? 60 : 0;
    if (*(const uint8_t *)va(WIDESCREEN_ON)) return g_auto.scriptedCutscenes ? 80 : 0;

    int pref = 0;
    for (const RunningScript *s = *(RunningScript *const *)va(ACTIVE_SCRIPTS); s; s = s->next) {
        int l = scriptLimit(s);
        if (l >= 0) pref = l;
    }
    return pref;
}

static void __cdecl autoLimitTick(void)
{
    uint32_t *limit = (uint32_t *)va(RSGLOBAL_FRAMELIMIT);
    int pref = preferredLimit();
    if (pref) {
        if (!g_savedLimit) g_savedLimit = *limit;
        *limit = (uint32_t)pref < g_savedLimit ? (uint32_t)pref : g_savedLimit;
    } else if (g_savedLimit) {
        *limit = g_savedLimit;
        g_savedLimit = 0;
    }
}

static void readAutoLimitFlags(void)
{
    g_auto.missions = readIni("AutoLimitFPS", "ForMissions", 1);
    g_auto.minigames = readIni("AutoLimitFPS", "ForMinigames", 1);
    g_auto.schools = readIni("AutoLimitFPS", "ForSchools", 1);
    g_auto.cutscenes = readIni("AutoLimitFPS", "ForCutscenes", 1);
    g_auto.scriptedCutscenes = readIni("AutoLimitFPS", "ForScriptedCutscenes", 1);
}

static int autoLimitEnabled(void)
{
    return g_auto.missions || g_auto.minigames || g_auto.schools || g_auto.cutscenes || g_auto.scriptedCutscenes;
}

// call CTimer::Update (in Idle)  ->  call stub: CTimer::Update, then autoLimitTick
static int patchAutoLimitHook(void)
{
    uintptr_t site = va(IDLE_TIMER_UPDATE);
    if (*(const uint8_t *)site != 0xE8 || site + 5 + *(const int32_t *)(site + 1) != va(TIMER_UPDATE)) return 0;

    Emitter e = stubBegin(); uint8_t *start = e.p;
    emitCall(&e, va(TIMER_UPDATE));
    emit8(&e, 0x60);                                   // pushad
    emitCall(&e, (uintptr_t)autoLimitTick);
    emit8(&e, 0x61);                                   // popad
    emit8(&e, 0xC3);                                   // ret
    stubEnd(&e, start);

    int32_t rel = (int32_t)((uintptr_t)start - (site + 5));
    writeBytes(site + 1, &rel, 4);
    return 1;
}

// ================================================================ init

static void report(const char *name, int ok) { logf_("%-30s %s", name, ok ? "OK" : "SKIPPED (bytes differ)"); }

static void applyLimiterAndWheels(int fps)
{
    if (fps > 0) {
        report("FPS limit", patchFpsLimit(fps));
        report("Remove min frame delay", patchFrameDelay());
    }
    report("Wheel spin on rails 1", patchWheelSpin(0x6E397F, 0x828));
    report("Wheel spin on rails 2", patchWheelSpin(0x6E398D, 0x82C));
    report("Wheel spin on rails 3", patchWheelSpin(0x6E399B, 0x830));
    report("Wheel spin on rails 4", patchWheelSpin(0x6E39A7, 0x834));
}

static void applyDoorsVehiclesSirens(void)
{
    report("Door force A (moving)", patchDoorForce(0x6F5D35));
    report("Door force A (stopped)", patchDoorForce(0x6F5D46));
    report("Door force B", patchDoorForce(0x6F5DF7));
    report("Door damping (firela ladder)", patchDoorDampingFirela());
    report("Door damping (flag 0x20)", patchDoorDampingFlagged());
    report("Door angle integration", patchDoorAngleIntegrate());
    report("Burnout", patchBurnout());
    report("Car slowdown 1", patchCarSlowdown(0x710F1D));
    report("Car slowdown 2", patchCarSlowdown(0x710F96));
    report("Car slowdown 3", patchCarSlowdown(0x7116FB));
    report("Car slowdown 4", patchCarSlowdown(0x711726));
    report("Car slowdown 5", patchCarSlowdown(0x711756));
    report("Siren toggle (time-based)", patchSiren());
}

static void applySwimmingPedsAutoLimits(void)
{
    report("Dive", patchDive());
    report("Dive come to surface", patchDiveSurface());
    report("Swim speed", patchSwimSpeed());
    report("Buoyancy (player)", patchBuoyancy());
    report("Heli rotor speed-up A", patchRotor(0x6F97EC, 0x8A3600, 1));
    report("Heli rotor speed-up B", patchRotor(0x6F97DE, 0x8A1FE8, 0));
    report("Skimmer resistance", patchSkimmer());
    report("Aiming while walking", patchAimWalk());
    report("Ped pushing car", patchPedPushCar());

    readAutoLimitFlags();
    if (autoLimitEnabled()) report("Auto FPS limits", patchAutoLimitHook());
}

static void init(HMODULE self)
{
    g_base = (uintptr_t)GetModuleHandleA(NULL);
    resolveModuleDir(self);
    openLog();
    logf_("Framerate Vigilante SA Steam " VERSION_TAG);
    logf_("gta-sa.exe base: %08X", (unsigned)g_base);

    if (!relocationArithmeticOk()) {
        logf_("Executable does not match the mapped Steam build. Nothing patched.");
        return;
    }
    if (!allocStubPool()) {
        logf_("Could not allocate memory for hooks. Nothing patched.");
        return;
    }
    applyLimiterAndWheels(readIni("Settings", "FPSlimit", 60));
    applyDoorsVehiclesSirens();
    applySwimmingPedsAutoLimits();
    logf_("Hook memory used: %u of %u bytes", (unsigned)g_stubUsed, STUB_POOL_SIZE);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) init(inst);
    return TRUE;
}
