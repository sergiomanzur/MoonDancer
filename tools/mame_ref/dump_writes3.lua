local writes = {}
local frame = 0
local space = manager.machine.devices[":maincpu"].spaces["program"]
local tap = space:install_write_tap(0x000000, 0xFFFFFF, "record", function(offset, data, mask)
    writes[string.format("%06X %04X %04X", offset, data, mask)] = true
end)
emu.register_frame_done(function()
    frame = frame + 1
    tap:reinstall()
    if frame ~= 900 then return end
    local f = io.open("build/writes_all.txt", "w")
    local keys = {}
    for k in pairs(writes) do keys[#keys+1] = k end
    table.sort(keys)
    for _, k in ipairs(keys) do f:write(k, "\n") end
    f:close()
    emu.print_info("dumped " .. #keys)
end, "frame")
