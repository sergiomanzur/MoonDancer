local frame = 0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
    frame = frame + 1
    if frame ~= 200 then return end
    for tag, port in pairs(manager.machine.ioport.ports) do
        log("PORT "..tostring(tag))
        for name, field in pairs(port.fields) do
            local v = field.value
            log(string.format("  FIELD %s value=%s", tostring(name), tostring(v)))
        end
    end
end, "frame")
