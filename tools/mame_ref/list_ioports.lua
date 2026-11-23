local frame = 0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
    frame = frame + 1
    if frame == 30 then
        for pname, port in pairs(manager.machine.ioport.ports) do
            for fname, field in pairs(port.fields) do
                local ok, v = pcall(function() return field:read() end)
                log(string.format("IOPORT %s %s read=%s", pname, fname, tostring(v)))
            end
        end
        manager.machine:exit()
    end
end, "frame")
