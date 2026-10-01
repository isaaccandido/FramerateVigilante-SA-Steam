// Native test harness: runs the real patch routines from src/main.c against a relocated copy of the
// Steam gta-sa.exe image, then executes each generated stub and checks its result.
// Build: zig cc -target x86-linux-musl -static -Itest/shim test/harness.c -o harness
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>

uintptr_t shim_imageBase;
#include "../src/main.c"

#define IMAGE_BASE 0x30000000u
#define IMAGE_SIZE 0x00A00000u

static int g_failures;

// ---------------------------------------------------------------- runners (stub returns via a 'ret' we plant at its exit)

__attribute__((naked)) static float runInOut(void *code, void *esiVal, float in)
{
    __asm__ volatile("push %esi\n push %edi\n push %ebx\n push %ebp\n"
                     "mov 20(%esp), %eax\n mov 24(%esp), %esi\n flds 28(%esp)\n"
                     "call *%eax\n"
                     "pop %ebp\n pop %ebx\n pop %edi\n pop %esi\n ret\n");
}

__attribute__((naked)) static void runInNone(void *code, void *esiVal, float in)
{
    __asm__ volatile("push %esi\n push %edi\n push %ebx\n push %ebp\n"
                     "mov 20(%esp), %eax\n mov 24(%esp), %esi\n flds 28(%esp)\n"
                     "call *%eax\n"
                     "pop %ebp\n pop %ebx\n pop %edi\n pop %esi\n ret\n");
}

__attribute__((naked)) static float runNoneOut(void *code, void *esiVal)
{
    __asm__ volatile("push %esi\n push %edi\n push %ebx\n push %ebp\n"
                     "mov 20(%esp), %eax\n mov 24(%esp), %esi\n"
                     "call *%eax\n"
                     "pop %ebp\n pop %ebx\n pop %edi\n pop %esi\n ret\n");
}

__attribute__((naked)) static uint32_t runSiren(void *code, void *esiVal)
{
    __asm__ volatile("push %esi\n push %edi\n push %ebx\n push %ebp\n"
                     "mov 20(%esp), %eax\n mov 24(%esp), %esi\n mov $0x1234, %edi\n"
                     "call *%eax\n"
                     "cmp $0x1234, %edi\n je 1f\n mov $0xBAD, %eax\n 1:\n"
                     "pop %ebp\n pop %ebx\n pop %edi\n pop %esi\n ret\n");
}

// ---------------------------------------------------------------- general runner: registers + x87 in/out

typedef struct {
    uint32_t eax, ecx, edx, esi, ebp; // 0,4,8,12,16
    uint32_t hasIn;                   // 20
    float in;                         // 24
    void *code;                       // 28
    uint32_t nOut;                    // 32
    float out[2];                     // 36,40
} Ctx;

__attribute__((naked)) static void runCtx(Ctx *c)
{
    __asm__ volatile("push %ebx\n push %esi\n push %edi\n push %ebp\n"
                     "mov 20(%esp), %ebx\n"
                     "cmpl $0, 20(%ebx)\n je 1f\n flds 24(%ebx)\n 1:\n"
                     "mov 0(%ebx), %eax\n mov 4(%ebx), %ecx\n mov 8(%ebx), %edx\n"
                     "mov 12(%ebx), %esi\n mov 16(%ebx), %ebp\n"
                     "push %ebx\n call *28(%ebx)\n pop %ebx\n"
                     "mov %eax, 0(%ebx)\n mov %ecx, 4(%ebx)\n mov %edx, 8(%ebx)\n"
                     "mov 32(%ebx), %ecx\n lea 36(%ebx), %edx\n"
                     "2:\n test %ecx, %ecx\n je 3f\n fstps (%edx)\n add $4, %edx\n dec %ecx\n jmp 2b\n 3:\n"
                     "pop %ebp\n pop %edi\n pop %esi\n pop %ebx\n ret\n");
}

// ---------------------------------------------------------------- helpers

