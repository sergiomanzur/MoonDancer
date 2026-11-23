local frame = 0
local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local tap = space:install_write_tap(0x000000, 0xFFFFFF, "rec", function(offset, data, mask)
    if offset >= 0xFE0000 and offset <= 0xFE003F then
        emu.print_info(string.format("W %06X=%04X m=%04X PC=%06X", offset, data, mask, cpu.state["CURPC"].value))
    end
end)
emu.register_frame_done(function()
    frame = frame + 1
    tap:reinstall()
    if frame == 300 then emu.print_info("done") end
end, "frame")
