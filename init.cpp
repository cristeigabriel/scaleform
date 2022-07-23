#include "init.hpp"
#include "sdk.hpp"

#include "scaleform/scaleform.hpp"

#include <minhook.h>

// hooks
#define prot_hook(name, type)                     \
struct name {                                 \
using fn_t = std::remove_pointer_t<type>; \
static fn_t fn;                           \
static inline fn_t* og;                   \
};                          

// decl hooks
prot_hook(level_init_pre_entity, void(__stdcall *)(const char *));
prot_hook(level_shutdown, void(__fastcall *)(void *, void *));
prot_hook(create_move, bool(__fastcall *)(void *, void *, float, tsf::user_cmd_t *));

// impl hooks
void level_init_pre_entity::fn(const char *map)
{
    DEBUG("LevelInitPreEntity\n");
    og(map);
    return scaleform_install();
}

void level_shutdown::fn(void *self, void *edx)
{
    // maybe get the retaddr of only one call. eh, this is very inexpensive so it doesn't matter
    DEBUG("LevelShutdown\n");
    og(self, edx);
    return scaleform_init();
}

bool create_move::fn(void *self, void *edx, float input_sample_time, tsf::user_cmd_t *cmd)
{
    if (!self || !cmd->get_command_number() || !input_sample_time)
        return og(self, edx, input_sample_time, cmd);
    
    bool ret = og(self, edx, input_sample_time, cmd);
    
    // TODO: This is not the ideal place for this...
    scaleform_tick();
    
    return ret;
}

// setup hooks
template <typename T>
static void hook_impl(std::optional<void *> &&target) {
    if (target.has_value() &&
        (MH_CreateHook(target.value(), (void*)(&T::fn), (void**)(&T::og)) == MH_OK))
    {
        LOG("Succesful hook at %x\n", (uintptr_t)target.value());
    } else {
        (void)(LOG("Failed hooking (address nil)\n"), exit(0));
    }
}
#define hook(a, ...) hook_impl<a>(__VA_ARGS__)

static void hooks_init()
{
    MH_Initialize();
    hook(level_init_pre_entity, ctx.client.find_string<void *, false>("(mapname)", MEMSCAN_FIRST_MATCH, {0x55, 0x8b, 0xec}, MEMSCAN_FIRST_MATCH, MS_FOLLOW_DIRECTION_BACKWARDS));
    hook(level_shutdown, ctx.client.find_string<void *, false>("(mapname)", MEMSCAN_FIRST_MATCH, {0x55, 0x8b, 0xec}, 1, MS_FOLLOW_DIRECTION_FORWARDS));
    hook(create_move, ctx.client.find_pattern<void *>("55 8B EC 56 8B F1 57 8B 7D 0C 8B 8E", MEMSCAN_FIRST_MATCH));
    MH_EnableHook(MH_ALL_HOOKS);
}

// setup ctx
static void ctx_init()
{
    using namespace memscan;
    ctx.client   = mapped_region_t(GetModuleHandleA("client.dll"));
    LOG("client:   %x %x\n", ctx.client.get_start(), ctx.client.get_end());
    ctx.engine   = mapped_region_t(GetModuleHandleA("engine.dll"));
    LOG("engine:   %x %x\n", ctx.engine.get_start(), ctx.engine.get_end());
    ctx.panorama = mapped_region_t(GetModuleHandleA("panorama.dll"));
    LOG("panorama: %x %x\n", ctx.panorama.get_start(), ctx.panorama.get_end());
    
    auto panorama = ctx.panorama.find_pattern<uintptr_t>("B9 CC CC CC CC 56 FF 50 40 8B", MEMSCAN_FIRST_MATCH);
    if (panorama.has_value())
        ctx.i.panorama = *(tsf::panorama_t **)(panorama.value() + 1);
    else (void)(LOG("Failed init (panorama interface nil)\n"), exit(0));
}

void ::init()
{
    ctx_init();
    scaleform_init();
    hooks_init();
}