static void loadImage(const char *path)
{
    void *m = mmap((void *)IMAGE_BASE, IMAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (m != (void *)IMAGE_BASE) { perror("mmap"); exit(2); }
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open"); exit(2); }
    size_t off = 0; ssize_t r;
    while ((r = read(fd, (char *)m + off, 1 << 20)) > 0) off += (size_t)r;
    close(fd);
    shim_imageBase = IMAGE_BASE;
}

static void *stubAt(uint32_t siteVa)
{
    uintptr_t s = va(siteVa);
    if (*(uint8_t *)s != 0xE9) return NULL;
    return (void *)(s + 5 + *(int32_t *)(s + 1));
}

static void plantRet(uint32_t siteVa) { *(uint8_t *)va(siteVa) = 0xC3; }

static float *fieldF(void *obj, int off) { return (float *)((uint8_t *)obj + off); }

static void expectNear(const char *name, float got, float want)
{
    int ok = fabsf(got - want) <= 1e-4f * (1.0f + fabsf(want));
    if (!ok) g_failures++;
    printf("%-36s %-4s got %.6f want %.6f\n", name, ok ? "PASS" : "FAIL", got, want);
}

static void expectEq(const char *name, uint32_t got, uint32_t want)
{
    int ok = got == want;
    if (!ok) g_failures++;
    printf("%-36s %-4s got %X want %X\n", name, ok ? "PASS" : "FAIL", got, want);
}

static void setTimestep(float ts) { *(float *)va(TIMESTEP) = ts; }

// ---------------------------------------------------------------- tests

static float g_obj[0x400];

static void testWheels(float ts)
{
    static const uint32_t sites[] = {0x6E397F, 0x6E398D, 0x6E399B, 0x6E39A7};
    static const int offs[] = {0x828, 0x82C, 0x830, 0x834};
    for (int i = 0; i < 4; i++) {
        plantRet(sites[i] + 6);
        *fieldF(g_obj, offs[i]) = 10.0f;
        char n[64]; snprintf(n, sizeof n, "wheel spin %d", i + 1);
        expectNear(n, runInOut(stubAt(sites[i]), g_obj, 2.0f), 2.0f * ts + 10.0f);
    }
}

static void testDoorForces(float ts)
{
    static const uint32_t sites[] = {0x6F5D35, 0x6F5D46, 0x6F5DF7};
    for (int i = 0; i < 3; i++) {
        plantRet(sites[i] + 6);
        *fieldF(g_obj, 0x14) = 1.0f;
        runInNone(stubAt(sites[i]), g_obj, 0.5f);
        char n[64]; snprintf(n, sizeof n, "door force %d", i + 1);
        expectNear(n, *fieldF(g_obj, 0x14), 1.0f + 0.5f * ts * 0.6f);
    }
}

static void testDoorDamping(float ts)
{
    plantRet(0x6F5E1E + 6);
    expectNear("door damping firela", runInOut(stubAt(0x6F5E1E), g_obj, 2.0f), 2.0f * (1.0f - ts * 0.6f * 0.08f));

    plantRet(0x6F5E40 + 6);
    *((uint8_t *)g_obj + 8) = 0x20;
    expectNear("door damping flag 0x20", runInOut(stubAt(0x6F5E40), g_obj, 2.0f), 2.0f * (1.0f - ts * 0.6f * 0.03f));
    *((uint8_t *)g_obj + 8) = 0x00;
    expectNear("door damping other (unchanged)", runInOut(stubAt(0x6F5E40), g_obj, 2.0f), 2.0f * 0.97f);
}

static void testDoorAngle(float ts)
{
    plantRet(0x6F5EAD + 5);
    *fieldF(g_obj, 0x14) = 3.0f;
    expectNear("door angle integrate", runInOut(stubAt(0x6F5EAD), g_obj, 1.0f), 1.0f + 3.0f * ts * 0.6f);
}

static void testBurnout(float ts)
{
    plantRet(0x6D2DB4 + 6);
    expectNear("burnout", runInOut(stubAt(0x6D2DB4), g_obj, 1500.0f), 3000.0f / 1500.0f * ts * 0.6f);
}

static void testCarSlowdown(float ts)
{
    static const uint32_t sites[] = {0x710F1D, 0x710F96, 0x7116FB, 0x711726, 0x711756};
    *(float *)va(HANDLING_SLOWDOWN) = 0.9f;
    for (int i = 0; i < 5; i++) {
        plantRet(sites[i] + 6);
        char n[64]; snprintf(n, sizeof n, "car slowdown %d", i + 1);
        expectNear(n, runNoneOut(stubAt(sites[i]), g_obj), 0.9f * ts * 0.6f);
    }
}

// Siren: landing pads replace the three resume points with "pop edi; mov eax, id; ret".
static void plantSirenLanding(uintptr_t at, uint8_t id)
{
    uint8_t code[] = {0x5F, 0xB8, id, 0, 0, 0, 0xC3};
    memcpy((void *)at, code, sizeof code);
}

static uint32_t sirenStep(void *stub, uint8_t *vehicle, uint8_t *pad, uint32_t now, int hornNow, int hornBefore)
{
    *(uint32_t *)va(TIMER_TIME_MS) = now;
    *(int16_t *)(pad + 0x24) = (int16_t)hornNow;   // NewState.ShockButtonL
    *(int16_t *)(pad + 0x54) = (int16_t)hornBefore; // OldState.ShockButtonL
    return runSiren(stub, vehicle);
}

static void testSiren(void)
{
    void *stub = stubAt(0x71ABD1);
    plantSirenLanding(g_sirenToggle, 1);
    plantSirenLanding(g_sirenHorn, 2);
    plantSirenLanding(g_sirenNoHorn, 3);

    static uint8_t vehicle[0x600], playerPed[16];
    *(void **)(vehicle + VEHICLE_DRIVER) = playerPed;
    *(void **)va(PLAYERS) = playerPed;
    uint8_t *pad = (uint8_t *)va(PADS);
    memset(pad, 0, PAD_SIZE * 2);

    expectEq("siren: idle -> no horn", sirenStep(stub, vehicle, pad, 1000, 0, 0), 3);
    expectEq("siren: press -> no horn yet", sirenStep(stub, vehicle, pad, 1010, 1, 0), 3);
    expectEq("siren: quick release -> toggle", sirenStep(stub, vehicle, pad, 1100, 0, 1), 1);
    expectEq("siren: press again", sirenStep(stub, vehicle, pad, 2000, 1, 0), 3);
    expectEq("siren: hold 200ms -> horn", sirenStep(stub, vehicle, pad, 2200, 1, 1), 2);
    expectEq("siren: release after hold -> none", sirenStep(stub, vehicle, pad, 2300, 0, 1), 3);

    // second player drives: must read pad 1, so pad 0 input is ignored
    static uint8_t otherPed[16];
    *(void **)(vehicle + VEHICLE_DRIVER) = otherPed;
    expectEq("siren: player 2 ignores pad 0", sirenStep(stub, vehicle, pad, 3000, 1, 1), 3);
    *(int16_t *)(pad + PAD_SIZE + 0x24) = 1;
    *(int16_t *)(pad + PAD_SIZE + 0x54) = 1;
    *(uint32_t *)va(TIMER_TIME_MS) = 4000;
    expectEq("siren: player 2 horn via pad 1", runSiren(stub, vehicle), 2);
}

// ---------------------------------------------------------------- batch 3

static float g_frame[64];                       // fake stack frame; ebp points into the middle
static uint32_t ebpOf(void) { return (uint32_t)(uintptr_t)&g_frame[32]; }
static float *local(int disp) { return (float *)(uintptr_t)(ebpOf() + disp); }

static float runFloat(uint32_t siteVa, int landingOff, float in)
{
    plantRet(siteVa + landingOff);
    Ctx c = {0}; c.code = stubAt(siteVa); c.esi = (uint32_t)(uintptr_t)g_obj; c.ebp = ebpOf();
    c.hasIn = 1; c.in = in; c.nOut = 1;
    runCtx(&c);
    return c.out[0];
}

static void testSwimDive(float ts)
{
    float s = ts * 0.6f;
    expectNear("dive", runFloat(0x6B740D, 6, 3.0f), 3.0f * -0.1f * s);
    expectNear("dive come to surface", runFloat(0x6B74E8, 6, 0.5f), (0.5f + 0.01f) / s);

    plantRet(0x6B7534 + 5);
    *local(-0x20) = 2.0f; *local(-0x1C) = 4.0f; *local(-0x18) = 8.0f;
    *fieldF(g_obj, 0x44) = 10.0f;
    Ctx c = {0}; c.code = stubAt(0x6B7534); c.esi = (uint32_t)(uintptr_t)g_obj; c.ebp = ebpOf();
    c.hasIn = 1; c.in = 0.5f; c.nOut = 2;
    runCtx(&c);
    expectNear("swim speed X target", *local(-0x20), 2.0f / s);
    expectNear("swim speed Y target", *local(-0x1C), 4.0f / s);
    expectNear("swim speed Z target (unchanged)", *local(-0x18), 8.0f);
    expectNear("swim original fld/fmul", c.out[0], 0.5f * 10.0f);
}

static void runBuoyancy(uint8_t *entity, float value)
{
    plantRet(0x6F6BD4 + 6);
    Ctx c = {0}; c.code = stubAt(0x6F6BD4); c.eax = (uint32_t)(uintptr_t)entity; c.edx = 0xDEADBEEF;
    c.ebp = ebpOf(); c.hasIn = 1; c.in = value; c.nOut = 0;
    runCtx(&c);
    if (c.edx != 0xDEADBEEF) { g_failures++; puts("buoyancy: edx clobbered FAIL"); }
}

static void testBuoyancy(float ts)
{
    static uint8_t entity[0x600];
    float clamped = ts < 0.7f ? 0.7f : ts, s = ts * 0.6f;

    entity[0x36] = 2; // vehicle
    *local(0x0C) = clamped; runBuoyancy(entity, 5.0f);
    expectNear("buoyancy non-ped (unchanged)", *local(0x0C), 5.0f * clamped);

    entity[0x36] = 3; *(uint32_t *)(entity + 0x598) = 0; // player ped
    *local(0x0C) = clamped; runBuoyancy(entity, 5.0f);
    expectNear("buoyancy player", *local(0x0C), 5.0f * (1.0f + s / 1.5f) * s);

    *(uint32_t *)(entity + 0x598) = 6; // NPC ped
    *local(0x0C) = clamped; runBuoyancy(entity, 5.0f);
    expectNear("buoyancy npc (unchanged)", *local(0x0C), 5.0f * clamped);
}

static void testVehiclesPeds(float ts)
{
    float s = ts * 0.6f;
    expectNear("heli rotor A", runFloat(0x6F97EC, 6, 0.1f), 0.1f + 0.22f / 220.0f * 3.0f * s);
    expectNear("heli rotor B", runFloat(0x6F97DE, 6, 0.1f), 0.1f + 0.22f / 220.0f * s);
    expectNear("skimmer resistance", runFloat(0x70C8ED, 6, 2.0f), 2.0f * 30.0f * s);
    expectNear("aiming while walking", runFloat(0x63DBED, 6, ts), ts * 0.07f / s);

    plantRet(0x55A62E + 6);
    *local(-0x0C) = 1.0f; *local(-0x08) = 2.0f; *local(-0x04) = 3.0f;
    uint32_t pushed[4] = {0};
    Ctx c = {0}; c.code = stubAt(0x55A62E); c.ebp = ebpOf(); c.eax = (uint32_t)(uintptr_t)pushed; c.ecx = 0x11223344;
    runCtx(&c);
    expectNear("ped push car X", *local(-0x0C), 1.0f * s);
    expectNear("ped push car Y", *local(-0x08), 2.0f * s);
    expectNear("ped push car Z", *local(-0x04), 3.0f * s);
    float edxAsFloat; memcpy(&edxAsFloat, &c.edx, 4);
    expectNear("ped push car edx = scaled X", edxAsFloat, 1.0f * s);
    expectEq("ped push car [eax+8] = ecx", pushed[2], 0x11223344);
}

static RunningScript *makeScript(RunningScript *s, const char *name, RunningScript *next)
{
    memset(s, 0, sizeof *s);
    strncpy(s->name, name, 7);
    s->next = next;
    return s;
}

static void testAutoLimit(void)
{
    uint32_t *limit = (uint32_t *)va(RSGLOBAL_FRAMELIMIT);
    RunningScript **head = (RunningScript **)va(ACTIVE_SCRIPTS);
    static RunningScript a, b;
    *limit = 60; g_savedLimit = 0;
    *(uint8_t *)va(CUTSCENE_RUNNING) = 0; *(uint8_t *)va(WIDESCREEN_ON) = 0; *(int32_t *)va(CURR_AREA) = 0;

    // exercise through the real hook: plant 'ret' at CTimer::Update, call the patched call site's stub
    uintptr_t site = va(IDLE_TIMER_UPDATE);
    void *hookStub = (void *)(site + 5 + *(int32_t *)(site + 1));
    plantRet(TIMER_UPDATE);
    Ctx c = {0}; c.code = hookStub;

    *head = makeScript(&a, "MAIN", makeScript(&b, "POOL2", NULL));
    runCtx(&c); expectEq("auto limit: pool -> 30", *limit, 30);
    *head = makeScript(&a, "MAIN", NULL);
    runCtx(&c); expectEq("auto limit: pool ends -> restore", *limit, 60);
    *head = makeScript(&a, "drugs1", NULL);
    runCtx(&c); expectEq("auto limit: DRUGS1 outside -> 60", *limit, 60);
    *(int32_t *)va(CURR_AREA) = 3;
    runCtx(&c); expectEq("auto limit: DRUGS1 interior -> 50", *limit, 50);
    *(int32_t *)va(CURR_AREA) = 0; *head = NULL;
    *(uint8_t *)va(WIDESCREEN_ON) = 1;
    runCtx(&c); expectEq("auto limit: scripted scene 80 (cap 60)", *limit, 60);
    *(uint8_t *)va(WIDESCREEN_ON) = 0;
    runCtx(&c); expectEq("auto limit: scripted scene ends -> 60", *limit, 60);
    *limit = 100;
    *(uint8_t *)va(WIDESCREEN_ON) = 1;
    runCtx(&c); expectEq("auto limit: scripted scene at cap 100", *limit, 80);
    *(uint8_t *)va(WIDESCREEN_ON) = 0;
    runCtx(&c); expectEq("auto limit: restore 100", *limit, 100);
}

static void printLog(void)
{
    FILE *f = fopen(".\\FramerateVigilanteSteam.log", "r");
    char line[256];
    while (f && fgets(line, sizeof line, f)) {
        fputs(line, stdout);
        if (strstr(line, "SKIPPED") || strstr(line, "Nothing patched")) g_failures++;
    }
    if (f) fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s steam_reloc.bin\n", argv[0]); return 2; }
    loadImage(argv[1]);
    init(NULL);
    printLog();
    expectEq("hook memory within pool", g_stubUsed < STUB_POOL_SIZE, 1);
    puts("---");

    const float steps[] = {50.0f / 30.0f, 50.0f / 60.0f};
    for (int i = 0; i < 2; i++) {
        printf("[timestep %.4f]\n", steps[i]);
        setTimestep(steps[i]);
        testWheels(steps[i]);
        testDoorForces(steps[i]);
        testDoorDamping(steps[i]);
        testDoorAngle(steps[i]);
        testBurnout(steps[i]);
        testCarSlowdown(steps[i]);
        testSwimDive(steps[i]);
        testBuoyancy(steps[i]);
        testVehiclesPeds(steps[i]);
    }
    puts("[siren]");
    testSiren();
    puts("[auto FPS limits]");
    testAutoLimit();

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures != 0;
}
