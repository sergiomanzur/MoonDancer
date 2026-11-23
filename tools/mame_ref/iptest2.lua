local frame = 0
local function log(s) emu.print_info(s) end
local svc = manager.machine.ioport.ports[":SERVICE"]
local coin = svc.fields["Coin 1"]

-- Try setting the coin input at frame start (after MAME's frame_update).
emu.register_frame(function()
    local f = frame + 1
    if f >= 600 and f < 700 then coin:set_value(1) else coin:set_value(0) end
end, "frame")

emu.register_frame_done(function()
    frame = frame + 1
    if (frame >= 595 and frame <= 615) or frame % 100 == 0 then
        log(string.format("PORT %d svc=%02X", frame, svc:read()))
    end
    if frame >= 1200 then
        log("DONE")
        manager.machine:exit()
    end
end, "frame")
