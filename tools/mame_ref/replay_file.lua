-- Drive MAME with the exact same 5-byte-per-frame replay the native port reads.
-- Replay line format: "p1 p2 p3 service coinage" (active low).
-- The native port applies p1 to the 68000 P1 port and service to the misc
-- input port (bit 0 = coin, bit 4 = start); other bytes are unused by mwalk.
--
-- Usage: mame.exe mwalk -rompath refroms -autoboot_script tools/mame_ref/replay_file.lua
-- Env vars (MAME Lua has no argv): REPLAY_FILE, SNAP_EVERY, DUMP_FRAMES, NFRAMES
-- We read them from a small config file instead: build/mame_replay.cfg
local cfg = {}
do
    local f = io.open("build/mame_replay.cfg", "r")
    if f then
        for line in f:lines() do
            local k, v = line:match("^([%w_]+)=(.*)$")
            if k then cfg[k] = v end
        end
        f:close()
    end
end
local REPLAY = cfg.replay or "build/r_spam_right.txt"
local NFRAMES = tonumber(cfg.frames or "20000")
local SNAP_EVERY = tonumber(cfg.snap_every or "0")
local SNAP_FROM = tonumber(cfg.snap_from or "0")
local SNAP_TO = tonumber(cfg.snap_to or "999999")
local DUMP_FRAMES = {}
for tok in (cfg.dump_frames or ""):gmatch("%d+") do DUMP_FRAMES[tonumber(tok)] = true end
local LOG_EVERY = tonumber(cfg.log_every or "500")

local rows = {}
do
    local f = assert(io.open(REPLAY, "r"))
    for line in f:lines() do
        local a, b, c, d, e = line:match("^%s*(%x+)%s+(%x+)%s+(%x+)%s+(%x+)%s+(%x+)")
        if a then
            rows[#rows + 1] = { tonumber(a, 16), tonumber(d, 16) }
        end
    end
    f:close()
end

local frame = 0
local function log(s) emu.print_info(s) end
local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local function in_row()
    if frame < #rows then return rows[frame + 1] end
    return { 0xFF, 0xFF }
end

local tap = space:install_read_tap(0xE40000, 0xE4003F, "io", function(offset, data, mask)
    local r = in_row()
    if offset == 0xE40000 then
        data = (data & 0xFF00) | r[1]
    elseif offset == 0xE40008 then
        data = (data & 0xFF00) | r[2]
    end
    return data, mask
end)

local function workram()
    return manager.machine.memory.shares[":workram"]
end

local prev_e120, prev_e1e1, prev_e125 = -1, -1, -1

-- Save the emulated screen as a 24-bit BMP without going through the video
-- output, so the oracle works under -video none (MAME's video:snapshot()
-- produces no file in that mode).
local function save_bmp(path)
    local scr = manager.machine.screens[":screen"]
    local w, h = scr.width, scr.height
    local row = math.floor((w * 3 + 3) / 4) * 4
    local img = {}
    for y = h - 1, 0, -1 do
        local line = {}
        for x = 0, w - 1 do
            local p = scr:pixel(x, y)
            line[#line + 1] = string.char(p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF)
        end
        local s = table.concat(line)
        if row > w * 3 then s = s .. string.rep("\0", row - w * 3) end
        img[#img + 1] = s
    end
    local data = table.concat(img)
    local f = assert(io.open(path, "wb"))
    local size = 54 + #data
    f:write("BM", string.char(size & 0xFF, (size >> 8) & 0xFF, (size >> 16) & 0xFF, (size >> 24) & 0xFF,
        0, 0, 0, 0, 54, 0, 0, 0))
    f:write(string.char(40, 0, 0, 0))
    f:write(string.char(w & 0xFF, (w >> 8) & 0xFF, (w >> 16) & 0xFF, (w >> 24) & 0xFF))
    f:write(string.char(h & 0xFF, (h >> 8) & 0xFF, (h >> 16) & 0xFF, (h >> 24) & 0xFF))
    f:write(string.char(1, 0, 24, 0))
    f:write(string.rep("\0", 24))
    f:write(data)
    f:close()
end

emu.register_frame_done(function()
    frame = frame + 1
    tap:reinstall()
    local sh = workram()
    -- Log every change of the scene / child-task counters.  e120 is the
    -- active hidden-children task count used by the scene-advance check;
    -- e1e1 is the current act/scene index used to index the room table.
    if frame > 100 then
        local e120 = sh:read_u8(0x2120)
        local e1e1 = sh:read_u8(0x21E1)
        local e125 = sh:read_u8(0x2125)
        if e120 ~= prev_e120 or e1e1 ~= prev_e1e1 or e125 ~= prev_e125 then
            log(string.format("MCHANGE %d e120=%d e1e1=%d e125=%d cam=%d ply=%d",
                frame, e120, e1e1, e125,
                sh:read_u16(0x2110), sh:read_u16(0x21E2)))
            prev_e120, prev_e1e1, prev_e125 = e120, e1e1, e125
        end
    end
    if frame % LOG_EVERY == 0 then
        local cam = sh:read_u16(0x2110)
        local ply = sh:read_u16(0x21E2)
        local score_guess = sh:read_u16(0x0A60)
        local e120 = sh:read_u8(0x2120)
        local e127 = sh:read_u8(0x2127)
        local e1e1 = sh:read_u8(0x21E1)
        log(string.format("MPROBE %d cam=%d ply=%d a=%d b=%d e120=%d e127=%d e1e1=%d w=%02X%02X",
            frame, cam, ply, sh:read_u16(0x2112), sh:read_u16(0x21E4), e120, e127, e1e1,
            sh:read_u8(0x0A61), sh:read_u8(0x0A60)))
    end
    if SNAP_EVERY > 0 and frame >= SNAP_FROM and frame <= SNAP_TO and frame % SNAP_EVERY == 0 then
        save_bmp(string.format("build/mameshot_%d.bmp", frame))
        log("MSNAP " .. frame)
    end
    if DUMP_FRAMES[frame] then
        local f = io.open(string.format("build/mame_wr_%d.bin", frame), "wb")
        if f then
            for i = 0, sh.size - 1 do f:write(string.char(sh:read_u8(i))) end
            f:close()
        end
        log("MDUMP " .. frame)
    end
    if frame >= NFRAMES then
        log("MDONE " .. frame)
        manager.machine:exit()
    end
end, "frame")
