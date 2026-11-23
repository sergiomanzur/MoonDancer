local frame = 0
local function log(s) emu.print_info(s) end
local svc = manager.machine.ioport.ports[":SERVICE"]
emu.register_frame_done(function()
    frame = frame + 1
    if frame == 30 then
        for name, f in pairs(svc.fields) do
            log(string.format("F %s mask=%02X type=%s class=%s enabled=%s impulse=%s toggle=%s def=%s",
                name, f.mask, tostring(f.type), tostring(f.type_class), tostring(f.enabled),
                tostring(f.impulse), tostring(f.toggle), tostring(f.defvalue)))
        end
    end
    local c1 = svc.fields["Coin 1"]
    local c2 = svc.fields["Coin 2"]
    local c3 = svc.fields["Coin 3"]
    if frame >= 100 and frame < 200 then
        if c1 then c1:set_value(1) end
        if c2 then c2:set_value(1) end
        if c3 then c3:set_value(1) end
    else
        if c1 then c1:set_value(0) end
        if c2 then c2:set_value(0) end
        if c3 then c3:set_value(0) end
    end
    if frame >= 95 and frame <= 110 or frame % 100 == 0 then
        log(string.format("PORT %d svc=%02X", frame, svc:read()))
    end
    if frame >= 400 then log("DONE"); manager.machine:exit() end
end, "frame")
