-- Drive MAME from the same 5-byte-per-frame replay the native port reads,
-- using the emulated ioport fields rather than address-space taps (the
-- System 18 mapper hides the 315-5296 ports behind a mapped handler, so
-- program-space read taps never see them).
--
-- Config: build/mame_replay.cfg (key=value), keys:
--   replay, frames, snap_every, snap_from, snap_to, log_every, dump_frames
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
local REPLAY = cfg.replay or "build/kids1.txt"
local NFRAMES = tonumber(cfg.frames or "20000")
local SNAP_EVERY = tonumber(cfg.snap_every or "0")
local SNAP_FROM = tonumber(cfg.snap_from or "0")
local SNAP_TO = tonumber(cfg.snap_to or "999999")
local LOG_EVERY = tonumber(cfg.log_every or "1000")
local OFFSET = tonumber(cfg.offset or "0")   -- lua frame = port frame + OFFSET
local DUMP_FRAMES = {}
for tok in (cfg.dump_frames or ""):gmatch("%d+") do DUMP_FRAMES[tonumber(tok)] = true end

local rows = {}
do
    local f = assert(io.open(REPLAY, "r"))
    for line in f:lines() do
        local a, d = line:match("^%s*(%x+)%s+%x+%s+%x+%s+(%x+)")
        if a then rows[#rows + 1] = { tonumber(a, 16), tonumber(d, 16) } end
    end
    f:close()
end

local frame = 0
local function log(s) emu.print_info(s) end

local p1 = manager.machine.ioport.ports[":P1"]
local svc = manager.machine.ioport.ports[":SERVICE"]
local F = {
    left = p1.fields["P1 Left"], right = p1.fields["P1 Right"],
    up = p1.fields["P1 Up"], down = p1.fields["P1 Down"],
    b1 = p1.fields["P1 Button 1"], b2 = p1.fields["P1 Button 2"],
    b3 = p1.fields["P1 Button 3"],
    coin = svc.fields["Coin 2"], start = svc.fields["1 Player Start"],
}
for k, v in pairs(F) do
    if not v then log("MFIELD missing " .. k) end
end
local function set(field, pressed) if field then field:set_value(pressed and 1 or 0) end end
local function apply(byte, which)
    if which == "p1" then
        set(F.left, (byte & 0x80) == 0)
        set(F.right, (byte & 0x40) == 0)
        set(F.up, (byte & 0x20) == 0)
        set(F.down, (byte & 0x10) == 0)
        set(F.b1, (byte & 0x01) == 0)
        set(F.b2, (byte & 0x02) == 0)
        set(F.b3, (byte & 0x04) == 0)
    else
        set(F.coin, (byte & 0x01) == 0)
        set(F.start, (byte & 0x10) == 0)
    end
end

local function workram() return manager.machine.memory.shares[":workram"] end

local function save_bmp(path)
    local scr = manager.machine.screens[":screen"]
    local w, h = scr.width, scr.height
    local img = {}
    for y = h - 1, 0, -1 do
        local line = {}
        for x = 0, w - 1 do
            local p = scr:pixel(x, y)
            line[#line + 1] = string.char(p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF)
        end
        img[#img + 1] = table.concat(line)
    end
    local data = table.concat(img)
    local size = 54 + #data
    local f = assert(io.open(path, "wb"))
    f:write("BM", string.char(size & 0xFF, (size >> 8) & 0xFF, (size >> 16) & 0xFF, (size >> 24) & 0xFF,
        0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0))
    f:write(string.char(w & 0xFF, (w >> 8) & 0xFF, 0, 0, h & 0xFF, (h >> 8) & 0xFF, 0, 0))
    f:write(string.char(1, 0, 24, 0), string.rep("\0", 24), data)
    f:close()
end

local prev_e120, prev_e1e1 = -1, -1
emu.register_frame_done(function()
    frame = frame + 1
    local row = rows[math.max(1, math.min(frame - OFFSET + 1, #rows))] or { 0xFF, 0xFF }
    apply(row[1], "p1")
    apply(row[2], "svc")
    local sh = workram()
    if frame > 100 then
        local e120, e1e1 = sh:read_u8(0x2120), sh:read_u8(0x21E1)
        if e120 ~= prev_e120 or e1e1 ~= prev_e1e1 then
            log(string.format("MCHANGE %d e120=%d e1e1=%d cam=%d ply=%d credits=%d",
                frame, e120, e1e1, sh:read_u16(0x2110), sh:read_u16(0x21E2),
                sh:read_u8(0x2004) + sh:read_u8(0x2002) + sh:read_u8(0x2000)))
            prev_e120, prev_e1e1 = e120, e1e1
        end
    end
    if frame % LOG_EVERY == 0 then
        log(string.format("MPROBE %d cam=%d ply=%d e120=%d e1e1=%d credit=%d",
            frame, sh:read_u16(0x2110), sh:read_u16(0x21E2),
            sh:read_u8(0x2120), sh:read_u8(0x21E1), sh:read_u8(0x2004)))
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
