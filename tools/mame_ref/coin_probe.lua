local frame = 0
local function log(s) emu.print_info(s) end
local function svc() return manager.machine.ioport.ports[":SERVICE"] end
emu.register_frame_done(function()
    frame = frame + 1
    if frame == 200 then
        for name, field in pairs(svc().fields) do
            local ok, v = pcall(function() return field:read() end)
            log("FIELD "..tostring(name).." read="..tostring(v))
        end
    end
    if frame == 210 then
        local ok, err = pcall(function() svc().fields["Coin 1"]:set_value(0) end)
        log("set coin 1: "..tostring(ok).." "..tostring(err))
    end
    if frame == 220 then
        pcall(function() svc().fields["Coin 1"]:set_value(1) end)
    end
    if frame == 300 or frame == 400 or frame == 600 then
        local sh = manager.machine.memory.shares[":workram"]
        log(string.format("frame %d credits %02X %02X %02X", frame,
            sh:read_u8(0x2000), sh:read_u8(0x2002), sh:read_u8(0x2004)))
    end
end, "frame")
