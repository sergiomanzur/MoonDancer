local frame = 0
local wanted = { [120]=true, [180]=true, [240]=true, [300]=true, [360]=true }
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
    frame = frame + 1
    if not wanted[frame] then return end
    manager.machine.video:snapshot()
    local text = manager.machine.memory.shares[":textram"]
    local pal = manager.machine.memory.shares[":paletteram"]
    if text then
        local t = {}
        for i = 0, 31 do t[#t+1] = string.format("%04X", text:read_u16(i*2)) end
        log(string.format("frame %d textram0: %s", frame, table.concat(t, " ")))
    end
    if pal then
        local p = {}
        for i = 0, 31 do p[#p+1] = string.format("%04X", pal:read_u16(i*2)) end
        log(string.format("frame %d pal0: %s", frame, table.concat(p, " ")))
    end
end, "frame")
