-- Drive the same combat replay as the native port and log camera/player state.
local frame = 0
local NFRAMES = 40000
local function log(s) emu.print_info(s) end
local spot = {}

local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local LEFT, RIGHT, UP, DOWN = 0x80, 0x40, 0x20, 0x10
local ATK, JUMP = 0x01, 0x02

local function p1_value()
    if frame < 900 then return 0xFF end
    if frame < 1040 then return 0xFF end
    local p = 0xFF & ~LEFT
    if math.floor(frame / 700) % 2 == 1 and frame > 20000 then
        p = 0xFF & ~RIGHT
    end
    if (frame % 10) < 7 then p = p & ~ATK end
    if frame % 260 < 8 then p = p & ~JUMP end
    if frame % 520 < 40 then p = p & ~UP end
    return p
end

local function svc_value()
    if frame >= 900 and frame < 940 then return 0xFF & ~0x01 end
    if frame >= 1000 and frame < 1040 then return 0xFF & ~0x10 end
    if frame % 700 == 0 and frame > 2000 then return 0xFF & ~0x10 end
    return 0xFF
end

local rtap = space:install_read_tap(0xE40000, 0xE4003F, "io", function(offset, data, mask)
    if offset == 0xE40008 then
        data = (data & 0xFF00) | svc_value()
    elseif offset == 0xE40000 then
        data = (data & 0xFF00) | p1_value()
    end
    return data, mask
end)

emu.register_frame_done(function()
    frame = frame + 1
    rtap:reinstall()
    if frame % 1000 == 0 then
        local sh = manager.machine.memory.shares[":workram"]
        local cam = sh:read_u16(0x2110)
        local ply = sh:read_u16(0x21E2)
        local a = sh:read_u16(0x2112)
        local b = sh:read_u16(0x21E4)
        log(string.format("MPROBE %d cam=%d ply=%d a=%d b=%d", frame, cam, ply, a, b))
        manager.machine.video:snapshot()
    end
    if frame >= NFRAMES then
        emu.print_info("MDONE")
        manager.machine:exit()
    end
end, "frame")
