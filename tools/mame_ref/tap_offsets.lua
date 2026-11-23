local frame = 0
local seen = {}
local function log(s) emu.print_info(s) end
local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
space:install_read_tap(0xE40000, 0xE4003F, "io", function(offset, data, mask)
    if not seen[offset] then
        seen[offset] = true
        log(string.format("TAPOFFSET %X data=%04X", offset, data))
    end
    return data, mask
end)
emu.register_frame_done(function()
    frame = frame + 1
    if frame == 3000 then
        log("DONE")
        manager.machine:exit()
    end
end, "frame")
